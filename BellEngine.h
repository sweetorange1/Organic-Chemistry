#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

#include "BellWave.h"
#include "MoleculeModel.h"
#include "MoleculeAudioMapper.h"

class OrganicChemistryAudioProcessor;   // 全局前置声明（BellVoice 仅持有指针）

// ============================================================
//  BellEngine — "BELL Reflections" 音色引擎
//
//  从 Vital 预设（作者 jazen，Peaceful Winter 风格）反向解析得到：
//
//    核心是三个正弦波振荡器构成的 lo-fi 钟声：
//      OSC1  正弦 基频       -3 dB   直接进效果链（绕过滤波器）
//      OSC2  正弦 +12 八度   0(宏)   squeeze 相位失真 + 低通滤波 + 5 复音
//      OSC3  正弦 +12 八度  -16 dB   直接进效果链
//
//  包络是经典钟形（快起音 + 指数衰减到 0 + 长释放），全局 +12 八度、
//  力度不敏感（velocity track = 0）。效果链：多段压缩 → 合唱 → 延迟 →
//  混响 → 降采样失真。四个宏：WET / BITCRUSH / DETUNE / ATTACK。
//
//  此引擎完全脱离"分子驱动音色"：波形固定为正弦，参数固定为复刻值，
//  只有四个宏可由宿主 / 编辑器调节。
// ============================================================

namespace organic
{

// ---------------------------------------------------------------------------
//  SqueezePhaseDistortion — 相位挤压失真（Vital kSqueeze 的标量移植）
// ---------------------------------------------------------------------------
struct SqueezePhaseDistortion
{
    static float process (float phase, float amount) noexcept
    {
        if (amount <= 1e-4f)
            return phase;

        // Vital: distortion = amount * 2 * 0.95 + (1 - 0.95)
        const float d = amount * 1.9f + 0.05f;

        float p = phase - std::floor (phase);        // [0, 1)
        const float sign = p >= 0.5f ? -1.0f : 1.0f;
        float a = p >= 0.5f ? 1.0f - p : p;          // |p| in [0, 0.5]

        const float pivot = d * 0.25f;
        float ap = (a <= pivot)
            ? a / d
            : 0.5f - (0.5f - a) / (2.0f - d);

        const float result = sign > 0.0f ? ap : 1.0f - ap;
        return result - std::floor (result);
    }
};

// ---------------------------------------------------------------------------
//  DownsampleDistortion — 降采样（sample & hold）
// ---------------------------------------------------------------------------
struct DownsampleDistortion
{
    void reset() noexcept { counter = 0; held = 0.0f; }

    float process (float x, float drive) noexcept
    {
        if (drive <= 1e-4f)
            return x;

        const int period = juce::jlimit (1, 128, (int) std::lround (1.0f + drive * 63.0f));
        if (++counter >= period)
        {
            counter = 0;
            held = x;
        }
        return held;
    }

    int counter = 0;
    float held = 0.0f;
};

// ---------------------------------------------------------------------------
//  CompressorBand — 单段向下压缩 + makeup gain
// ---------------------------------------------------------------------------
struct CompressorBand
{
    void reset() noexcept { envelope = 1.0f; }

    void prepare (double sampleRate) noexcept
    {
        attackCoeff  = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.005)));
        releaseCoeff = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.120)));
    }

    float process (float x, float thresholdDb, float ratio, float makeupDb) noexcept
    {
        const float absX = std::fabs (x);
        const float levelDb = absX > 1e-7f ? juce::Decibels::gainToDecibels (absX) : -120.0f;

        float gainDb = 0.0f;
        if (levelDb > thresholdDb)
        {
            const float over = levelDb - thresholdDb;
            gainDb = -over * (1.0f - juce::jlimit (0.0f, 1.0f, ratio));
        }

        const float target = juce::Decibels::decibelsToGain (gainDb + makeupDb);
        envelope += (target - envelope) * (target < envelope ? attackCoeff : releaseCoeff);
        return x * envelope;
    }

    float attackCoeff = 1.0f;
    float releaseCoeff = 0.1f;
    float envelope = 1.0f;
};

// ---------------------------------------------------------------------------
//  MultibandCompressor — 三频段压缩器
// ---------------------------------------------------------------------------
struct MultibandCompressor
{
    void reset() noexcept
    {
        lowBand.reset();
        midBand.reset();
        highBand.reset();
        lowState = highState = 0.0f;
    }

