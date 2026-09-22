#include "BellEngine.h"

#include "PluginProcessor.h"

namespace organic
{

namespace
{
// FNV-1a 32 位哈希：把 SMILES 变成稳定种子。
uint32_t fnv1aHash (const char* data, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= static_cast<uint8_t> (data[i]);
        hash *= 16777619u;
    }
    return hash;
}

// xorshift32：确定性伪随机。同一 SMILES 永远得到同一串值。
struct XorShift
{
    uint32_t state;
    explicit XorShift (uint32_t seed) : state (seed != 0 ? seed : 1u) {}
    uint32_t next()
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }
    float nextFloat() { return static_cast<float> (next() >> 8) / 16777216.0f; }
};
}

// ---------------------------------------------------------------------------
//  分子 → Bell 参数（非单调、确定但不可预测）
//
//  所有参数都落在「冬季钟声/键盘」好音色的共性范围内，但具体取值由
//  SMILES 哈希决定，因此：
//    · 同一分子永远同一音色（确定）
//    · 分子间变化无规律（添加/删除原子不会让音色单调变亮/变响）
// ---------------------------------------------------------------------------

std::array<float, kNumBellParams> mapMoleculeToBellParams (
    const ChemicalDescriptors& d, const juce::String& smiles)
{
    juce::ignoreUnused (d);
    auto p = defaultBellParams();

    const char* bytes = smiles.toRawUTF8();
    XorShift rng (fnv1aHash (bytes, (size_t) smiles.getNumBytesAsUTF8()));

    // --- 包络：瞬态拨弦（共性规律，sustain 固定 0）---
    p[(size_t) BellParamId::AmpAttack]  = 0.0005f + rng.nextFloat() * 0.015f;   // 0.5ms ~ 15ms
    p[(size_t) BellParamId::AmpSustain] = 0.0f;
    p[(size_t) BellParamId::AmpDecay]   = 0.8f + rng.nextFloat() * 2.5f;        // 0.8 ~ 3.3s
    p[(size_t) BellParamId::AmpRelease] = 0.6f + rng.nextFloat() * 2.0f;        // 0.6 ~ 2.6s

    // --- 声源：三个正弦层 ---
    p[(size_t) BellParamId::Osc1Level]     = 0.65f + rng.nextFloat() * 0.2f;    // 基频
    p[(size_t) BellParamId::Osc1Transpose] = (rng.nextFloat() < 0.4f) ? 12.0f : 0.0f;
    p[(size_t) BellParamId::Osc2Transpose] = (rng.nextFloat() < 0.4f) ? 24.0f : 12.0f;
    p[(size_t) BellParamId::Osc2Unison]    = 1.0f + std::floor (rng.nextFloat() * 7.0f);
    p[(size_t) BellParamId::Osc2Squeeze]   = rng.nextFloat();
    p[(size_t) BellParamId::Osc3Level]     = rng.nextFloat() * 0.35f;
    p[(size_t) BellParamId::Osc3Transpose] = (rng.nextFloat() < 0.5f) ? 24.0f : 12.0f;

    // --- 滤波 ---
    p[(size_t) BellParamId::FilterCutoff]   = 400.0f + rng.nextFloat() * 4000.0f;
    p[(size_t) BellParamId::FilterResonance] = rng.nextFloat() * 0.3f;
    p[(size_t) BellParamId::FilterKeytrack] = 1.0f;

    // --- 噪声击打层 ---
    p[(size_t) BellParamId::NoiseLevel]  = (rng.nextFloat() < 0.7f)
                                         ? 0.3f + rng.nextFloat() * 0.5f : 0.0f;
    p[(size_t) BellParamId::NoiseCutoff] = 1000.0f + rng.nextFloat() * 1000.0f;

    // --- 空间效果（共性规律的范围）---
    p[(size_t) BellParamId::ChorusMix]      = 0.05f + rng.nextFloat() * 0.25f;
    p[(size_t) BellParamId::ChorusFeedback] = 0.35f + rng.nextFloat() * 0.15f;
    p[(size_t) BellParamId::DelayMix]       = 0.15f + rng.nextFloat() * 0.3f;
    p[(size_t) BellParamId::DelayFeedback]  = 0.2f + rng.nextFloat() * 0.3f;
    p[(size_t) BellParamId::DelayTime]      = 0.3f + rng.nextFloat() * 0.5f;
    p[(size_t) BellParamId::ReverbMix]      = 0.2f + rng.nextFloat() * 0.4f;
    p[(size_t) BellParamId::ReverbDecay]    = 0.4f + rng.nextFloat() * 1.0f;
    p[(size_t) BellParamId::ReverbSize]     = 0.3f + rng.nextFloat() * 0.3f;

    // --- 失真 ---
    p[(size_t) BellParamId::DistortionDrive] = rng.nextFloat() * 0.5f;
    p[(size_t) BellParamId::DistortionMix]   = rng.nextFloat() * 0.3f;

    // 宏：osc_2 电平由 ATTACK 宏驱动（非单调哈希决定）。
    p[(size_t) BellParamId::MacroWET]       = 0.5f;
    p[(size_t) BellParamId::MacroBITCRUSH]  = 0.0f;
    p[(size_t) BellParamId::MacroDETUNE]    = 0.2f;
    p[(size_t) BellParamId::MacroATTACK]    = (rng.nextFloat() < 0.6f) ? rng.nextFloat() : 0.0f;

    return p;
}

