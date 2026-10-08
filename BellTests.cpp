#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

int runLegacyBellTests()
{
    const juce::ScopedJuceInitialiser_GUI initialiseJuce;

    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    OrganicChemistryAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (sr, blockSize);
    processor.prepareToPlay (sr, blockSize);
    processor.setMoleculeEmpty (false);   // 离线测试：非空分子才发声

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);

    double energyAttack = 0.0, energyTail = 0.0, peak = 0.0;
    int samplesAttack = 0, samplesTail = 0;

    std::vector<float> audio;
    audio.reserve ((size_t) sr);

    for (int offset = 0; offset < (int) sr; offset += blockSize)
    {
        const int count = juce::jmin (blockSize, (int) sr - offset);
        buffer.setSize (2, count, false, false, true);
        midi.clear();
        if (offset == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        processor.processBlock (buffer, midi);

        for (int s = 0; s < count; ++s)
        {
            const float x = buffer.getSample (0, s);
            audio.push_back (x);
            peak = std::max (peak, (double) std::fabs (x));
            if (offset + s < (int) (sr * 0.3))
            {
                energyAttack += (double) x * x;
                ++samplesAttack;
            }
            else if (offset + s >= (int) (sr * 0.5))
            {
                energyTail += (double) x * x;
                ++samplesTail;
            }
        }
    }

    const double rms = std::sqrt ((energyAttack + energyTail) / (samplesAttack + samplesTail));
    const double attackRms = std::sqrt (energyAttack / std::max (1, samplesAttack));
    const double tailRms = std::sqrt (energyTail / std::max (1, samplesTail));

    std::cout << "Bell 音色离线验证\n";
    std::cout << "  峰值     = " << peak << " (应 < 1)\n";
    std::cout << "  总 RMS   = " << rms << "\n";
    std::cout << "  前0.3s RMS = " << attackRms << "\n";
    std::cout << "  0.5s后 RMS = " << tailRms << "\n";
    std::cout << "  衰减比   = " << (attackRms / std::max (tailRms, 1e-9)) << " (钟形应 > 2)\n";

    bool ok = true;
    if (! (peak > 0.01 && peak < 1.0)) { std::cout << "  [FAIL] 峰值异常\n"; ok = false; }
    if (! (rms > 0.005))              { std::cout << "  [FAIL] 输出过弱\n"; ok = false; }
    if (! (attackRms > tailRms * 2.0)){ std::cout << "  [FAIL] 非钟形包络\n"; ok = false; }

    // 频谱：找峰值频率，验证双八度分量（+12 移调后基频 ~523Hz + 高八度 ~1047Hz）。
    const int fftSize = 1 << 14;
    juce::dsp::FFT fft (14);
    std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
    for (int i = 0; i < std::min (fftSize, (int) audio.size()); ++i)
        fftData[(size_t) i] = audio[(size_t) i] * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * i / fftSize));
    fft.performRealOnlyForwardTransform (fftData.data(), false);

    int peakBin = 0;
    float peakMag = 0.0f;
    for (int bin = 10; bin < fftSize / 2; ++bin)
    {
        const float mag = std::sqrt (fftData[(size_t) 2 * bin] * fftData[(size_t) 2 * bin]
                                   + fftData[(size_t) 2 * bin + 1] * fftData[(size_t) 2 * bin + 1]);
        if (mag > peakMag) { peakMag = mag; peakBin = bin; }
    }
    const double peakFreq = peakBin * sr / fftSize;
    std::cout << "  频谱峰值  = " << peakFreq << " Hz (期望 ~523 或 ~1047)\n";

    const auto near = [] (double a, double b, double tol) { return std::fabs (a - b) < tol; };
    const bool hasFundamental = near (peakFreq, 523.0, 40.0);
    const bool hasOctave = near (peakFreq, 1046.0, 40.0);
    if (! (hasFundamental || hasOctave)) { std::cout << "  [FAIL] 频谱峰值不符合双八度\n"; ok = false; }

    // 高音高回归：E9/G9（note 124/127）曾因滤波 keytrack 把截止频率推到
    // Nyquist 以上，导致 TPT 滤波器 tan() 溢出为 Inf/NaN 并永久污染状态
    // （电流声后整机静音）。修复后应无 NaN，且高音之后普通音仍正常出声。
    {
        OrganicChemistryAudioProcessor hi;
        hi.setRateAndBufferSizeDetails (sr, blockSize);
        hi.prepareToPlay (sr, blockSize);
        hi.setMoleculeEmpty (false);

        juce::AudioBuffer<float> b (2, blockSize);
        juce::MidiBuffer m;
        bool anyNan = false;
        for (int note : { 124, 127 })
        {
            for (int offset = 0; offset < (int) sr; offset += blockSize)
            {
                const int count = juce::jmin (blockSize, (int) sr - offset);
                b.setSize (2, count, false, false, true);
                m.clear();
                if (offset == 0)
                    m.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
                hi.processBlock (b, m);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < count; ++s)
                        if (! std::isfinite (b.getSample (ch, s))) anyNan = true;
            }
        }

        // 高音之后，普通音仍应正常出声（整机未被 NaN 污染）。
        double recovery = 0.0;
        const int recSamples = (int) (sr * 0.5);
        for (int offset = 0; offset < recSamples; offset += blockSize)
        {
            const int count = juce::jmin (blockSize, recSamples - offset);
            b.setSize (2, count, false, false, true);
            m.clear();
            if (offset == 0)
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            hi.processBlock (b, m);
            for (int s = 0; s < count; ++s)
                recovery += (double) b.getSample (0, s) * b.getSample (0, s);
        }
        const double recoveryRms = std::sqrt (recovery / std::max (1, recSamples));

        std::cout << "  高音 NaN 检测   = " << (anyNan ? "有 NaN (FAIL)" : "无 NaN (OK)") << "\n";
        std::cout << "  高音后恢复 RMS  = " << recoveryRms << " (应 > 0.005)\n";

        if (anyNan)                   { std::cout << "  [FAIL] 高音高产生 NaN\n"; ok = false; }
        if (! (recoveryRms > 0.005))  { std::cout << "  [FAIL] 高音后整机失效\n"; ok = false; }
    }

    std::cout << (ok ? "BELL TEST PASSED\n" : "BELL TEST FAILED\n");
    return ok ? 0 : 1;
}

