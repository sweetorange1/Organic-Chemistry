#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>

int main()
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