// ---------------------------------------------------------------------------
//  近正弦波表：基频主导 + 少量低幅度谐波
// ---------------------------------------------------------------------------

WaveTable buildNearSineWave (const ChemicalDescriptors& d, const juce::String& smiles, int size)
{
    juce::ignoreUnused (d);
    WaveTable table;
    table.samples.resize (static_cast<size_t> (juce::jmax (16, size)));

    const char* bytes = smiles.toRawUTF8();
    XorShift rng (fnv1aHash (bytes, (size_t) smiles.getNumBytesAsUTF8()));

    // 谐波幅度独立随机（非单调）：偶次谐波（温暖/不对称感）可以更强，
    // 奇次谐波克制（避免刺耳）。上限比纯正弦明显，但远低于锯齿/方波。
    float h2 = rng.nextFloat() * 0.35f;    // 0 ~ -9 dB（偶次：温暖）
    float h3 = rng.nextFloat() * 0.15f;    // 0 ~ -16 dB
    float h4 = rng.nextFloat() * 0.09f;    // 0 ~ -21 dB
    float h5 = rng.nextFloat() * 0.05f;    // 0 ~ -26 dB
    float h6 = rng.nextFloat() * 0.035f;   // 0 ~ -29 dB

    // 总谐波能量约束：极端情况下整体缩放，保证波形仍"近正弦"不跑偏。
    const float total = h2 + h3 + h4 + h5 + h6;
    if (total > 0.5f)
    {
        const float scale = 0.5f / total;
        h2 *= scale; h3 *= scale; h4 *= scale; h5 *= scale; h6 *= scale;
    }

    const float p2 = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    const float p3 = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    const float p4 = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    const float p5 = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    const float p6 = rng.nextFloat() * juce::MathConstants<float>::twoPi;

    const int n = static_cast<int> (table.samples.size());
    for (int i = 0; i < n; ++i)
    {
        const float theta = juce::MathConstants<float>::twoPi
                          * static_cast<float> (i) / static_cast<float> (n);
        float v = std::sin (theta);
        v += h2 * std::sin (2.0f * theta + p2);
        v += h3 * std::sin (3.0f * theta + p3);
        v += h4 * std::sin (4.0f * theta + p4);
        v += h5 * std::sin (5.0f * theta + p5);
        v += h6 * std::sin (6.0f * theta + p6);
        table.samples[(size_t) i] = v;
    }

    // 峰值归一化到 0.95。
    float peak = 0.0f;
    for (float x : table.samples)
        peak = std::max (peak, std::fabs (x));
    if (peak > 1e-6f)
        for (float& x : table.samples)
            x *= 0.95f / peak;

    return table;
}

// ---------------------------------------------------------------------------
//  BellVoice
// ---------------------------------------------------------------------------

BellVoice::BellVoice (OrganicChemistryAudioProcessor* processor)
    : owner (processor), voiceIndex (0)
{
    patch = bellReflectionsPatch();
    for (auto& phases : oscPhases)
        phases.fill (0.0);
    lowPass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    noiseBandpass.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
}

bool BellVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<BellSound*> (sound) != nullptr;
}

void BellVoice::startNote (int midiNoteNumber, float velocity,
                           juce::SynthesiserSound*, int /*pitchWheel*/)
{
    updatePatch();

    const int transposed = midiNoteNumber + patch.voiceTranspose;
    baseFreqHz = static_cast<float> (juce::MidiMessage::getMidiNoteInHertz (transposed));

    // velocity track = 0 → 力度不敏感，所有音等响。
    juce::ignoreUnused (velocity);

    // Vital：所有振荡器 PHASE 180°（起始相位 0.5）。
    for (auto& phases : oscPhases)
        phases.fill (0.5);

    // 触发噪声击打采样（noise_attack_1 一次性播放）。
    samplePos = 0.0;

    envState = EnvState::attack;
    env2State = EnvState::attack;
    env = 0.0f;
    env2 = 0.0f;
    envSamples = 0;
    env2Samples = 0;

    lowPass.reset();
}