    void prepare (double sampleRate) noexcept
    {
        const float crossoverLow  = 300.0f;
        const float crossoverHigh = 3000.0f;
        lowSplit  = (float) (1.0 - std::exp (-juce::MathConstants<float>::twoPi * crossoverLow  / sampleRate));
        highSplit = (float) (1.0 - std::exp (-juce::MathConstants<float>::twoPi * crossoverHigh / sampleRate));
        lowBand.prepare (sampleRate);
        midBand.prepare (sampleRate);
        highBand.prepare (sampleRate);
    }

    float process (float x,
                   float lowThresh,  float lowRatio,  float lowMakeup,
                   float midThresh,  float midRatio,  float midMakeup,
                   float highThresh, float highRatio, float highMakeup) noexcept
    {
        lowState += (x - lowState) * lowSplit;            // 低 = 低通
        const float low = lowState;
        const float high = x - low;                        // 高 = 高通
        highState += (high - highState) * highSplit;       // 中 = 带通
        const float mid = highState;
        const float top = high - highState;                // 高频

        const float cLow  = lowBand.process (low, lowThresh, lowRatio, lowMakeup);
        const float cMid  = midBand.process (mid, midThresh, midRatio, midMakeup);
        const float cHigh = highBand.process (top, highThresh, highRatio, highMakeup);
        return cLow + cMid + cHigh;
    }

    float lowSplit = 0.0f, highSplit = 0.0f;
    float lowState = 0.0f, highState = 0.0f;
    CompressorBand lowBand, midBand, highBand;
};

// ---------------------------------------------------------------------------
//  BellPatch — 一个 Bell 音色的全部参数（固定值，复刻 Vital）
// ---------------------------------------------------------------------------
struct BellOsc
{
    bool  on = true;
    float level = 0.0f;
    int   transpose = 0;         // 半音
    int   unisonVoices = 1;
    float unisonDetune = 0.0f;   // 0..1
    float squeezeAmount = 0.0f;
    bool  throughFilter = false;
};

struct BellPatch
{
    std::array<BellOsc, 3> osc;

    // 包络时间单位：秒。Vital 以四次方编码（0.1495⁴ ≈ 0.5ms），此处已换算。
    float ampAttack  = 0.0005f;   // 0.1495⁴ → 瞬态音头
    float ampDecay   = 1.1f;      // 1.026⁴
    float ampSustain = 0.0f;
    float ampRelease = 2.6f;      // 1.273⁴

    float env2Attack  = 0.0005f;
    float env2Decay   = 0.019f;   // 0.37⁴
    float env2Sustain = 0.0f;
    float env2Release = 0.0008f;  // 0.1665⁴
    float env2ToFilter = 0.31f;

    float filterCutoff    = 400.0f;   // Hz
    float filterResonance = 0.0f;
    float filterKeytrack  = 1.0f;

    float compressorMix  = 0.005f;
    float compressorThresholdLow  = -35.0f;
    float compressorRatioLow      = 0.8f;
    float compressorMakeupLow     = 16.3f;
    float compressorThresholdMid  = -36.0f;
    float compressorRatioMid      = 0.8f;
    float compressorMakeupMid     = 11.7f;
    float compressorThresholdHigh = -35.0f;
    float compressorRatioHigh     = 0.8f;
    float compressorMakeupHigh    = 16.3f;

    float distortionDrive = 0.35f;
    float distortionMix   = 0.0f;

    float chorusMix      = 0.105f;
    float chorusFeedback = 0.4f;
    float chorusRate     = 0.8f;

    float delayMix      = 0.253f;
    float delayFeedback = 0.44f;
    float delayTime     = 0.5f;

    float reverbMix   = 0.195f;
    float reverbDecay = 0.48f;
    float reverbSize  = 0.295f;

    int   voiceTranspose = 12;
    float velocityTrack  = 0.0f;

    // DETUNE 宏深度（慢速音高漂移，0..1）。
    float detuneDepth = 0.25f;

    // 采样器（噪声击打层）
    bool  sampleOn       = false;
    float sampleLevel    = 0.707f;
    float sampleCutoffHz = 1079.0f;
    float sampleResonance = 0.204f;