namespace
{
using organic::Element;
using organic::Molecule;
using Audio = std::array<std::vector<float>, 2>;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

juce::ValueTree topology (std::initializer_list<Element> elements,
                          std::initializer_list<organic::PresetBond> bonds)
{
    juce::ValueTree tree ("Molecule");
    for (auto element : elements)
    {
        juce::ValueTree atom ("Atom");
        atom.setProperty ("element", (int) element, nullptr);
        tree.appendChild (atom, nullptr);
    }
    for (auto value : bonds)
    {
        juce::ValueTree bond ("Bond");
        bond.setProperty ("a", value.a, nullptr);
        bond.setProperty ("b", value.b, nullptr);
        bond.setProperty ("order", value.order, nullptr);
        tree.appendChild (bond, nullptr);
    }
    return tree;
}

Molecule methane()
{
    Molecule mol;
    require (mol.addAtom (Element::Carbon, {}) == 0, "Cannot create methane");
    return mol;
}

Molecule substitute (Molecule mol, Element element, int count)
{
    for (int n = 0; n < count; ++n)
    {
        const int anchor = mol.attachmentIndex (element, { 80.0f * n, 0 });
        require (anchor >= 0, "No valid carbon for substitution");
        const int index = mol.addAtom (element, { 80.0f * n, -60 }, anchor);
        require (index >= 0 && index < (int) mol.atoms().size()
                 && mol.atoms()[(size_t) index].element == element, "Added atom index changed during hydrogen rebuild");
    }
    return mol;
}

void chemistryTests()
{
    static_assert ((int) Element::Hydrogen == 5 && (int) Element::Fluorine == 6);
    auto mol = methane();
    const auto parentKey = mol.canonicalSmiles();
    require (mol.formula() == "CH4", "Methane formula changed");
    for (auto element : organic::halogenElements)
    {
        mol = substitute (mol, element, 1);
        require (mol.withoutHalogens().canonicalSmiles() == parentKey, "Substitution changed the parent seed");
    }
    require (mol.formula() == "CBrClFI", "Mixed halogen Hill formula is wrong");
    require (std::abs (mol.molecularWeight() - 273.267f) < 0.003f, "Halogen molecular weight is wrong");
    require (mol.halogenCounts() == std::array<int, 4> { 1, 1, 1, 1 }, "Mixed counts are wrong");
    require (mol.addAtom (Element::Fluorine, {}, 0) < 0, "Carbon accepted a fifth bond");
    require (mol.addAtom (Element::Carbon, {}, 1) < 0, "Halogen accepted a second bond");
    require (! mol.cycleBondOrder (0), "C-X bond became a double bond");
    Molecule restored;
    require (restored.fromValueTree (mol.toValueTree()), "Halogen state restore failed");
    require (restored.formula() == mol.formula() && restored.halogenCounts() == mol.halogenCounts(), "Halogens became another element on restore");
    require (mol.removeAtom (2) && mol.formula() == "CHBrFI", "Deleting chlorine did not restore a hydrogen");
    require (mol.removeAtom (0) && mol.heavyAtomCount() == 0, "Deleting carbon left isolated halogens");
    mol = substitute (methane(), Element::Iodine, 1);
    require (mol.removeBond (0) && mol.formula() == "CH4", "C-X bond deletion kept the halogen fragment");

    for (auto element : organic::halogenElements)
    {
        Molecule empty;
        require (empty.addAtom (element, {}) < 0 && empty.heavyAtomCount() == 0, "Halogen seeded an empty canvas");
        for (auto other : { Element::Oxygen, Element::Nitrogen, Element::Sulfur, Element::Phosphorus })
        {
            Molecule hetero;
            hetero.addAtom (other, {});
            require (hetero.addAtom (element, {}, 0) < 0, "Non-carbon accepted a halogen");
        }
    }

    const auto keep = methane().toValueTree();
    const std::vector<juce::ValueTree> invalid {
        topology ({ Element::Hydrogen }, {}),
        topology ({ (Element) 99 }, {}),
        topology ({ Element::Fluorine }, {}),
        topology ({ Element::Oxygen, Element::Fluorine }, {{ 0, 1, 1 }}),
        topology ({ Element::Carbon, Element::Chlorine }, {{ 0, 1, 2 }}),
        topology ({ Element::Carbon, Element::Bromine, Element::Carbon }, {{ 0, 1, 1 }, { 1, 2, 1 }}),
        topology ({ Element::Carbon, Element::Carbon }, {{ 0, 1, 1 }, { 1, 0, 1 }}),
        topology ({ Element::Carbon, Element::Carbon }, {}),
        topology ({ Element::Carbon, Element::Oxygen, Element::Oxygen, Element::Fluorine }, {{ 0, 1, 2 }, { 0, 2, 2 }, { 0, 3, 1 }})
    };
    for (const auto& tree : invalid)
    {
        require (restored.fromValueTree (keep), "Cannot restore test parent");
        require (! restored.fromValueTree (tree) && restored.formula() == "CH4", "Malformed graph was accepted or destroyed existing state");
    }
    int checked = 0;
    for (int i = 0; i < (int) organic::moleculePresets().size(); ++i)
    {
        Molecule original;
        if (! original.fromValueTree (organic::presetValueTree (i)))
            throw std::runtime_error (std::string ("Legacy preset rejected: ") + organic::moleculePresets()[(size_t) i].name);

        // 母体 = 剥离卤素后的无卤素骨架。它自身必须稳定（再剥离不变）：
        // 无卤素预设的母体 == 自身，含卤素预设的母体 == 去卤补氢骨架，两者统一验证。
        const Molecule parent = original.withoutHalogens();
        const juce::String parentKey = parent.canonicalSmiles();
        require (parent.withoutHalogens().canonicalSmiles() == parentKey, "Parent identity changed");

        for (auto element : organic::halogenElements)
        {
            if (original.attachmentIndex (element, {}) < 0)
                continue;
            auto variant = substitute (original, element, 1);
            require (variant.withoutHalogens().canonicalSmiles() == parentKey, "Ring/chain parent identity changed");
            require (variant.withoutHalogens().formula() == parent.formula(), "Parent hydrogen completion changed");
            require (restored.fromValueTree (variant.toValueTree()) && restored.formula() == variant.formula(), "Substituted preset did not round-trip");
            ++checked;
        }
    }
    auto branched = substitute (methane(), Element::Fluorine, 2);
    branched = substitute (branched, Element::Chlorine, 1);
    require (! branched.canonicalSmiles().contains ("F(") && ! branched.canonicalSmiles().contains ("Cl("),
             "SMILES text attached a branch to a terminal halogen");
    std::cout << "PASS chemical constraints / legacy presets / " << checked << " substituted preset round-trips\n";
}

Audio testSignal (double rate, double seconds = 1.0)
{
    const int size = (int) (rate * seconds);
    Audio audio { std::vector<float> ((size_t) size), std::vector<float> ((size_t) size) };
    for (int i = 0; i < size; ++i)
    {
        const double t = (double) i / rate;
        const float signal = t < 0.12 ? (float) (0.18 * std::sin (t * 6.28318530718 * 220.0)
            + 0.08 * std::sin (t * 6.28318530718 * 997.0) + 0.05 * std::sin (t * 6.28318530718 * 3511.0)) : 0.0f;
        audio[0][(size_t) i] = signal;
        audio[1][(size_t) i] = signal * 0.83f;
    }
    return audio;
}

Audio processEffects (const Audio& input, const std::array<int, 4>& counts, double rate, int block)
{
    organic::HalogenEffects effects;
    effects.prepare (rate);
    effects.setCounts (counts, true);
    Audio output = input;
    for (int offset = 0; offset < (int) input[0].size(); offset += block)
    {
        float* pointers[] { output[0].data() + offset, output[1].data() + offset };
        effects.process (pointers, 2, std::min (block, (int) input[0].size() - offset));
    }
    return output;
}

double difference (const Audio& a, const Audio& b)
{
    double delta = 0, reference = 0;
    for (size_t ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < a[ch].size(); ++i)
        {
            const double d = (double) a[ch][i] - b[ch][i];
            delta += d * d;
            reference += (double) a[ch][i] * a[ch][i];
        }
    return std::sqrt (delta / std::max (1.0e-12, reference));
}

void effectTests()
{
    const auto input = testSignal (48000);
    require (processEffects (input, {}, 48000, 257) == input, "Zero halogens are not transparent");
    std::array<Audio, 4> variants;
    for (int i = 0; i < 4; ++i)
    {
        std::array<int, 4> one {}, several {};
        one[(size_t) i] = 1;
        several[(size_t) i] = 4;
        variants[(size_t) i] = processEffects (input, one, 48000, 257);
        const auto stronger = processEffects (input, several, 48000, 257);
        const double single = difference (input, variants[(size_t) i]);
        const double more = difference (variants[(size_t) i], stronger);
        std::cout << "FX " << i << " single delta=" << single << " extra substitutions delta=" << more << "\n";
        require (single > 0.06, "First substitution is too subtle in the signal test");
        require (more > 0.04, "Repeated substitutions have no meaningful effect");
        require (stronger == processEffects (input, several, 48000, 1), "Effect depends on host block size");
    }
    for (size_t i = 0; i < variants.size(); ++i)
        for (size_t j = i + 1; j < variants.size(); ++j)
            require (difference (variants[i], variants[j]) > 0.05, "Two halogen effects sound numerically identical");

    require (organic::HalogenEffects::strengthForCount (0) == 0.0f, "Zero count has nonzero strength");
    for (int count = 1; count <= 8; ++count)
        require (organic::HalogenEffects::strengthForCount (count) > organic::HalogenEffects::strengthForCount (count - 1), "Count mapping is not monotonic");
    require (organic::HalogenEffects::strengthForCount (10000) == organic::HalogenEffects::strengthForCount (8), "Count safety ceiling is missing");

    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0 })
    {
        const auto signal = testSignal (rate, 0.5);
        const auto mixed = processEffects (signal, { 8, 8, 8, 8 }, rate, 511);
        for (const auto& channel : mixed)
            for (float sample : channel)
                require (std::isfinite (sample) && std::abs (sample) < 4.0f, "Mixed effects unstable at a supported sample rate");
    }

    organic::HalogenEffects effects;
    effects.prepare (48000);
    std::array<std::array<float, 257>, 2> data {};
    float* channels[] { data[0].data(), data[1].data() };
    effects.process (channels, 2, 0);
    for (int block = 0; block < 120; ++block)
    {
        effects.setCounts ({ block % 9, (block / 2) % 9, (block / 3) % 9, (block / 5) % 9 });
        for (int i = 0; i < 257; ++i)
            data[0][(size_t) i] = data[1][(size_t) i] = (float) std::sin ((block * 257 + i) * 0.17) * 0.3f;
        if (block == 5) data[0][10] = std::numeric_limits<float>::quiet_NaN();
        if (block == 10) data[1][20] = std::numeric_limits<float>::infinity();
        effects.process (channels, block % 2 + 1, 257);
        for (int ch = 0; ch < block % 2 + 1; ++ch)
            for (float value : data[(size_t) ch])
                require (std::isfinite (value), "Effect edit/invalid input poisoned a delay line");
    }
    effects.setCounts ({});
    for (int block = 0; block < 20; ++block)
    {
        data = {};
        effects.process (channels, 2, 257);
    }
    effects.setCounts ({ 1, 1, 1, 1 });
    for (int block = 0; block < 60; ++block)
    {
        data = {};
        effects.process (channels, 2, 257);
        for (const auto& channel : data)
            for (float value : channel)
                require (value == 0.0f, "Re-enabling an effect revived a frozen tail");
    }
    std::cout << "PASS four distinct effects / repeated counts / block invariance / finite extremes / cleared tails\n";
}