void BellVoice::stopNote (float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        envState = EnvState::release;
        env2State = EnvState::release;
        envReleaseFrom = env;   // 从当前值平滑释放，避免突变 click
        env2ReleaseFrom = env2;
        envSamples = 0;
        env2Samples = 0;
    }
    else
    {
        clearCurrentNote();
        env = 0.0f;
        env2 = 0.0f;
    }
}

void BellVoice::updatePatch()
{
    patch = bellReflectionsPatch();
    if (owner == nullptr)
        return;

    std::array<float, kNumBellParams> params;
    for (int i = 0; i < kNumBellParams; ++i)
        params[(size_t) i] = owner->getBellParam (i);
    patch = applyBellParams (patch, params);
}

void BellVoice::updateFilter (float cutoffHz, float sampleRate)
{
    // filterResonance 0..1 → Q 0.707..8（Vital resonance 语义）。
    const float q = 0.707f + patch.filterResonance * 7.3f;
    lowPass.setCutoffFrequency (cutoffHz);
    lowPass.setResonance (1.0f / juce::jmax (0.1f, q));
    juce::ignoreUnused (sampleRate);
}

float BellVoice::renderOscillator (int index, double sampleRate, double pitchRatio)
{
    const BellOsc& o = patch.osc[(size_t) index];
    if (! o.on)
        return 0.0f;

    // 频率比：转调（半音）× DETUNE 宏音高漂移。
    const double transposeRatio = std::pow (2.0, o.transpose / 12.0);
    const double cyclesPerSample = (double) baseFreqHz * transposeRatio * pitchRatio / sampleRate;

    float sum = 0.0f;
    const int voices = juce::jlimit (1, 5, o.unisonVoices);

    for (int v = 0; v < voices; ++v)
    {
        // unison 失谐（Vital：detune_range 2 × unison_detune 1.92% ≈ ±3.84 音分）。
        double detuneCents = 0.0;
        if (voices > 1)
        {
            const double t = (double) v / (double) (voices - 1) * 2.0 - 1.0;   // -1..1
            detuneCents = t * 2.0 * 1.92;
        }
        const double ratio = std::pow (2.0, detuneCents / 1200.0);

        double phase = oscPhases[(size_t) index][(size_t) v];
        phase += cyclesPerSample * ratio;
        if (phase >= 1.0) phase -= 1.0;
        oscPhases[(size_t) index][(size_t) v] = phase;

        // squeeze 相位失真。
        const float distortedPhase = SqueezePhaseDistortion::process ((float) phase, o.squeezeAmount);

        if (index == 0)
        {
            // osc_1 用分子波表（近正弦，由分子编辑决定）。
            const float* wave = owner->getOsc1WaveData();
            const int size = organic::kWaveTableSize;
            const float pos = distortedPhase * (float) size;
            const int i0 = (int) pos % size;
            const float frac = pos - std::floor (pos);
            const int i1 = (i0 + 1) % size;
            sum += wave[i0] * (1.0f - frac) + wave[i1] * frac;
        }
        else
        {
            // osc_2 / osc_3 用纯正弦。
            sum += std::sin (distortedPhase * juce::MathConstants<float>::twoPi);
        }
    }

    // unison 归一化（不含 level，level 由调用方乘入）。
    return sum / std::sqrt ((float) voices);
}

void BellVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                 int startSample, int numSamples)
{
    if (baseFreqHz <= 0.0f || owner == nullptr)
        return;

    const double sr = getSampleRate();
    if (sr <= 0.0)
        return;

    const int numChannels = outputBuffer.getNumChannels();

    // 滤波器准备（采样率变化时重新准备）。
    if ((float) sr != preparedSr)
    {
        preparedSr = (float) sr;
        juce::dsp::ProcessSpec spec { sr, 512, 1 };
        lowPass.prepare (spec);
        noiseBandpass.prepare (spec);
        noiseBandpass.setCutoffFrequency (patch.sampleCutoffHz);
        noiseBandpass.setResonance (juce::jlimit (0.25f, 20.0f, 1.0f / (0.707f + patch.sampleResonance * 4.0f)));
    }

    const float attackStep  = (float) (1.0 / (sr * juce::jmax (0.001f, patch.ampAttack)));
    const float decayTau    = (float) (patch.ampDecay * sr / 5.0);
    const float releaseTau  = (float) (patch.ampRelease * sr / 5.0);
    const float env2DecayTau  = (float) (patch.env2Decay * sr / 5.0);
    const float env2ReleaseTau= (float) (patch.env2Release * sr / 5.0);
    const double sampleStep = (owner->getNoiseSampleRate() > 0.0)
        ? owner->getNoiseSampleRate() / sr : 1.0;   // 采样率换算

    // DETUNE 宏：慢速三角波音高漂移（Vital lfo_1 0.2Hz → voice_tune）。
    const double detuneDepthCents = 0.225 * patch.detuneDepth * 100.0;   // 最多 ±22.5 音分
    const double lfoInc = juce::MathConstants<double>::twoPi * 0.2 / sr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // --- 幅度包络（钟形） ---
        switch (envState)
        {
            case EnvState::attack:
                env += attackStep;
                if (env >= 1.0f) { env = 1.0f; envState = EnvState::decay; envSamples = 0; }
                break;
            case EnvState::decay:
                env = patch.ampSustain + (1.0f - patch.ampSustain) * std::exp (-envSamples / decayTau);
                if (env <= patch.ampSustain + 0.0001f) { env = patch.ampSustain; envReleaseFrom = env; envState = EnvState::release; envSamples = 0; }
                break;
            case EnvState::release:
                env = envReleaseFrom * std::exp (-envSamples / releaseTau);
                if (env <= 0.0001f) { env = 0.0f; }
                break;
        }

        // --- ENV2 → 滤波器截止 ---
        switch (env2State)
        {
            case EnvState::attack:
                env2 += attackStep;
                if (env2 >= 1.0f) { env2 = 1.0f; env2State = EnvState::decay; env2Samples = 0; }
                break;
            case EnvState::decay:
                env2 = std::exp (-env2Samples / env2DecayTau);
                if (env2 <= 0.0001f) { env2 = 0.0f; env2ReleaseFrom = env2; env2State = EnvState::release; env2Samples = 0; }
                break;
            case EnvState::release:
                env2 = env2ReleaseFrom * std::exp (-env2Samples / env2ReleaseTau);
                if (env2 <= 0.0001f) env2 = 0.0f;
                break;
        }

        if (env <= 0.0f && envState == EnvState::release)
        {
            clearCurrentNote();
            break;
        }

        // DETUNE 宏：慢速三角波音高漂移（Vital LFO1 = Triangle）。
        double tri = detuneLfoPhase / juce::MathConstants<double>::twoPi;   // [0,1)
        tri = tri < 0.5 ? (tri * 4.0 - 1.0) : (3.0 - tri * 4.0);            // -1..1
        const double detuneCents = detuneDepthCents * tri;
        detuneLfoPhase += lfoInc;
        if (detuneLfoPhase >= juce::MathConstants<double>::twoPi) detuneLfoPhase -= juce::MathConstants<double>::twoPi;
        const double detuneRatio = std::pow (2.0, detuneCents / 1200.0);

        // --- 三个振荡器 ---
        float direct = 0.0f;    // 绕过滤波器（osc_1, osc_3）
        float filtered = 0.0f;  // 经过滤波器（osc_2）

        for (int i = 0; i < 3; ++i)
        {
            // 有效电平：osc_2 基础 0，由 ATTACK 宏驱动。
            float level = patch.osc[(size_t) i].level;
            if (i == 1)
                level = owner->getMacro (3) * 0.31f;

            // Vital kQuadratic：实际增益 = level²（levelOutput 中 amp * amp）。
            const float s = renderOscillator (i, sr, detuneRatio) * level * level;

            if (patch.osc[(size_t) i].throughFilter)
                filtered += s;
            else
                direct += s;
        }

        // --- 噪声击打采样（noise_attack_1 → FILTER2 带通）---
        float noiseOut = 0.0f;
        if (patch.sampleOn)
        {
            const float* data = owner->getNoiseSampleData();
            const int len = owner->getNoiseSampleLength();
            if (data != nullptr && len > 1 && samplePos < (double) (len - 1))
            {
                const int idx = (int) samplePos;
                const float frac = (float) (samplePos - (double) idx);
                const float raw = data[idx] + (data[idx + 1] - data[idx]) * frac;
                const float bandpassed = noiseBandpass.processSample (0, raw);
                noiseOut = bandpassed * (patch.sampleLevel * patch.sampleLevel) * 2.0f;
                samplePos += sampleStep;
            }
        }
        direct += noiseOut;   // 采样绕过 filter_1 直接进效果链

        // 低通滤波（filter_1，含 keytrack + ENV2 调制）。
        const float keytrackCents = patch.filterKeytrack
            * (float) (12.0 * std::log2 (juce::jmax (20.0, baseFreqHz) / 261.63));
        const float cutoffHz = patch.filterCutoff * std::pow (2.0f, keytrackCents / 12.0f)
                             * std::pow (2.0f, patch.env2ToFilter * env2 * 4.0f / 12.0f);
        updateFilter (cutoffHz, (float) sr);
        const float filteredOut = lowPass.processSample (0, filtered);

        const float out = (direct + filteredOut) * env * 0.5f;

        if (numChannels > 1)
        {
            outputBuffer.addSample (0, startSample + sample, out);
            outputBuffer.addSample (1, startSample + sample, out);
        }
        else
        {
            outputBuffer.addSample (0, startSample + sample, out);
        }

        ++envSamples;
        ++env2Samples;
    }
}

} // namespace organic