    // 宏初始位置（0=WET 1=BITCRUSH 2=DETUNE 3=ATTACK）。
    std::array<float, 4> macroDefaults { 0.5f, 0.15f, 0.25f, 0.3f };
};

inline BellPatch bellReflectionsPatch()
{
    BellPatch p;
    p.osc[0] = { true, 0.707f, 0,  1, 0.0f,    0.0f,   false };
    p.osc[1] = { true, 0.0f,   12, 5, 0.0192f, 0.695f, true  };
    p.osc[2] = { true, 0.152f, 12, 1, 0.0f,    0.0f,   false };

    // 采样器（noise_attack_1）：钟声的金属击打瞬态，经 FILTER2 带通。
    p.sampleOn          = true;
    p.sampleLevel       = 0.707f;   // Vital kQuadratic：实际增益 = level²
    p.sampleCutoffHz    = 1079.0f;  // filter_2 cutoff MIDI 84.5 → Hz
    p.sampleResonance   = 0.204f;

    // 宏初始位置（按用户 Vital 界面截图估读）。
    p.macroDefaults = { 0.5f, 0.15f, 0.25f, 0.3f };
    return p;
}

// ---------------------------------------------------------------------------
//  Bell 参数集 — 暴露到 Test 面板的全部可调参数
//
//  覆盖三个振荡器、包络、滤波器、噪声击打层、各效果器与四个宏。
//  参数顺序同时决定 TestPanel 的显示顺序。
// ---------------------------------------------------------------------------
enum class BellParamId : int
{
    // Oscillator
    Osc1Level = 0,
    Osc1Transpose,
    Osc2Transpose,
    Osc2Squeeze,
    Osc2Unison,
    Osc3Level,
    Osc3Transpose,
    // Envelope
    AmpAttack,
    AmpDecay,
    AmpSustain,
    AmpRelease,
    Env2ToFilter,
    // Filter
    FilterCutoff,
    FilterResonance,
    FilterKeytrack,
    // Noise（采样器击打层）
    NoiseLevel,
    NoiseCutoff,
    // Distortion
    DistortionDrive,
    DistortionMix,
    // Compressor
    CompressorMix,
    // Chorus
    ChorusMix,
    ChorusFeedback,
    // Delay
    DelayMix,
    DelayFeedback,
    DelayTime,
    // Reverb
    ReverbMix,
    ReverbDecay,
    ReverbSize,
    // Macro
    MacroWET,
    MacroBITCRUSH,
    MacroDETUNE,
    MacroATTACK,
    Count
};

constexpr int kNumBellParams = static_cast<int> (BellParamId::Count);

struct BellParamDef
{
    const char* name;
    const char* category;
    float minValue;
    float maxValue;
    float step;
    float defaultValue;
};