void apply (OrganicChemistryAudioProcessor& processor, const Molecule& mol)
{
    require (processor.setMolecularState (mol.toValueTree()), "Processor rejected a valid molecule");
    processor.applyMolecularStateToAudio();
}

std::array<float, organic::kNumBellParams> bellParameters (OrganicChemistryAudioProcessor& processor)
{
    std::array<float, organic::kNumBellParams> result;
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = processor.getBellParam ((int) i);
    return result;
}

void stateTests()
{
    auto parent = methane();
    parent.addAtom (Element::Carbon, { 62, 0 }, 0);
    OrganicChemistryAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (48000, 257);
    processor.prepareToPlay (48000, 257);
    processor.setAdsrParams (0.012f, 0.71f, 0.28f, 2.6f);
    apply (processor, parent);
    const auto original = bellParameters (processor);
    const auto originalSample = processor.getNoiseSampleView();
    juce::AudioBuffer<float> buffer (2, 257);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    std::array<float, organic::kWaveTableSize> originalWave;
    std::copy_n (processor.getOsc1WaveData(), originalWave.size(), originalWave.begin());
    auto modified = parent;
    for (auto element : organic::halogenElements)
    {
        modified = substitute (modified, element, 1);
        apply (processor, modified);
        processor.processBlock (buffer, midi);
        require (bellParameters (processor) == original, "Halogen changed Bell parent parameters or ADSR");
        require (std::equal (originalWave.begin(), originalWave.end(), processor.getOsc1WaveData()), "Halogen changed parent wave");
        const auto sample = processor.getNoiseSampleView();
        require (sample.data == originalSample.data && sample.length == originalSample.length, "Halogen changed the strike sample");
    }
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    const auto once = state;
    processor.getStateInformation (state);
    require (state == once, "Saving appended a second state");
    OrganicChemistryAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    require (restored.getHalogenCounts() == std::array<int, 4> { 1, 1, 1, 1 }, "Headless restore lost halogen effects");
    require (bellParameters (restored) == original, "Headless restore changed the parent or ADSR");
    restored.setRateAndBufferSizeDetails (48000, 257);
    restored.prepareToPlay (48000, 257);
    double energy = 0;
    for (int block = 0; block < 100; ++block)
    {
        midi.clear();
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        restored.processBlock (buffer, midi);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float value = buffer.getSample (0, i);
            require (std::isfinite (value), "Restored mixed halogens generated nonfinite audio");
            energy += value * value;
        }
    }
    require (energy > 0.001, "No audio without an editor");
    apply (processor, parent);
    require (processor.getHalogenCounts() == std::array<int, 4> {} && bellParameters (processor) == original,
             "Removing all halogens did not restore the parent");
    const auto savedParent = processor.getMolecularState();
    const auto invalid = topology ({ Element::Oxygen, Element::Iodine }, {{ 0, 1, 1 }});
    require (! processor.setMolecularState (invalid) && processor.getMolecularState().isEquivalentTo (savedParent), "Invalid state replaced the parent");
    auto legacy = parent.toValueTree();
    legacy.removeProperty ("schemaVersion", nullptr);
    auto xml = legacy.createXml()->toString();
    restored.setStateInformation (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
    require (restored.getHalogenCounts() == std::array<int, 4> {}, "Legacy molecule inherited old halogen effects");
    apply (restored, Molecule());
    restored.processBlock (buffer, midi);
    apply (restored, parent);
    midi.clear();
    restored.processBlock (buffer, midi);
    require (buffer.getMagnitude (0, buffer.getNumSamples()) == 0.0f, "Clear left old effect tails frozen");
    std::cout << "PASS parent identity / ADSR / sample identity / headless and legacy state / silence\n";
}

