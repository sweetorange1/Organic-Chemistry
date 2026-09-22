#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

class AudioRegressionTests
{
public:
    struct Patch
    {
        organic::WaveTable table;
        organic::SynthesisParameters params;
        int atoms = 0;
    };

    struct Measurement
    {
        double energy = 0.0, weightedEnergy = 0.0, peak = 0.0;
        int samples = 0;
        double rms() const { return std::sqrt (energy / juce::jmax (1, samples)); }
        double weightedRms() const { return std::sqrt (weightedEnergy / juce::jmax (1, samples)); }
    };

    static void require (bool condition, const char* message)
    {
        if (! condition)
            throw std::runtime_error (message);
    }

    static Patch preset (const char* name)
    {
        const auto& presets = organic::moleculePresets();
        for (size_t i = 0; i < presets.size(); ++i)
            if (juce::String (presets[i].name).equalsIgnoreCase (name))
            {
                organic::Molecule molecule;
                molecule.fromValueTree (organic::presetValueTree ((int) i));
                const auto descriptors = molecule.computeDescriptors();
                return { organic::buildWaveTable (descriptors, molecule.canonicalSmiles()),
                         organic::mapMoleculeToAudio (descriptors), descriptors.heavyAtomCount };
            }
        throw std::runtime_error (std::string ("Missing preset: ") + name);
    }

    static void prepare (OrganicChemistryAudioProcessor& processor, double rate, int blockSize)
    {
        processor.setRateAndBufferSizeDetails (rate, blockSize);
        processor.prepareToPlay (rate, blockSize);
    }

    static void publish (OrganicChemistryAudioProcessor& processor, const Patch& patch)
    {
        const int version = processor.getWaveTableVersion();
        processor.setWaveTable (patch.table);
        processor.setSynthesisParameters (patch.params);
        processor.setMoleculeEmpty (false);
        if (processor.calibrationInstance)
            return;
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
        while (processor.getWaveTableVersion() == version)
        {
            processor.processBlock (buffer, midi);
            require (juce::Time::getMillisecondCounterHiRes() < deadline, "Calibration timed out");
            juce::Thread::sleep (2);
        }
    }