inline const BellParamDef& bellParamDef (int index)
{
    static const BellParamDef table[] = {
        // Oscillator
        { "OSC1 level",      "OSCILLATOR",  0.0f, 1.0f,  0.01f, 0.707f },
        { "OSC1 pitch",      "OSCILLATOR", -24.0f, 24.0f, 1.0f, 0.0f },
        { "OSC2 pitch",      "OSCILLATOR", -24.0f, 24.0f, 1.0f, 12.0f },
        { "OSC2 squeeze",    "OSCILLATOR",  0.0f, 1.0f,  0.01f, 0.695f },
        { "OSC2 unison",     "OSCILLATOR",  1.0f, 5.0f,  1.0f, 5.0f },
        { "OSC3 level",      "OSCILLATOR",  0.0f, 1.0f,  0.01f, 0.152f },
        { "OSC3 pitch",      "OSCILLATOR", -24.0f, 24.0f, 1.0f, 12.0f },
        // Envelope
        { "Attack (s)",      "ENVELOPE",    0.0005f, 0.5f,  0.0005f, 0.0005f },
        { "Decay (s)",       "ENVELOPE",    0.01f, 6.0f,   0.01f, 1.1f },
        { "Sustain",         "ENVELOPE",    0.0f, 1.0f,   0.01f, 0.0f },
        { "Release (s)",     "ENVELOPE",    0.01f, 8.0f,   0.01f, 2.6f },
        { "Env2 -> filter",  "ENVELOPE",    0.0f, 1.0f,   0.01f, 0.31f },
        // Filter
        { "Filter cutoff",   "FILTER",     100.0f, 8000.0f, 10.0f, 400.0f },
        { "Filter res",      "FILTER",      0.0f, 1.0f,   0.01f, 0.0f },
        { "Filter keytrack", "FILTER",      0.0f, 1.0f,   0.01f, 1.0f },
        // Noise
        { "Noise level",     "NOISE",       0.0f, 1.0f,   0.01f, 0.707f },
        { "Noise cutoff",    "NOISE",      200.0f, 6000.0f, 10.0f, 1079.0f },
        // Distortion
        { "Drive",           "DISTORTION",  0.0f, 1.0f,   0.01f, 0.35f },
        { "Dist mix",        "DISTORTION",  0.0f, 1.0f,   0.01f, 0.0f },
        // Compressor
        { "Comp mix",        "COMPRESSOR",  0.0f, 1.0f,   0.01f, 0.005f },
        // Chorus
        { "Chorus mix",      "CHORUS",      0.0f, 0.6f,   0.01f, 0.105f },
        { "Chorus feedback", "CHORUS",      0.0f, 0.9f,   0.01f, 0.4f },
        // Delay
        { "Delay mix",       "DELAY",       0.0f, 0.7f,   0.01f, 0.253f },
        { "Delay feedback",  "DELAY",       0.0f, 0.95f,  0.01f, 0.44f },
        { "Delay time (s)",  "DELAY",       0.01f, 2.0f,   0.01f, 0.5f },
        // Reverb
        { "Reverb mix",      "REVERB",      0.0f, 0.6f,   0.01f, 0.195f },
        { "Reverb decay",    "REVERB",      0.0f, 1.0f,   0.01f, 0.48f },
        { "Reverb size",     "REVERB",      0.0f, 1.0f,   0.01f, 0.295f },
        // Macro
        { "WET",             "MACRO",       0.0f, 1.0f,   0.01f, 0.5f },
        { "BITCRUSH",        "MACRO",       0.0f, 1.0f,   0.01f, 0.15f },
        { "DETUNE",          "MACRO",       0.0f, 1.0f,   0.01f, 0.25f },
        { "ATTACK",          "MACRO",       0.0f, 1.0f,   0.01f, 0.3f },
    };
    return table[index];
}

/** 默认 Bell 参数集（对应 bellReflectionsPatch）。 */
inline std::array<float, kNumBellParams> defaultBellParams()
{
    std::array<float, kNumBellParams> params;
    for (int i = 0; i < kNumBellParams; ++i)
        params[(size_t) i] = bellParamDef (i).defaultValue;
    return params;
}

/** SMILES → 32 位稳定哈希（FNV-1a），用于从采样库等确定性选择。 */
inline uint32_t hashSmiles (const juce::String& smiles)
{
    uint32_t hash = 2166136261u;
    const char* data = smiles.toRawUTF8();
    const size_t length = (size_t) smiles.getNumBytesAsUTF8();
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= static_cast<uint8_t> (data[i]);
        hash *= 16777619u;
    }
    return hash;
}

/** 分子描述符 + SMILES → Bell 参数集。
    非单调、确定但不可预测：用 SMILES 哈希作为确定性随机源，所有参数
    落在「冬季钟声/键盘」好音色的共性范围内。同一分子永远同一音色。 */
std::array<float, kNumBellParams> mapMoleculeToBellParams (
    const ChemicalDescriptors& d, const juce::String& smiles);

/** 生成近正弦波表：基频主导 + 少量低幅度谐波（SMILES 哈希决定），
    谐波结构落在好音色范围内（不是任意复杂波形）。 */
WaveTable buildNearSineWave (const ChemicalDescriptors& d,
                             const juce::String& smiles, int size = kWaveTableSize);