juce::MouseEvent pointerEvent (juce::Component& component, juce::Point<float> position, int modifiers,
                              juce::Point<float> start = {})
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
        juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &component, &component,
        juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1, position != start);
}

template<class Component>
Component* childOf (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        if (auto* result = dynamic_cast<Component*> (child))
            return result;
    return nullptr;
}

void saveSnapshot (juce::Component& component, const juce::File& file)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "Cannot create preview directory");
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), true);
    auto stream = file.createOutputStream();
    require (image.isValid() && stream != nullptr && stream->setPosition (0) && stream->truncate().wasOk()
             && juce::PNGImageFormat().writeImageToStream (image, *stream), "Cannot write native UI preview");
}

void interactionTests (bool snapshots)
{
    OrganicChemistryAudioProcessor processor;
    apply (processor, methane());
    std::unique_ptr<juce::AudioProcessorEditor> owner (processor.createEditor());
    auto* editor = dynamic_cast<OrganicChemistryAudioProcessorEditor*> (owner.get());
    require (editor != nullptr, "Wrong editor type");
    auto* bar = childOf<organic::ElementBar> (*editor);
    auto* wheel = childOf<organic::HalogenWheel> (*editor);
    auto* canvas = childOf<organic::MoleculeCanvas> (*editor);
    require (bar != nullptr && wheel != nullptr && canvas != nullptr, "Missing halogen UI components");
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    for (int width : { 410, 820, 1640 })
    {
        editor->setSize (width, juce::roundToInt ((float) width * 560.0f / 820.0f));
        editor->advanceAnimationForTesting (24);
        const auto anchor = bar->halogenBounds().getCentre();
        for (int i = 0; i < 4; ++i)
        {
            const auto before = canvas->getMolecule().toValueTree();
            bar->mouseDown (pointerEvent (*bar, anchor, left, anchor));
            require (wheel->isVisible(), "Holding X did not open the wheel");
            require (canvas->getMolecule().toValueTree().isEquivalentTo (before), "Opening a wheel added an atom");
            const auto option = wheel->optionCentre (i);
            require (editor->getLocalBounds().toFloat().contains (option) && wheel->indexAt (option) == i, "Wheel hit region is out of bounds or wrong");
            const auto onBar = bar->getLocalPoint (wheel, option);
            bar->mouseDrag (pointerEvent (*bar, onBar, left, anchor));
            if (snapshots && i == 1)
            {
                editor->advanceAnimationForTesting (8);
                saveSnapshot (*editor, juce::File::getCurrentWorkingDirectory().getChildFile ("Preview/Halogen-wheel-" + juce::String (width) + ".png"));
            }
            bar->mouseUp (pointerEvent (*bar, onBar, 0, anchor));
            require (! wheel->isVisible() && canvas->getCurrentElement() == organic::HalogenWheel::elementFor (i), "Mouse release did not select halogen");
            require (canvas->getMolecule().toValueTree().isEquivalentTo (before), "Wheel mouse-up fell through to the canvas");
        }
        const auto chosen = bar->getSelected();
        bar->mouseDown (pointerEvent (*bar, anchor, left, anchor));
        bar->mouseUp (pointerEvent (*bar, anchor, 0, anchor));
        require (! wheel->isVisible() && bar->getSelected() == chosen, "A short click accidentally chose a sector");
        bar->mouseDown (pointerEvent (*bar, anchor, left, anchor));
        wheel->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        bar->mouseUp (pointerEvent (*bar, anchor, 0, anchor));
        require (! wheel->isVisible() && bar->getSelected() == chosen, "Escape did not cancel the wheel");
        bar->mouseDown (pointerEvent (*bar, anchor, left, anchor));
        wheel->focusLost (juce::Component::focusChangedDirectly);
        require (! wheel->isVisible(), "Wheel remained open after focus loss");
    }

    editor->setSize (820, 560);
    canvas->restoreMolecule (methane().toValueTree());
    editor->advanceAnimationForTesting (80);
    bar->setSelected (Element::Fluorine);
    for (int count = 1; count <= 4; ++count)
    {
        const auto p = canvas->atomScreenPosition (0);
        canvas->mouseDown (pointerEvent (*canvas, p, left, p));
        canvas->mouseUp (pointerEvent (*canvas, p, 0, p));
        require (canvas->getMolecule().halogenCounts()[0] == count, "Continuous placement failed");
    }
    auto p = canvas->atomScreenPosition (0);
    canvas->mouseDown (pointerEvent (*canvas, p, left, p));
    canvas->mouseUp (pointerEvent (*canvas, p, 0, p));
    require (canvas->getMolecule().halogenCounts()[0] == 4, "Canvas overfilled carbon valence");
    canvas->restoreMolecule (methane().toValueTree());
    p = canvas->atomScreenPosition (0);
    canvas->mouseDown (pointerEvent (*canvas, p, left, p));
    canvas->mouseUp (pointerEvent (*canvas, { -20, -20 }, 0, p));
    require (canvas->getMolecule().formula() == "CH4", "Out-of-canvas release added an atom");
    canvas->mouseDown (pointerEvent (*canvas, p, left, p));
    canvas->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    canvas->mouseUp (pointerEvent (*canvas, p, 0, p));
    require (canvas->getMolecule().formula() == "CH4", "Cancelled placement added an atom");
    canvas->mouseDown (pointerEvent (*canvas, p, left, p));
    canvas->clearMolecule();
    canvas->mouseUp (pointerEvent (*canvas, p, 0, p));
    require (canvas->getMolecule().heavyAtomCount() == 0, "Clear left a live placement gesture");
    canvas->mouseDown (pointerEvent (*canvas, p, left, p));
    canvas->mouseUp (pointerEvent (*canvas, p, 0, p));
    require (canvas->getMolecule().heavyAtomCount() == 0, "Canvas seeded an isolated halogen");
    apply (processor, substitute (methane(), Element::Bromine, 1));
    editor->advanceAnimationForTesting (1);
    require (canvas->getMolecule().formula() == "CH3Br", "Open editor failed to follow restored state");

    if (snapshots)
    {
        Molecule example;
        for (int i = 0; i < (int) organic::moleculePresets().size(); ++i)
            if (juce::String (organic::moleculePresets()[(size_t) i].name) == "Benzene")
                example.fromValueTree (organic::presetValueTree (i));
        require (example.heavyAtomCount() > 0, "Preview parent not found");
        for (auto element : organic::halogenElements)
            example = substitute (example, element, 1);
        canvas->restoreMolecule (example.toValueTree());
        editor->advanceAnimationForTesting (180);
        saveSnapshot (*editor, juce::File::getCurrentWorkingDirectory().getChildFile ("Preview/Halogen-molecule.png"));
    }
    std::cout << "PASS radial selection / cancellation / 50-200% scale / continuous placement / open-editor restore\n";
}