    static Measurement render (OrganicChemistryAudioProcessor& processor, double rate,
                               int blockSize, int note = 60, float velocity = 0.8f,
                               int voices = 1, double duration = 1.5, bool noteOn = true)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::dsp::IIR::Filter<float> highPass[2], shelf[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            highPass[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (rate, 60.0f, 0.5f);
            shelf[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                rate, 1500.0f, 0.707f, juce::Decibels::decibelsToGain (4.0f));
        }
        Measurement result;
        const int total = (int) (duration * rate);
        for (int offset = 0; offset < total; offset += blockSize)
        {
            const int count = juce::jmin (blockSize, total - offset);
            buffer.setSize (2, count, false, false, true);
            midi.clear();
            if (offset == 0 && noteOn)
                for (int voice = 0; voice < voices; ++voice)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note + voice * 3, velocity), 0);
            processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < count; ++s)
                {
                    const float sample = buffer.getSample (ch, s);
                    const float x = processor.calibrationInstance ? std::tanh (sample) : sample;
                    require (std::isfinite (x), "Non-finite audio output");
                    const double weighted = shelf[ch].processSample (highPass[ch].processSample (x));
                    result.energy += (double) x * x;
                    result.weightedEnergy += weighted * weighted;
                    result.peak = juce::jmax (result.peak, (double) std::abs (x));
                    ++result.samples;
                }
        }
        return result;
    }

    static double db (double gain) { return 20.0 * std::log10 (juce::jmax (gain, 1.0e-12)); }

    static void run()
    {
        constexpr double rate = 48000.0;
        constexpr int blockSize = 256;
        std::vector<double> before, after;
        for (const char* name : { "Methane", "Ethanol", "Cyclohexane", "Benzene", "Toluene", "Phenol", "Naphthalene" })
        {
            auto patch = preset (name);
            OrganicChemistryAudioProcessor corrected;
            prepare (corrected, rate, blockSize);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            publish (corrected, patch);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            double oldEnergy = 0.0, newEnergy = 0.0;
            for (int note : { 48, 60, 72 })
            {
                OrganicChemistryAudioProcessor raw (true);
                auto oldPatch = patch;
                oldPatch.params[(size_t) organic::ParamId::OscLevel] = 0.30f + juce::jmin (1.0f, patch.atoms / 10.0f) * 0.35f;
                publish (raw, oldPatch);
                prepare (raw, rate, blockSize);
                prepare (corrected, rate, blockSize);
                const auto oldResult = render (raw, rate, blockSize, note);
                const auto newResult = render (corrected, rate, blockSize, note);
                oldEnergy += oldResult.weightedEnergy / oldResult.samples;
                newEnergy += newResult.weightedEnergy / newResult.samples;
                require (newResult.peak < 1.0, "Output exceeds full scale");
            }
            before.push_back (db (std::sqrt (oldEnergy / 3.0)));
            after.push_back (db (std::sqrt (newEnergy / 3.0)));
            std::cout << name << ": before=" << before.back() << " after=" << after.back()
                      << " gain=" << db (corrected.calibratedGain) << " dB, calibration=" << elapsed << " ms\n";
        }
        const auto range = [] (const auto& values) {
            return *std::max_element (values.begin(), values.end()) - *std::min_element (values.begin(), values.end());
        };
        std::cout << "Weighted level spread: " << range (before) << " -> " << range (after) << " dB\n";
        require (range (after) < 4.0, "Representative timbre loudness spread is too large");
        require (std::abs (after[3] - after[2]) < 2.0, "Benzene/cyclohexane loudness mismatch");

        const auto benzene = preset ("Benzene");
        OrganicChemistryAudioProcessor processor;
        prepare (processor, rate, blockSize);
        publish (processor, benzene);
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 512 })
                for (int voices : { 1, 4, 8 })
                {
                    prepare (processor, sr, block);
                    const auto result = render (processor, sr, block, 48, 1.0f, voices, 0.5);
                    require (result.peak < 1.0 && result.rms() > 0.0001, "Invalid polyphonic output");
                }

        prepare (processor, rate, blockSize);
        const auto quiet = render (processor, rate, blockSize, 60, 0.3f);
        prepare (processor, rate, blockSize);
        const auto loud = render (processor, rate, blockSize, 60, 0.9f);
        require (loud.rms() > quiet.rms() * 1.4, "Velocity dynamics were flattened");
        processor.setMoleculeEmpty (true);
        render (processor, rate, blockSize, 60, 0.8f, 1, 0.1, false);
        require (render (processor, rate, blockSize, 60, 0.8f, 1, 0.1, false).peak == 0.0,
                 "Empty molecule is not silent");

        // A phase-only waveform edit must not restart a sustained amplitude envelope.
        OrganicChemistryAudioProcessor voiceTest (true);
        auto plain = preset ("Methane");
        plain.params[(size_t) organic::ParamId::SustainLevel] = 0.2f;
        plain.params[(size_t) organic::ParamId::DistMix] = 0.0f;
        plain.params[(size_t) organic::ParamId::ReverbWet] = 0.0f;
        plain.params[(size_t) organic::ParamId::DelayMix] = 0.0f;
        plain.params[(size_t) organic::ParamId::ChorusMix] = 0.0f;
        plain.params[(size_t) organic::ParamId::LfoToAmp] = 0.0f;
        plain.params[(size_t) organic::ParamId::LfoToPan] = 0.0f;
        publish (voiceTest, plain);
        prepare (voiceTest, rate, blockSize);
        render (voiceTest, rate, blockSize);
        const auto sustained = render (voiceTest, rate, blockSize, 60, 0.8f, 1, 0.15, false);
        std::rotate (plain.table.samples.begin(), plain.table.samples.begin() + 100, plain.table.samples.end());
        voiceTest.setWaveTable (plain.table);
        const auto edited = render (voiceTest, rate, blockSize, 60, 0.8f, 1, 0.15, false);
        require (edited.peak < sustained.peak * 1.3, "Wave edit retriggered amplitude envelope");
        // Exercise the real asynchronous path while a note remains held.
        OrganicChemistryAudioProcessor live;
        prepare (live, rate, blockSize);
        const auto cyclohexane = preset ("Cyclohexane");
        publish (live, cyclohexane);
        render (live, rate, blockSize, 60, 0.8f, 1, 1.5);
        const auto oldSustain = render (live, rate, blockSize, 60, 0.8f, 1, 0.2, false);
        live.setWaveTable (benzene.table);
        live.setSynthesisParameters (benzene.params);
        double transitionPeak = 0.0;
        for (int block = 0; block < 80; ++block)
        {
            const auto result = render (live, rate, blockSize, 60, 0.8f, 1, 0.01, false);
            transitionPeak = juce::jmax (transitionPeak, result.peak);
            juce::Thread::sleep (2);
        }
        const auto newSustain = render (live, rate, blockSize, 60, 0.8f, 1, 0.2, false);
        std::cout << "Held-note edit peak excess: "
                  << db (transitionPeak / juce::jmax (oldSustain.peak, newSustain.peak)) << " dB\n";
        require (transitionPeak < juce::jmax (oldSustain.peak, newSustain.peak) * 2.0,
                 "Live benzene edit has an excessive level spike");

        // Rapid volume changes must remain responsive and not cancel calibration.
        auto controls = benzene.params;
        const float gainBeforeControls = live.calibratedGain;
        for (int i = 0; i < 100; ++i)
        {
            controls[(size_t) organic::ParamId::MasterLevel] = 0.1f + 0.002f * i;
            live.setSynthesisParameters (controls);
            require (live.getParam ((int) organic::ParamId::MasterLevel)
                        == controls[(size_t) organic::ParamId::MasterLevel], "Master control was delayed");
        }
        require (live.calibratedGain == gainBeforeControls, "Master control changed compensation");

        // A new parameter-only calibration must smoothly scale lingering FX tails.
        live.synth.allNotesOff (0, false);
        controls[(size_t) organic::ParamId::Drive] = 0.0f;
        controls[(size_t) organic::ParamId::DistMix] = 0.0f;
        live.setSynthesisParameters (controls);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
        bool pending = false;
        while (! pending)
        {
            {
                const juce::SpinLock::ScopedLockType lock (live.pendingTimbreLock);
                pending = live.pendingTimbreReady;
            }
            require (juce::Time::getMillisecondCounterHiRes() < deadline, "Tail calibration timed out");
            juce::Thread::sleep (2);
        }
        const float tailGain = live.timbreGain.getCurrentValue();
        live.applyPendingTimbre();
        require (live.timbreGain.isSmoothing() && live.timbreGain.getCurrentValue() == tailGain,
                 "Calibration abruptly changed a tail's gain");
        render (live, rate, blockSize, 60, 0.8f, 1, 0.2, false);
        std::cout << "Audio regression tests passed.\n";
    }
};

int main()
{
    std::cout << std::unitbuf << "Starting audio regression tests.\n";
    const juce::ScopedJuceInitialiser_GUI initialiseJuce;
    try { AudioRegressionTests::run(); }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