/** 用参数数组覆盖 BellPatch 的可调字段。 */
inline BellPatch applyBellParams (const BellPatch& base,
                                  const std::array<float, kNumBellParams>& p)
{
    BellPatch patch = base;
    patch.osc[0].level       = p[(size_t) BellParamId::Osc1Level];
    patch.osc[0].transpose   = (int) p[(size_t) BellParamId::Osc1Transpose];
    patch.osc[1].transpose   = (int) p[(size_t) BellParamId::Osc2Transpose];
    patch.osc[1].squeezeAmount = p[(size_t) BellParamId::Osc2Squeeze];
    patch.osc[1].unisonVoices  = (int) p[(size_t) BellParamId::Osc2Unison];
    patch.osc[2].level       = p[(size_t) BellParamId::Osc3Level];
    patch.osc[2].transpose   = (int) p[(size_t) BellParamId::Osc3Transpose];

    patch.ampAttack  = p[(size_t) BellParamId::AmpAttack];
    patch.ampDecay   = p[(size_t) BellParamId::AmpDecay];
    patch.ampSustain = p[(size_t) BellParamId::AmpSustain];
    patch.ampRelease = p[(size_t) BellParamId::AmpRelease];
    patch.env2ToFilter = p[(size_t) BellParamId::Env2ToFilter];

    patch.filterCutoff    = p[(size_t) BellParamId::FilterCutoff];
    patch.filterResonance = p[(size_t) BellParamId::FilterResonance];
    patch.filterKeytrack  = p[(size_t) BellParamId::FilterKeytrack];

    patch.sampleLevel    = p[(size_t) BellParamId::NoiseLevel];
    patch.sampleCutoffHz = p[(size_t) BellParamId::NoiseCutoff];

    patch.distortionDrive = p[(size_t) BellParamId::DistortionDrive];
    patch.distortionMix   = p[(size_t) BellParamId::DistortionMix];
    patch.compressorMix   = p[(size_t) BellParamId::CompressorMix];
    patch.chorusMix       = p[(size_t) BellParamId::ChorusMix];
    patch.chorusFeedback  = p[(size_t) BellParamId::ChorusFeedback];
    patch.delayMix        = p[(size_t) BellParamId::DelayMix];
    patch.delayFeedback   = p[(size_t) BellParamId::DelayFeedback];
    patch.delayTime       = p[(size_t) BellParamId::DelayTime];
    patch.reverbMix       = p[(size_t) BellParamId::ReverbMix];
    patch.reverbDecay     = p[(size_t) BellParamId::ReverbDecay];
    patch.reverbSize      = p[(size_t) BellParamId::ReverbSize];

    patch.detuneDepth = p[(size_t) BellParamId::MacroDETUNE];
    patch.macroDefaults[(size_t) 0] = p[(size_t) BellParamId::MacroWET];
    patch.macroDefaults[(size_t) 1] = p[(size_t) BellParamId::MacroBITCRUSH];
    patch.macroDefaults[(size_t) 2] = p[(size_t) BellParamId::MacroDETUNE];
    patch.macroDefaults[(size_t) 3] = p[(size_t) BellParamId::MacroATTACK];
    return patch;
}

// ---------------------------------------------------------------------------
//  BellVoice
// ---------------------------------------------------------------------------
class BellVoice final : public juce::SynthesiserVoice
{
public:
    explicit BellVoice (OrganicChemistryAudioProcessor* owner);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int /*pitchWheel*/) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

private:
    void updatePatch();
    void updateFilter (float cutoffHz, float sampleRate);
    float renderOscillator (int index, double sampleRate, double pitchRatio);

    OrganicChemistryAudioProcessor* owner = nullptr;
    int voiceIndex = 0;

    BellPatch patch;

    std::array<std::array<double, 5>, 3> oscPhases {};
    double baseFreqHz = 261.63;
    double detuneLfoPhase = 0.0;

    enum class EnvState { attack, decay, release };
    EnvState envState = EnvState::release;
    EnvState env2State = EnvState::release;
    float env = 0.0f;
    float env2 = 0.0f;
    float envReleaseFrom = 0.0f;   // noteOff 时锁存当前包络值，作为释放起点
    float env2ReleaseFrom = 0.0f;
    int envSamples = 0;
    int env2Samples = 0;

    // 二阶低通（osc_2 路由经过）
    juce::dsp::StateVariableTPTFilter<float> lowPass;

    // 噪声击打采样（noise_attack_1，经 FILTER2 带通）。
    juce::dsp::StateVariableTPTFilter<float> noiseBandpass;
    double samplePos = 0.0;     // 采样播放位置（noteOn 重置为 0）
    float preparedSr = 0.0f;    // 滤波器已准备的采样率

    juce::Random random;
};

class BellSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

} // namespace organic