void publicationStressTest()
{
    OrganicChemistryAudioProcessor processor;
    auto a = substitute (methane(), Element::Fluorine, 1);
    auto b = methane();
    b.addAtom (Element::Carbon, { 62, 0 }, 0);
    b = substitute (b, Element::Iodine, 1);
    apply (processor, a);
    processor.setRateAndBufferSizeDetails (48000, 257);
    processor.prepareToPlay (48000, 257);
    std::atomic<bool> start { false };
    std::thread publisher ([&]
    {
        while (! start.load()) std::this_thread::yield();
        for (int i = 0; i < 80; ++i)
            apply (processor, i % 2 == 0 ? a : b);
    });
    juce::AudioBuffer<float> buffer (2, 257);
    juce::MidiBuffer midi;
    start.store (true);
    bool finite = true;
    for (int block = 0; block < 180; ++block)
    {
        midi.clear();
        if (block % 10 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 48 + block % 40, 0.7f), 0);
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
    }
    publisher.join();
    require (finite, "Concurrent parent publication produced nonfinite audio");
    std::cout << "PASS concurrent molecule publication\n";
}

Audio renderInstrument (const Molecule& molecule)
{
    constexpr int rate = 48000, blockSize = 257, length = rate * 2;
    OrganicChemistryAudioProcessor processor;
    apply (processor, molecule);
    processor.setAdsrParams (0.004f, 0.8f, 0.0f, 1.5f);
    processor.setRateAndBufferSizeDetails (rate, blockSize);
    processor.prepareToPlay (rate, blockSize);
    Audio output { std::vector<float> (length), std::vector<float> (length) };
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < length; offset += blockSize)
    {
        const int count = std::min (blockSize, length - offset);
        buffer.setSize (2, count, false, false, true);
        midi.clear();
        if (offset == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        const int noteOff = rate / 3 - offset;
        if (noteOff >= 0 && noteOff < count)
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), noteOff);
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < count; ++i)
            {
                const float value = buffer.getSample (ch, i);
                require (std::isfinite (value) && std::abs (value) <= 1.0f, "Instrument output exceeded its finite safety boundary");
                output[(size_t) ch][(size_t) offset + i] = value;
            }
    }
    return output;
}

void instrumentTests (bool exportAudio)
{
    std::vector<Audio> clips;
    clips.push_back (renderInstrument (methane()));
    for (auto element : organic::halogenElements)
    {
        const auto one = renderInstrument (substitute (methane(), element, 1));
        const auto several = renderInstrument (substitute (methane(), element, 3));
        const double firstDelta = difference (clips.front(), one);
        const double nextDelta = difference (one, several);
        std::cout << organic::elementInfo (element).symbol << " Bell delta=" << firstDelta
                  << " repeated delta=" << nextDelta << '\n';
        require (firstDelta > 0.04 && nextDelta > 0.025, "Actual Bell instrument did not respond distinctly to substitutions");
        clips.push_back (one);
        clips.push_back (several);
    }
    auto mixed = methane();
    for (auto element : organic::halogenElements)
        mixed = substitute (mixed, element, 1);
    clips.push_back (renderInstrument (mixed));
    if (exportAudio)
    {
        const int clipLength = (int) clips.front()[0].size();
        juce::AudioBuffer<float> preview (2, clipLength * (int) clips.size());
        for (size_t clip = 0; clip < clips.size(); ++clip)
            for (int ch = 0; ch < 2; ++ch)
                preview.copyFrom (ch, (int) clip * clipLength, clips[clip][(size_t) ch].data(), clipLength);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile ("Preview/Halogen-audio-comparison.wav");
        require (file.getParentDirectory().createDirectory().wasOk(), "Cannot create audio preview directory");
        auto stream = file.createOutputStream();
        require (stream != nullptr && stream->setPosition (0) && stream->truncate().wasOk(), "Cannot open audio comparison output");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48000.0, 2, 24, {}, 0));
        require (writer != nullptr && writer->writeFromAudioSampleBuffer (preview, 0, preview.getNumSamples()), "Cannot write audio comparison");
    }
    std::cout << "PASS end-to-end Bell substitutions and bounded output\n";
}
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const bool snapshots = argc > 1 && juce::String (argv[1]) == "--snapshots";
        require (runLegacyBellTests() == 0, "Legacy Bell verification failed");
        chemistryTests();
        effectTests();
        stateTests();
        interactionTests (snapshots);
        publicationStressTest();
        instrumentTests (argc > 1 && juce::String (argv[1]) == "--audio-preview");
        std::cout << "HALOGEN TESTS PASSED" << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
