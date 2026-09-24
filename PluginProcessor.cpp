#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "network/UpdateChecker.h"

#include <algorithm>
#include <cmath>

namespace
{
// ============================================================
//  WaveTableSound — 合成器"音色"定义
//  当前所有 MIDI 音符 / 通道都适用；后续可在此做键盘分区、力度分层等。
// ============================================================
class WaveTableSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// ============================================================
//  LFO 波形
//
//  shape 在四种波形之间连续插值：
//      0 正弦 --- 1 三角 --- 2 方波 --- 3 阶梯（采样保持感）
//
//  关键设计：**完全由相位决定，不含任何内部状态**。全局 LFO 需要被多个
//  voice 同时读取，若采样保持依赖各自的随机数发生器，各 voice 会算出不同
//  的值，"全局"就不成立了。这里用一张固定的伪随机阶梯表按相位索引，任何
//  地方用同一个相位算出的结果都一致。
// ============================================================
inline float lfoShapeValue (double phase, float shape) noexcept
{
    const double twoPi = juce::MathConstants<double>::twoPi;

    float p = static_cast<float> (phase / twoPi);
    p -= std::floor (p);                       // 归一到 [0,1)

    const float sine   = std::sin (static_cast<float> (phase));
    const float tri    = 1.0f - 4.0f * std::abs (p - 0.5f);
    const float square = (p < 0.5f) ? 1.0f : -1.0f;

    // 8 级固定伪随机阶梯，听感等同采样保持但无状态。
    static const float steps[8] = {
         0.62f, -0.31f,  0.88f, -0.74f,
         0.17f,  0.95f, -0.53f, -0.09f
    };
    const float stair = steps[juce::jlimit (0, 7, (int) (p * 8.0f))];

    const float s = juce::jlimit (0.0f, 3.0f, shape);

    if (s <= 1.0f)  return sine   + (tri    - sine)   * s;
    if (s <= 2.0f)  return tri    + (square - tri)    * (s - 1.0f);
    return               square + (stair  - square) * (s - 2.0f);
}

// ============================================================
//  WaveTableVoice — 单个复音的 wavetable 发声体
//
//  相位累加后对单周期 wavetable 做线性插值查表，波形形状由分子决定、
//  完全不受预设波形限制。内置线性 attack（5ms）/ release（100ms）包络
//  避免 note on/off 瞬间的爆音。
// ============================================================
class WaveTableVoice final : public juce::SynthesiserVoice
{
public:
    WaveTableVoice (OrganicChemistryAudioProcessor* p, int voiceIndexIn)
        : owner (p), voiceIndex (voiceIndexIn) {}

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<WaveTableSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        currentAngle = 0.0;
        subAngle     = 0.0;

        noteVelocity = juce::jlimit (0.0f, 1.0f, velocity);

        // --- 每音随机源 ---
        //
        // 现代合成器让声音"活"起来的第一要素：**没有两个音是完全一样的**。
        // 真实乐器每次发声都受材料、力度、腔体状态影响而略有不同，纯数字
        // 合成之所以听着死板，正是因为同一个音符每次输出逐样本相同。
        // 这里在每次 note on 时抽三个随机偏移（音高 / 截止 / 声像），量由
        // Organic 分类下的三个参数控制。
        randPitchUnit  = random.nextFloat() * 2.0f - 1.0f;
        randFilterUnit = random.nextFloat() * 2.0f - 1.0f;
        randPanUnit    = random.nextFloat() * 2.0f - 1.0f;

        // 慢漂移：每个 voice 有各自的相位与略微不同的速率，因此多音齐奏时
        // 各声部各自缓慢游移，而不是整体同步摆动（同步摆动听起来仍然是
        // 机器）。这是模拟合成器"温度漂移"的数字对应物。
        driftPhase = random.nextDouble() * juce::MathConstants<double>::twoPi;
        driftRateJitter = 0.75f + random.nextFloat() * 0.5f;   // 0.75x ~ 1.25x

        // LFO2 与 LFO1 淡入都从音符起点开始计时。
        lfo2Phase = 0.0;
        noteAgeSamples = 0;

        baseFreqHz = static_cast<float> (juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber));

        if (owner != nullptr)
            owner->reportNoteFrequency (baseFreqHz);

        // --- 目标相位增量（含失谐与每音随机音高）---
        const float detune = owner != nullptr
            ? owner->getParam ((int) organic::ParamId::OscDetune) : 0.0f;
        const float randCents = owner != nullptr
            ? owner->getParam ((int) organic::ParamId::RandPitch) : 0.0f;

        const double detuneRatio = 1.0 + detune * ((double) voiceIndex / 7.0 - 0.5);
        const double randRatio   = std::pow (2.0, (double) (randPitchUnit * randCents) / 1200.0);

        const double cyclesPerSample = (double) baseFreqHz / getSampleRate();
        angleDeltaTarget = cyclesPerSample * detuneRatio * randRatio
                         * juce::MathConstants<double>::twoPi;

        // Glide：从上一个音的音高滑上来。第一次发声没有前音，直接落位。
        const float glideSec = owner != nullptr
            ? owner->getParam ((int) organic::ParamId::GlideSec) : 0.0f;

        if (glideSec > 0.001f && angleDeltaBase > 0.0)
            angleDeltaBase = angleDeltaBase;    // 保留上一个音的音高作为起点
        else
            angleDeltaBase = angleDeltaTarget;

        // ADSR：从 0 线性爬升（attack）→ 衰减到 sustain → 释放。
        envelope = 0.0f;
        envelopeState = EnvelopeState::attack;

        modEnvelope = 0.0f;
        modEnvState = EnvelopeState::attack;

        // 滤波器与噪声状态清零，避免上一个音的残留能量泄进新音。
        voiceFilter.reset();
        noiseLpState = 0.0f;
        cutoffOctaveSmoothed = 0.0f;
        cutoffPrimed = false;

        updateEnvelopeRates();

        lastWaveVersion = owner != nullptr ? owner->getWaveTableVersion() : 0;
        if (owner != nullptr)
            std::copy_n (owner->getWaveTableData(), currentTable.size(), currentTable.begin());
        previousTable = currentTable;
        waveBlend.reset (getSampleRate(), 0.020);
        waveBlend.setCurrentAndTargetValue (1.0f);
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            envelopeState = EnvelopeState::release;
            modEnvState   = EnvelopeState::release;
        }
        else
        {
            clearCurrentNote();
            angleDeltaTarget = 0.0;
            angleDeltaBase   = 0.0;
            envelope = 0.0f;
        }
    }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (angleDeltaTarget == 0.0 || owner == nullptr)
            return;

        const int tableSize = owner->getWaveTableSize();
        const double twoPi = juce::MathConstants<double>::twoPi;
        const float  sr = static_cast<float> (getSampleRate());

        // 每块读一次的参数（避免逐样本原子读取）。
        updateEnvelopeRates();

        auto P = [this] (organic::ParamId id) { return owner->getParam ((int) id); };

        const float oscLevel = P (organic::ParamId::OscLevel);
        const float subLevel = P (organic::ParamId::SubLevel);
        const float warpBase = P (organic::ParamId::WaveWarp);
        const float spread   = P (organic::ParamId::OscSpread);
        const float sustain  = juce::jlimit (0.0f, 1.0f, P (organic::ParamId::SustainLevel));
        const float modSus   = juce::jlimit (0.0f, 1.0f, P (organic::ParamId::ModEnvSustain));
        const float envCurve = P (organic::ParamId::EnvCurve);
        const float velSens  = P (organic::ParamId::VelSens);

        const float noiseLevel  = P (organic::ParamId::NoiseLevel);
        const float noiseColour = P (organic::ParamId::NoiseColour);
        const float glideSec    = P (organic::ParamId::GlideSec);

        const float filterType = P (organic::ParamId::FilterType);
        const float filterFreq = P (organic::ParamId::FilterFreq);
        const float filterRes  = P (organic::ParamId::FilterRes);
        const float filterEnv  = P (organic::ParamId::FilterEnvAmt);
        const float keyTrk     = P (organic::ParamId::FilterKeyTrk);
        const float velTrk     = P (organic::ParamId::FilterVelTrk);
        const float filtDrive  = P (organic::ParamId::FilterDrive);

        const float me2Filter = P (organic::ParamId::ModEnvToFilter);
        const float me2Pitch  = P (organic::ParamId::ModEnvToPitch);
        const float me2Warp   = P (organic::ParamId::ModEnvToWarp);

        const float lfo1Rate   = P (organic::ParamId::LfoRate);
        const float lfo1Shape  = P (organic::ParamId::LfoShape);
        const float lfo1Fade   = P (organic::ParamId::LfoFadeSec);
        const float lfo1Filter = P (organic::ParamId::LfoToFilter);
        const float lfo1Pitch  = P (organic::ParamId::LfoToPitch);

        const float lfo2Rate   = P (organic::ParamId::Lfo2Rate);
        const float lfo2Shape  = P (organic::ParamId::Lfo2Shape);
        const float lfo2Warp   = P (organic::ParamId::Lfo2ToWarp);
        const float lfo2Filter = P (organic::ParamId::Lfo2ToFilter);

        const float randFilterOct = P (organic::ParamId::RandFilter);
        const float randPanAmt    = P (organic::ParamId::RandPan);
        const float driftRate     = P (organic::ParamId::DriftRate);
        const float driftDepth    = P (organic::ParamId::DriftDepth);

        // 力度灵敏度：0 时所有音等响，1 时完全跟随力度。
        const float velGain = (1.0f - velSens) + velSens * noteVelocity;
        const float outLevel = oscLevel * velGain;

        // 键跟踪：以中央 C（261.63 Hz）为参考，keyTrk = 1 时截止频率与音高
        // 1:1 同步（高音亮、低音暗，音色在整个键盘上保持一致），= 0 时截止
        // 频率固定（高音会越弹越闷）。这正是 Serum / Vital 的 "Key Trk"。
        const float keyOct = keyTrk * std::log2 (juce::jmax (20.0f, baseFreqHz) / 261.63f);
        const float velOct = velTrk * 2.0f * (noteVelocity - 0.5f);
        const float randOct = randFilterUnit * randFilterOct;

        // 声像：unison 展开 + 每音随机偏移。
        const float panPos = juce::jlimit (-1.0f, 1.0f,
              ((float) voiceIndex / 7.0f - 0.5f) * 2.0f * spread
            + randPanUnit * randPanAmt);
        const float panAngle = (panPos + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float gainL = std::cos (panAngle);
        const float gainR = std::sin (panAngle);

        const int numChannels = outputBuffer.getNumChannels();

        // 每样本推进量。
        const double lfo1Inc  = twoPi * (double) lfo1Rate / (double) sr;
        const double lfo2Inc  = twoPi * (double) lfo2Rate / (double) sr;
        const double driftInc = twoPi * (double) (driftRate * driftRateJitter) / (double) sr;

        // 本块的 LFO1 相位从处理器块首相位起算。所有 voice 与全局段都从同一
        // 个起点、同一速率推进，因此算出的 LFO1 序列完全一致（"全局 LFO"
        // 的语义得以成立）。
        double lfo1Phase = owner->getLfoBlockStartPhase();

        const float fadeSamples = juce::jmax (1.0f, lfo1Fade * sr);
        const float glideCoeff = (glideSec > 0.001f)
            ? (1.0f - std::exp (-1.0f / (glideSec * sr)))
            : 1.0f;

        // 噪声一阶低通系数：colour = 0 极暗（约 200 Hz），= 1 明亮（约 12 kHz）。
        const float noiseCut = 200.0f * std::pow (60.0f, noiseColour);
        const float noiseCoeff = juce::jlimit (0.002f, 0.99f,
            1.0f - std::exp (-juce::MathConstants<float>::twoPi * noiseCut / sr));

        // 波形切换不重触发 ADSR；连续编辑从当前混合波形继续过渡。
        const int currentWaveVersion = owner->getWaveTableVersion();
        if (currentWaveVersion != lastWaveVersion)
        {
            lastWaveVersion = currentWaveVersion;
            const float blend = waveBlend.getCurrentValue();
            for (size_t i = 0; i < currentTable.size(); ++i)
                previousTable[i] += (currentTable[i] - previousTable[i]) * blend;
            std::copy_n (owner->getWaveTableData(), currentTable.size(), currentTable.begin());
            waveBlend.setCurrentAndTargetValue (0.0f);
            waveBlend.setTargetValue (1.0f);
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // --- 幅度 ADSR ---
            advanceEnvelope (envelope, envelopeState, sustain,
                             attackStep, decayStep, releaseStep);

            if (envelopeState == EnvelopeState::release && envelope <= 0.0f)
            {
                clearCurrentNote();
                angleDeltaTarget = 0.0;
                envelope = 0.0f;
                break;
            }

            // --- 调制包络 ENV2（独立于幅度包络，可指派到滤波/音高/warp）---
            advanceEnvelope (modEnvelope, modEnvState, modSus,
                             modAttackStep, modDecayStep, modReleaseStep);
            const float modEnv = juce::jlimit (0.0f, 1.0f, modEnvelope);

            // --- 调制源 ---
            const float fade = juce::jmin (1.0f, (float) noteAgeSamples / fadeSamples);
            const float lfo1 = lfoShapeValue (lfo1Phase, lfo1Shape) * fade;
            const float lfo2 = lfoShapeValue (lfo2Phase, lfo2Shape);
            const float drift = static_cast<float> (std::sin (driftPhase));

            // --- 音高调制 ---
            const float semis = me2Pitch * modEnv
                              + (lfo1Pitch * lfo1) / 100.0f
                              + driftDepth * 0.30f * drift;

            angleDeltaBase += (angleDeltaTarget - angleDeltaBase) * glideCoeff;
            const double angleDelta = angleDeltaBase
                                    * std::pow (2.0, (double) semis / 12.0);

            // --- 滤波器截止（对数域相加，再一阶平滑避免逐块跳变）---
            const float targetOct = filterEnv * envelope
                                  + me2Filter * modEnv
                                  + lfo1Filter * 2.0f * lfo1
                                  + lfo2Filter * 2.0f * lfo2
                                  + driftDepth * 1.2f * drift
                                  + keyOct + velOct + randOct;

            if (! cutoffPrimed)
            {
                cutoffOctaveSmoothed = targetOct;
                cutoffPrimed = true;
            }
            cutoffOctaveSmoothed += (targetOct - cutoffOctaveSmoothed) * 0.02f;

            const float cutoff = juce::jlimit (25.0f, sr * 0.47f,
                                    filterFreq * std::pow (2.0f, cutoffOctaveSmoothed));
            voiceFilter.setCoefficients (cutoff, filterRes, sr);

            // --- 主振荡器：相位畸变 + wavetable 线性插值查表 ---
            const float warp = juce::jlimit (0.0f, 1.0f,
                  warpBase + me2Warp * modEnv + lfo2Warp * (lfo2 * 0.5f + 0.5f));

            float phaseNorm = static_cast<float> (currentAngle / twoPi);
            if (phaseNorm >= 1.0f)
                phaseNorm -= 1.0f;

            if (warp > 0.0001f)
                phaseNorm = std::pow (phaseNorm, 1.0f + warp * 2.0f);

            const float pos = phaseNorm * static_cast<float> (tableSize);
            const int i0 = juce::jlimit (0, tableSize - 1, static_cast<int> (pos));
            const float frac = pos - static_cast<float> (i0);
            const int i1 = (i0 + 1 >= tableSize) ? 0 : (i0 + 1);

            float wav = currentTable[(size_t) i0] * (1.0f - frac) + currentTable[(size_t) i1] * frac;
            const float oldWav = previousTable[(size_t) i0] * (1.0f - frac) + previousTable[(size_t) i1] * frac;
            wav = oldWav + (wav - oldWav) * waveBlend.getNextValue();

            // --- 子振荡器：低八度正弦，按比例混入 ---
            if (subLevel > 0.0001f)
            {
                wav += static_cast<float> (std::sin (subAngle)) * subLevel;
                wav *= 1.0f / (1.0f + subLevel);   // 归一，避免叠加溢出
            }

            // --- 噪声层：滤波白噪，模拟吹气 / 弓毛 / 材料摩擦声 ---
            if (noiseLevel > 0.0001f)
            {
                const float white = random.nextFloat() * 2.0f - 1.0f;
                noiseLpState += (white - noiseLpState) * noiseCoeff;
                wav += noiseLpState * noiseLevel;
            }

            // --- 滤波器前的驱动（软饱和，谐振自激时也不会炸）---
            if (filtDrive > 0.0001f)
            {
                const float g = 1.0f + filtDrive * 8.0f;
                wav = std::tanh (wav * g) / (1.0f + filtDrive * 1.5f);
            }

            wav = voiceFilter.process (wav, filterType);

            // 包络曲线：curve = 0 线性，= 1 强指数。真实乐器的衰减是指数的，
            // 线性衰减听起来"电子"。
            const float shapedEnv = (envCurve > 0.001f)
                ? std::pow (juce::jmax (0.0f, envelope), 1.0f + envCurve * 2.5f)
                : envelope;

            const float currentSample = wav * outLevel * shapedEnv;

            if (numChannels > 1)
            {
                outputBuffer.addSample (0, startSample + sample, currentSample * gainL);
                outputBuffer.addSample (1, startSample + sample, currentSample * gainR);
            }
            else
            {
                outputBuffer.addSample (0, startSample + sample, currentSample);
            }

            // --- 相位推进 ---
            currentAngle += angleDelta;
            if (currentAngle >= twoPi)
                currentAngle -= twoPi;

            subAngle += angleDelta * 0.5;
            if (subAngle >= twoPi)
                subAngle -= twoPi;

            lfo1Phase += lfo1Inc;
            if (lfo1Phase >= twoPi) lfo1Phase -= twoPi;

            lfo2Phase += lfo2Inc;
            if (lfo2Phase >= twoPi) lfo2Phase -= twoPi;

            driftPhase += driftInc;
            if (driftPhase >= twoPi) driftPhase -= twoPi;

            ++noteAgeSamples;
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

private:
    enum class EnvelopeState { attack, decay, sustain, release };

    /** 推进一个 ADSR 状态机一步。幅度包络与调制包络共用这套逻辑。 */
    static void advanceEnvelope (float& env, EnvelopeState& state, float sustain,
                                 float aStep, float dStep, float rStep) noexcept
    {
        switch (state)
        {
            case EnvelopeState::attack:
                env += aStep;
                if (env >= 1.0f) { env = 1.0f; state = EnvelopeState::decay; }
                break;

            case EnvelopeState::decay:
                env -= dStep;
                if (env <= sustain) { env = sustain; state = EnvelopeState::sustain; }
                break;

            case EnvelopeState::sustain:
                // 持续段跟随 sustain 参数缓慢移动，避免用户拖动滑杆时跳变。
                env += (sustain - env) * 0.0005f;
                break;

            case EnvelopeState::release:
                env -= rStep;
                if (env < 0.0f) env = 0.0f;
                break;
        }
    }

    /** 从当前参数刷新两条包络的每样本步长。 */
    void updateEnvelopeRates()
    {
        const double sr = getSampleRate();
        if (sr <= 0.0 || owner == nullptr)
            return;

        auto step = [sr] (float seconds)
        {
            return static_cast<float> (1.0 / (sr * juce::jmax (0.0001f, seconds)));
        };

        attackStep  = step (owner->getParam ((int) organic::ParamId::AttackSec));
        decayStep   = step (owner->getParam ((int) organic::ParamId::DecaySec));
        releaseStep = step (owner->getParam ((int) organic::ParamId::ReleaseSec));

        modAttackStep  = step (owner->getParam ((int) organic::ParamId::ModEnvAttack));
        modDecayStep   = step (owner->getParam ((int) organic::ParamId::ModEnvDecay));
        modReleaseStep = step (owner->getParam ((int) organic::ParamId::ModEnvRelease));
    }

    OrganicChemistryAudioProcessor* owner = nullptr;
    int voiceIndex = 0;   // 该 voice 在合成器中的序号，用于失谐 / 声像分布

    // --- 振荡器 ---
    double currentAngle     = 0.0;
    double angleDeltaBase   = 0.0;   // 当前音高（glide 会朝 target 逼近）
    double angleDeltaTarget = 0.0;   // 目标音高
    double subAngle         = 0.0;
    float  baseFreqHz       = 261.63f;
    float  noteVelocity     = 1.0f;

    // --- 包络 ---
    EnvelopeState envelopeState = EnvelopeState::sustain;
    float envelope     = 0.0f;
    float attackStep   = 0.0f;
    float decayStep    = 0.0f;
    float releaseStep  = 0.0f;

    EnvelopeState modEnvState = EnvelopeState::sustain;
    float modEnvelope     = 0.0f;
    float modAttackStep   = 0.0f;
    float modDecayStep    = 0.0f;
    float modReleaseStep  = 0.0f;

    // --- 调制源 ---
    double lfo2Phase   = 0.0;
    double driftPhase  = 0.0;
    float  driftRateJitter = 1.0f;
    juce::int64 noteAgeSamples = 0;

    // 每音随机（note on 时抽一次，整个音符期间保持不变）
    float randPitchUnit  = 0.0f;
    float randFilterUnit = 0.0f;
    float randPanUnit    = 0.0f;

    // --- 每 voice 滤波器 ---
    MorphFilter voiceFilter;
    float cutoffOctaveSmoothed = 0.0f;
    bool  cutoffPrimed = false;

    // --- 噪声层 ---
    float noiseLpState = 0.0f;

    juce::Random random;

    std::array<float, organic::kWaveTableSize> currentTable {}, previousTable {};
    juce::LinearSmoothedValue<float> waveBlend;
    int lastWaveVersion = 0;
};
}

// ============================================================
//  Processor 实现
// ============================================================

class OrganicChemistryAudioProcessor::LoudnessCalibrationThread final : public juce::Thread
{
public:
    explicit LoudnessCalibrationThread (OrganicChemistryAudioProcessor& p)
        : juce::Thread ("Timbre loudness calibration"), owner (p)
    {
        startThread (juce::Thread::Priority::low);
    }

    ~LoudnessCalibrationThread() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (-1);
    }

    void request (const WaveTableBuffer& table, const organic::SynthesisParameters& params)
    {
        const juce::ScopedLock lock (requestLock);
        auto calibrationParams = params;
        calibrationParams[(size_t) organic::ParamId::MasterLevel] = 1.0f;
        calibrationParams[(size_t) organic::ParamId::OscLevel] = 0.5f;
        if (requested.revision != 0 && requested.table == table && requested.params == calibrationParams)
            return;
        requested.table = table;
        requested.params = calibrationParams;
        requested.revision = revision.fetch_add (1) + 1;
        notify();
    }

    bool isCurrent (unsigned int value) const noexcept { return revision.load() == value; }

private:
    void run() override
    {
        unsigned int completed = 0;
        while (! threadShouldExit())
        {
            TimbreSnapshot job;
            {
                const juce::ScopedLock lock (requestLock);
                job = requested;
            }

            if (job.revision == completed)
            {
                wait (100);
                continue;
            }

            completed = job.revision;
            if (! measure (job) || threadShouldExit() || ! isCurrent (job.revision))
                continue;

            const juce::SpinLock::ScopedLockType lock (owner.pendingTimbreLock);
            owner.pendingTimbre = job;
            owner.pendingTimbreReady = true;
        }
    }

    bool measure (TimbreSnapshot& job)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        constexpr int sampleCount = 72000;
        double energy = 0.0;
        double peak = 0.0;
        int measuredSamples = 0;

        auto params = job.params;
        // 固定演奏力度；用户的两个音量旋钮不参与抵消。
        params[(size_t) organic::ParamId::MasterLevel] = 1.0f;
        params[(size_t) organic::ParamId::OscLevel] = 0.5f;
        params[(size_t) organic::ParamId::VelSens] = 1.0f;
        params[(size_t) organic::ParamId::RandPitch] = 0.0f;
        params[(size_t) organic::ParamId::RandFilter] = 0.0f;
        params[(size_t) organic::ParamId::RandPan] = 0.0f;
        params[(size_t) organic::ParamId::DriftDepth] = 0.0f;
        params[(size_t) organic::ParamId::GlideSec] = 0.0f;

        // 三个参考音覆盖低、中、高音区；复用实际 DSP，包括失真和空间效果。
        for (int note : { 48, 60, 72 })
        {
            OrganicChemistryAudioProcessor probe (true);
            probe.waveTables[0] = job.table;
            for (int i = 0; i < organic::kNumParams; ++i)
                probe.targetParams[(size_t) i].store (params[(size_t) i]);
            probe.setMoleculeEmpty (false);
            probe.setRateAndBufferSizeDetails (sampleRate, blockSize);
            probe.prepareToPlay (sampleRate, blockSize);

            juce::dsp::IIR::Filter<float> highPass[2], shelf[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                highPass[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (
                    sampleRate, 60.0f, 0.5f);
                shelf[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate, 1500.0f, 0.707f, juce::Decibels::decibelsToGain (4.0f));
            }

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            for (int offset = 0; offset < sampleCount; offset += blockSize)
            {
                if (threadShouldExit() || ! isCurrent (job.revision))
                    return false;
                const int count = juce::jmin (blockSize, sampleCount - offset);
                buffer.setSize (2, count, false, false, true);
                midi.clear();
                if (offset == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
                probe.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < count; ++s)
                    {
                        const float x = buffer.getSample (ch, s);
                        if (! std::isfinite (x))
                            return false;
                        peak = juce::jmax (peak, (double) std::abs (x));
                        const double weighted = shelf[ch].processSample (highPass[ch].processSample (x));
                        energy += weighted * weighted;
                        ++measuredSamples;
                    }
            }
        }

        // 感知加权 RMS，不是实时 AGC；同一音色的增益不随力度或尾音变化。
        const double rms = std::sqrt (energy / juce::jmax (1, measuredSamples));
        if (rms < 1.0e-5 || ! std::isfinite (rms))
            job.gain = 1.0f;
        else
            job.gain = (float) juce::jmin (juce::jlimit (0.063, 4.0, 0.07 / rms),
                                          0.8 / juce::jmax (peak, 1.0e-5));
        return true;
    }

    OrganicChemistryAudioProcessor& owner;
    juce::CriticalSection requestLock;
    TimbreSnapshot requested;
    std::atomic<unsigned int> revision { 0 };
};

OrganicChemistryAudioProcessor::OrganicChemistryAudioProcessor()
    : OrganicChemistryAudioProcessor (false)
{
}

OrganicChemistryAudioProcessor::OrganicChemistryAudioProcessor (bool isCalibrationInstance)
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
    ), calibrationInstance (isCalibrationInstance)
{
    // 默认 wavetable：正弦（首个分子生成波形之前也能出声）。
    for (auto& table : waveTables)
    {
        for (int i = 0; i < organic::kWaveTableSize; ++i)
        {
            const float theta = juce::MathConstants<float>::twoPi
                              * static_cast<float> (i)
                              / static_cast<float> (organic::kWaveTableSize);
            table[(size_t) i] = std::sin (theta) * 0.95f;
        }
    }

    // 初始化参数目标值为各参数的默认值（原子数组需逐项赋值）。
    for (int i = 0; i < organic::kNumParams; ++i)
        targetParams[(size_t) i].store (organic::paramDef (i).defaultValue,
                                        std::memory_order_relaxed);

    constexpr int kNumVoices = 8;
    // Bell 模式：完全脱离分子，使用三个正弦振荡器的 BellVoice。
    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new organic::BellVoice (this));

    synth.addSound (new organic::BellSound());
    previewTable = waveTables[0];
    timbreGain.setCurrentAndTargetValue (1.0f);
    // Bell 音色固定，无需后台响度标定（保留 LoudnessCalibrationThread 供未来分子模式）。

    // 初始化 Bell 参数为复刻默认值。
    for (int i = 0; i < organic::kNumBellParams; ++i)
        bellParams[(size_t) i].store (organic::bellParamDef (i).defaultValue,
                                      std::memory_order_relaxed);

    // 初始化 ADSR 参数树（宿主自动化 / MIDI CC / 持久化）。
    apvts = std::make_unique<juce::AudioProcessorValueTreeState> (
        *this, nullptr, "OrganicChemistryParams", createParameterLayout());
    adsrParams[0] = dynamic_cast<juce::AudioParameterFloat*> (apvts->getParameter ("attack"));
    adsrParams[1] = dynamic_cast<juce::AudioParameterFloat*> (apvts->getParameter ("decay"));
    adsrParams[2] = dynamic_cast<juce::AudioParameterFloat*> (apvts->getParameter ("sustain"));
    adsrParams[3] = dynamic_cast<juce::AudioParameterFloat*> (apvts->getParameter ("release"));
    apvts->addParameterListener ("attack",  this);
    apvts->addParameterListener ("decay",   this);
    apvts->addParameterListener ("sustain", this);
    apvts->addParameterListener ("release", this);

    // 加载噪声击打采样库（noise 目录下的瞬态采样）。
    loadNoiseSamples();

    // 分子波表默认 = osc_1 的复刻波表（近正弦）。
    moleculeWave = organic::kOsc1Wave;

    // 启动后延迟 5 秒异步检查一次更新（进程级去重，失败静默，仅在有新版本时弹窗）。
    if (! calibrationInstance)
    {
        juce::Timer::callAfterDelay (5000, []
        {
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
            const juce::String arch = "arm64";
#elif defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
            const juce::String arch = "x64";
#else
            const juce::String arch = "x86";
#endif
#if JUCE_WINDOWS
            const juce::String platform = "win-" + arch;
#elif JUCE_MAC
            const juce::String platform = "mac-" + arch;
#elif JUCE_LINUX
            const juce::String platform = "linux-" + arch;
#else
            const juce::String platform = "unknown";
#endif
            organic::network::CheckForUpdatesAsync (
                "organic-chemistry",
                juce::String (JucePlugin_VersionString),
                platform,
                [] (const organic::network::UpdateInfo& info)
                {
                    if (info.has_update)
                        organic::network::ShowUpdateDialog (info);
                });
        });
    }
}

// ---------------------------------------------------------------------------
//  噪声击打采样库
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::loadNoiseSamples()
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    // 先试开发环境绝对路径，再试相对插件目录。
    juce::File dir ("I:/Organic Chemistry/noise");
    if (! dir.isDirectory())
        dir = juce::File::getCurrentWorkingDirectory().getChildFile ("noise");
    if (! dir.isDirectory())
        return;

    const auto files = dir.findChildFiles (juce::File::findFiles, false);
    for (const auto& file : files)
    {
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext != ".wav" && ext != ".aif" && ext != ".aiff")
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
            continue;

        const int numChannels = (int) reader->numChannels;
        const int numSamples  = (int) reader->lengthInSamples;
        if (numChannels <= 0 || numSamples <= 0)
            continue;

        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        reader->read (&buffer, 0, numSamples, 0, true, true);

        NoiseSample sample;
        sample.sampleRate = reader->sampleRate;
        sample.data.resize ((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += buffer.getSample (ch, i);
            sample.data[(size_t) i] = sum / (float) numChannels;
        }
        noiseLibrary.push_back (std::move (sample));
    }
}

const float* OrganicChemistryAudioProcessor::getNoiseSampleData() const noexcept
{
    const int idx = activeNoiseIndex.load (std::memory_order_relaxed);
    if (idx < 0 || idx >= (int) noiseLibrary.size())
        return nullptr;
    return noiseLibrary[(size_t) idx].data.data();
}

int OrganicChemistryAudioProcessor::getNoiseSampleLength() const noexcept
{
    const int idx = activeNoiseIndex.load (std::memory_order_relaxed);
    if (idx < 0 || idx >= (int) noiseLibrary.size())
        return 0;
    return (int) noiseLibrary[(size_t) idx].data.size();
}

double OrganicChemistryAudioProcessor::getNoiseSampleRate() const noexcept
{
    const int idx = activeNoiseIndex.load (std::memory_order_relaxed);
    if (idx < 0 || idx >= (int) noiseLibrary.size())
        return 44100.0;
    return noiseLibrary[(size_t) idx].sampleRate;
}

void OrganicChemistryAudioProcessor::setNoiseSampleIndex (int index)
{
    if (noiseLibrary.empty())
        return;
    const int n = (int) noiseLibrary.size();
    activeNoiseIndex.store (juce::jlimit (0, n - 1, index), std::memory_order_relaxed);
}

OrganicChemistryAudioProcessor::~OrganicChemistryAudioProcessor()
{
    loudnessCalibration.reset();
}

// ---------------------------------------------------------------------------
//  生命周期
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.allNotesOff (0, false);
    synth.setCurrentPlaybackSampleRate (sampleRate);
    timbreGain.reset (sampleRate, 0.05);
    timbreGain.setCurrentAndTargetValue (calibratedGain);
    fadeState = FadeState::idle;
    fadeGain = 1.0f;
    chorusLfoPhase = 0.0;

    // --- 效果器 prepare ---
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    lowCutFilter.prepare (spec);
    lowCutFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowCutFilter.setResonance (1.0f / std::sqrt (2.0f));   // 12 dB/oct, flat passband

    highCutFilter.prepare (spec);
    highCutFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    highCutFilter.setResonance (1.0f / std::sqrt (2.0f));  // 12 dB/oct, flat passband

    // 共振峰滤波器：清空积分器状态。
    formantL.reset();
    formantR.reset();

    // 梳状共振体：最低跟踪到约 30 Hz，所以留 40 ms 缓冲绰绰有余。
    combLineL.prepare (spec);
    combLineR.prepare (spec);
    const int combMaxDelay = (int) (sampleRate * 0.05);
    combLineL.setMaximumDelayInSamples (combMaxDelay);
    combLineR.setMaximumDelayInSamples (combMaxDelay);
    combDampStateL = 0.0f;
    combDampStateR = 0.0f;

    lfoPhase = 0.0;
    lfoBlockStartPhase = 0.0;

    // 空分子静音门：12 ms 平滑，足够短到"删完最后一个原子立刻安静"，
    // 又足够长到不产生 click。
    silenceGate.reset (sampleRate, 0.012);
    silenceGate.setCurrentAndTargetValue (
        moleculeEmpty.load() || (! calibrationInstance && ! timbreReady) ? 0.0f : 1.0f);
    voicesKilledForSilence = false;

    reverb.prepare (spec);

    // 延迟线预留最长 1.5 秒（映射规则 delayTimeSec 上限为 1.2s，留余量）
    delayLineL.prepare (spec);
    delayLineR.prepare (spec);
    const int maxDelaySamples = (int) (sampleRate * 1.5);
    delayLineL.setMaximumDelayInSamples (maxDelaySamples);
    delayLineR.setMaximumDelayInSamples (maxDelaySamples);

    // 合唱延迟线：4 声部，基础延迟约 12ms + 深度余量，总缓冲 50ms 足够。
    const int chorusMaxDelay = (int) (sampleRate * 0.05);
    for (int i = 0; i < 4; ++i)
    {
        chorusLineL[i].prepare (spec);
        chorusLineR[i].prepare (spec);
        chorusLineL[i].setMaximumDelayInSamples (chorusMaxDelay);
        chorusLineR[i].setMaximumDelayInSamples (chorusMaxDelay);
    }

    // --- 平滑器：约 100 ms 的线性过渡，避免参数突变爆音 ---
    const double ramp = 0.10;
    for (int i = 0; i < organic::kNumParams; ++i)
    {
        paramSmoothers[(size_t) i].reset (sampleRate, ramp);
        // 立即载入当前目标值，避免从默认值斜坡到目标
        paramSmoothers[(size_t) i].setCurrentAndTargetValue (
            targetParams[(size_t) i].load());
    }

    // 分子变化弱 fade：约 15ms 淡出 + 20ms 淡入，足够短以不打断演奏、足够
    // 长以消除波形切换瞬间的电流声。
    fadeDipStep    = static_cast<float> (1.0 / (sampleRate * 0.015));
    fadeRecoverStep = static_cast<float> (1.0 / (sampleRate * 0.020));

    // Bell 效果链：多段压缩器 + 降采样。
    bellCompressor.prepare (sampleRate);
    bellCompressor.reset();
    bellDownsample[0].reset();
    bellDownsample[1].reset();
}

void OrganicChemistryAudioProcessor::releaseResources()
{
    synth.allNotesOff (0, true);
}

// ---------------------------------------------------------------------------
//  Bell 音色宏
// ---------------------------------------------------------------------------

float OrganicChemistryAudioProcessor::getBellParam (int index) const noexcept
{
    if (index < 0 || index >= organic::kNumBellParams)
        return 0.0f;
    return bellParams[(size_t) index].load (std::memory_order_relaxed);
}

void OrganicChemistryAudioProcessor::setBellParam (int index, float value)
{
    if (index < 0 || index >= organic::kNumBellParams)
        return;
    const auto& def = organic::bellParamDef (index);
    bellParams[(size_t) index].store (
        juce::jlimit (def.minValue, def.maxValue, value), std::memory_order_relaxed);
}

void OrganicChemistryAudioProcessor::setBellParams (const std::array<float, organic::kNumBellParams>& p)
{
    for (int i = 0; i < organic::kNumBellParams; ++i)
        setBellParam (i, p[(size_t) i]);
}

void OrganicChemistryAudioProcessor::applyMolecularStateToAudio()
{
    // 从保存的分子拓扑重建分子（无界面加载工程时也能映射出音色）。
    organic::Molecule mol;
    {
        const juce::ValueTree state = getMolecularState();
        if (state.isValid() && state.hasType ("Molecule"))
            mol.fromValueTree (state);
    }

    const auto descriptors = mol.computeDescriptors();
    const auto smiles      = mol.canonicalSmiles();

    // 分子波表（近正弦）。
    setMoleculeWave (organic::buildNearSineWave (descriptors, smiles));

    // ADSR 与分子解绑：映射前保存、映射后恢复。
    const float envA = getBellParam ((int) organic::BellParamId::AmpAttack);
    const float envD = getBellParam ((int) organic::BellParamId::AmpDecay);
    const float envS = getBellParam ((int) organic::BellParamId::AmpSustain);
    const float envR = getBellParam ((int) organic::BellParamId::AmpRelease);

    const auto bellParams = organic::mapMoleculeToBellParams (descriptors, smiles);
    setBellParams (bellParams);

    setBellParam ((int) organic::BellParamId::AmpAttack,  envA);
    setBellParam ((int) organic::BellParamId::AmpDecay,   envD);
    setBellParam ((int) organic::BellParamId::AmpSustain, envS);
    setBellParam ((int) organic::BellParamId::AmpRelease, envR);

    // 用 SMILES 哈希选击打采样。
    if (getNoiseSampleCount() > 0)
        setNoiseSampleIndex ((int) (organic::hashSmiles (smiles) % (uint32_t) getNoiseSampleCount()));

    // 画布无原子时静音。
    setMoleculeEmpty (mol.heavyAtomCount() == 0);

    // 波形/参数切换做一次弱 fade。
    triggerFade();
}

void OrganicChemistryAudioProcessor::setMoleculeWave (const organic::WaveTable& table)
{
    moleculeWave.fill (0.0f);
    const int n = juce::jmin (organic::kWaveTableSize, (int) table.samples.size());
    std::copy_n (table.samples.begin(), n, moleculeWave.begin());
}

float OrganicChemistryAudioProcessor::getMacro (int index) const noexcept
{
    return getBellParam ((int) organic::BellParamId::MacroWET + juce::jlimit (0, 3, index));
}

void OrganicChemistryAudioProcessor::setMacro (int index, float value)
{
    setBellParam ((int) organic::BellParamId::MacroWET + juce::jlimit (0, 3, index), value);
}

// ---------------------------------------------------------------------------
//  ADSR 参数（宿主自动化 / CC / 持久化）
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout OrganicChemistryAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "attack",  "Attack",  juce::NormalisableRange<float> (0.0005f, 0.5f, 0.0005f), 0.0005f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "decay",   "Decay",   juce::NormalisableRange<float> (0.01f,   6.0f, 0.01f),   1.1f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "sustain", "Sustain", juce::NormalisableRange<float> (0.0f,    1.0f, 0.01f),   0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release", juce::NormalisableRange<float> (0.01f,   8.0f, 0.01f),   2.6f));
    return layout;
}

void OrganicChemistryAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    int bellId = -1;
    if      (parameterID == "attack")  bellId = (int) organic::BellParamId::AmpAttack;
    else if (parameterID == "decay")   bellId = (int) organic::BellParamId::AmpDecay;
    else if (parameterID == "sustain") bellId = (int) organic::BellParamId::AmpSustain;
    else if (parameterID == "release") bellId = (int) organic::BellParamId::AmpRelease;
    else return;

    setBellParam (bellId, newValue);
    adsrDirty.store (true, std::memory_order_release);
}

void OrganicChemistryAudioProcessor::setAdsrParams (float attack, float decay, float sustain, float release)
{
    if (adsrParams[0] != nullptr) *adsrParams[0] = attack;
    if (adsrParams[1] != nullptr) *adsrParams[1] = decay;
    if (adsrParams[2] != nullptr) *adsrParams[2] = sustain;
    if (adsrParams[3] != nullptr) *adsrParams[3] = release;
}

void OrganicChemistryAudioProcessor::handleMidiControlChanges (juce::MidiBuffer& midiMessages)
{
    if (adsrParams[0] == nullptr)
        return;

    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (! msg.isController())
            continue;

        const int cc = msg.getControllerNumber();
        const float norm = static_cast<float> (msg.getControllerValue()) / 127.0f;

        int idx = -1;
        switch (cc)
        {
            case 20: idx = 0; break;   // Attack
            case 21: idx = 1; break;   // Decay
            case 22: idx = 2; break;   // Sustain
            case 23: idx = 3; break;   // Release
            default: break;
        }

        if (idx >= 0)
            adsrParams[idx]->setValueNotifyingHost (norm);
    }
}

// ---------------------------------------------------------------------------
//  Bell 效果链（BELL Reflections 复刻）
//
//  顺序与 Vital 一致：多段压缩 → 合唱 → 延迟 → 混响 → 降采样。
//  WET 宏控制空间效果干湿，BITCRUSH 宏控制降采样混入。
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::processBellBlock (juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer& midiMessages)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float sr = static_cast<float> (getSampleRate());

    // 空分子静音：没有分子就没有音色。
    if (moleculeEmpty.load (std::memory_order_relaxed) && !calibrationInstance)
    {
        buffer.clear();
        synth.allNotesOff (0, false);
        captureOutputWave (buffer, numSamples);   // 波形归零
        return;
    }

    buffer.clear();
    synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

    // 从 Bell 参数集构建当前音色。
    std::array<float, organic::kNumBellParams> params;
    for (int i = 0; i < organic::kNumBellParams; ++i)
        params[(size_t) i] = getBellParam (i);
    const organic::BellPatch patch = organic::applyBellParams (organic::bellReflectionsPatch(), params);

    // 宏。
    const float wetMacro      = getMacro (0);
    const float bitcrushMacro = getMacro (1);
    const float wetBipolar = wetMacro * 2.0f - 1.0f;   // Vital bipolar 宏

    // 1) 多段压缩器（mix 很小，接近旁通，保留动态）。
    {
        const float mix = patch.compressorMix;
        if (mix > 0.0001f)
            for (int s = 0; s < numSamples; ++s)
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float x = buffer.getSample (ch, s);
                    const float wet = bellCompressor.process (x,
                        patch.compressorThresholdLow, patch.compressorRatioLow, patch.compressorMakeupLow,
                        patch.compressorThresholdMid, patch.compressorRatioMid, patch.compressorMakeupMid,
                        patch.compressorThresholdHigh, patch.compressorRatioHigh, patch.compressorMakeupHigh);
                    buffer.setSample (ch, s, x * (1.0f - mix) + wet * mix);
                }
    }

    // 2) 合唱（Vital 4 声部 + 反馈）。
    {
        const float mix = juce::jlimit (0.0f, 0.5f, patch.chorusMix + wetBipolar * 0.25f);
        if (mix > 0.0001f)
        {
            // 4 个声部的基础延迟（秒），LFO 相位各差 90°，产生更宽的梳状特征。
            const float baseDelays[4] = { 0.008f, 0.010f, 0.012f, 0.014f };
            const float feedback = patch.chorusFeedback;

            for (int s = 0; s < numSamples; ++s)
            {
                chorusLfoPhase += juce::MathConstants<double>::twoPi * patch.chorusRate / sr;
                if (chorusLfoPhase >= juce::MathConstants<double>::twoPi)
                    chorusLfoPhase -= juce::MathConstants<double>::twoPi;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float in = buffer.getSample (ch, s);
                    float wetSum = 0.0f;
                    for (int v = 0; v < 4; ++v)
                    {
                        auto& line = (ch == 0) ? chorusLineL[v] : chorusLineR[v];
                        // 每个声部的 LFO 相位偏移 90°。
                        const float lfo = static_cast<float> (std::sin (chorusLfoPhase
                                              + v * juce::MathConstants<double>::halfPi));
                        const float delaySamples = (baseDelays[v] + 0.002f * lfo) * sr;
                        const float wet = line.popSample (0, delaySamples, true);
                        line.pushSample (0, in + wet * feedback);
                        wetSum += wet;
                    }
                    const float wet = wetSum * 0.25f;   // 4 声部平均
                    buffer.setSample (ch, s, in * (1.0f - mix) + wet * mix);
                }
            }
        }
    }

    // 3) 延迟（带反馈）。
    {
        const float mix = juce::jlimit (0.0f, 0.7f, patch.delayMix + wetBipolar * 0.507f);
        if (mix > 0.0001f)
            for (int s = 0; s < numSamples; ++s)
            {
                const float delaySamples = patch.delayTime * sr;
                const float inL = buffer.getSample (0, s);
                const float inR = numChannels > 1 ? buffer.getSample (1, s) : inL;

                const float delayedL = delayLineL.popSample (0, delaySamples, true);
                const float delayedR = delayLineR.popSample (0, delaySamples, true);

                // Ping-Pong：对侧声道反馈，回声左右交替（Vital delay_style 2）。
                delayLineL.pushSample (0, inL + delayedR * patch.delayFeedback);
                delayLineR.pushSample (0, inR + delayedL * patch.delayFeedback);

                buffer.setSample (0, s, inL + delayedL * mix);
                if (numChannels > 1)
                    buffer.setSample (1, s, inR + delayedR * mix);
            }
    }

    // 4) 混响。
    {
        const float mix = juce::jlimit (0.0f, 0.6f, patch.reverbMix + wetBipolar * 0.39f);
        juce::Reverb::Parameters rp;
        rp.roomSize   = patch.reverbSize;
        rp.damping    = juce::jlimit (0.0f, 1.0f, 1.0f - patch.reverbDecay * 0.5f);
        rp.wetLevel   = mix;
        rp.dryLevel   = 0.85f;
        rp.width      = 0.9f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        juce::dsp::AudioBlock<float> block (buffer);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // 5) 降采样失真（BITCRUSH 宏）。
    {
        const float mix = patch.distortionMix + bitcrushMacro * 0.27f;
        if (mix > 0.0001f)
            for (int s = 0; s < numSamples; ++s)
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float x = buffer.getSample (ch, s);
                    const float wet = bellDownsample[ch].process (x, patch.distortionDrive);
                    buffer.setSample (ch, s, x * (1.0f - mix) + wet * mix);
                }
    }

    // 6) 主电平 + 软削波。
    for (int s = 0; s < numSamples; ++s)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = buffer.getSample (ch, s) * 0.9f;
            buffer.setSample (ch, s, calibrationInstance ? x : std::tanh (x));
        }

    // 捕获实时输出波形供 UI 示波器显示。
    captureOutputWave (buffer, numSamples);
}

void OrganicChemistryAudioProcessor::captureOutputWave (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    int pos = outputWaveWritePos.load (std::memory_order_relaxed);
    for (int s = 0; s < numSamples; ++s)
    {
        outputWave[(size_t) pos] = buffer.getSample (0, s);   // 左声道
        pos = (pos + 1) % kPreviewWaveSize;
    }
    outputWaveWritePos.store (pos, std::memory_order_release);
}

void OrganicChemistryAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels == 0 || numSamples == 0)
        return;

    // 处理 MIDI CC → ADSR 参数（宿主通过 CC 自动化控制）。
    handleMidiControlChanges (midiMessages);

    // Bell 模式：走独立复刻音色链，完全脱离分子。
    processBellBlock (buffer, midiMessages);
    return;
    applyPendingTimbre();

    // 空分子 → 静音。第一次进入静音时把所有发声中的音符掐掉（不留尾音），
    // 之后每块都保持沉默；重新有分子时解除。
    const bool empty = moleculeEmpty.load (std::memory_order_relaxed);
    silenceGate.setTargetValue (empty || (! calibrationInstance && ! timbreReady) ? 0.0f : 1.0f);

    if (empty && ! voicesKilledForSilence)
    {
        synth.allNotesOff (0, false);
        voicesKilledForSilence = true;
    }
    else if (! empty)
    {
        voicesKilledForSilence = false;
    }

    // voice 需要与全局段读到同一个 LFO1 相位起点，所以在渲染之前拍一张快照。
    lfoBlockStartPhase = lfoPhase;

    buffer.clear();
    synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

    if (numSamples == 0)
        return;

    // 1) 拉取最新目标值交给平滑器（循环覆盖全部参数）。
    for (int i = 0; i < organic::kNumParams; ++i)
        paramSmoothers[(size_t) i].setTargetValue (targetParams[(size_t) i].load());

    const float sr = static_cast<float> (getSampleRate());

    // 块速率参数取值。
    //
    // 关键：SmoothedValue 的斜坡是按「样本」计数的。逐样本消费的参数每块会
    // 前进 numSamples 步，但只在块开头读一次的参数如果用 getNextValue()，
    // 每块就只前进 1 步——100 ms 的斜坡（48 kHz 下 4800 步）在 512 样本的
    // 块长下会被拉长成 4800 块 ≈ 51 秒，表现为拖动滑杆后声音要过很久才变。
    // skip(numSamples) 一次推进整块，让两类参数保持同一时间基准。
    auto blockParam = [&] (organic::ParamId id)
    {
        return paramSmoothers[(size_t) id].skip (numSamples);
    };

    // 2) 滤波：低切（高通）→ 高切（低通）。逐样本更新 cutoff / resonance。
    for (int s = 0; s < numSamples; ++s)
    {
        lowCutFilter.setCutoffFrequency (
            paramSmoothers[(size_t) organic::ParamId::LowCutHz].getNextValue());
        lowCutFilter.setResonance (
            paramSmoothers[(size_t) organic::ParamId::LowCutRes].getNextValue());
        highCutFilter.setCutoffFrequency (
            paramSmoothers[(size_t) organic::ParamId::HighCutHz].getNextValue());
        highCutFilter.setResonance (
            paramSmoothers[(size_t) organic::ParamId::HighCutRes].getNextValue());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float x = lowCutFilter.processSample (ch, buffer.getSample (ch, s));
            buffer.setSample (ch, s, highCutFilter.processSample (ch, x));
        }
    }

    // 2a) LFO1 主相位推进。
    //
    //     形态滤波器已下沉到 voice 内部（那里才拿得到音高、力度和包络），
    //     所以 LFO1 → 滤波是在 voice 里完成的。这里只把主相位推到块末，
    //     保证下一块的起点与各 voice 推进到的位置一致。用原始目标值而非
    //     平滑值，voice 读的也是原始值，两边不会因平滑速度不同而错开相位。
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        const float rate = getParam ((int) organic::ParamId::LfoRate);
        lfoPhase += twoPi * (double) rate * (double) numSamples / (double) sr;
        lfoPhase = std::fmod (lfoPhase, twoPi);
    }

    // 2b) 梳状共振体：一条按音高调谐的短延迟反馈线。
    //
    //     这是 Karplus-Strong 家族最简形式，效果上等同给声音套一个"共鸣腔"
    //     或"被激励的弦"。反馈回路里挂一个一阶低通做阻尼——真实腔体的高频
    //     总是先衰减掉，没有阻尼的梳状滤波听起来像金属管，加了阻尼才像木头。
    //     调谐跟随最近触发的音符，comb tune 给出与该音的频率比。
    {
        const float mix  = blockParam (organic::ParamId::CombMix);
        const float tune = blockParam (organic::ParamId::CombTune);
        const float fb   = blockParam (organic::ParamId::CombFeedback);
        const float damp = blockParam (organic::ParamId::CombDamp);

        const float noteHz = lastNoteHz.load (std::memory_order_relaxed);
        const float freq = juce::jlimit (30.0f, 5000.0f, noteHz * tune);
        const float delaySamples = juce::jlimit (2.0f, sr * 0.045f, sr / freq);
        const float dampCoeff = juce::jlimit (0.02f, 1.0f, 1.0f - damp * 0.95f);

        // mix = 0 时干声原样通过（in * 1 + wet * 0），所以直接跳过整段：
        // 声学结果一致，但省掉逐样本的延迟线读写。
        if (mix > 0.001f)
        for (int s = 0; s < numSamples; ++s)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& line  = (ch == 0) ? combLineL : combLineR;
                auto& state = (ch == 0) ? combDampStateL : combDampStateR;

                const float in = buffer.getSample (ch, s);
                const float wet = line.popSample (0, delaySamples, true);

                state += (wet - state) * dampCoeff;
                line.pushSample (0, in + state * fb);

                buffer.setSample (ch, s, in * (1.0f - mix) + wet * mix);
            }
        }
    }

    // 2c) 共振峰滤波器：把输出塞进一个元音腔体。
    //
    //     三路并联带通的总能量远低于干声，所以湿声要补偿增益。
    {
        const float morph = blockParam (organic::ParamId::FormantMorph);
        const float mix   = blockParam (organic::ParamId::FormantMix);

        if (mix > 0.001f)
        {
            formantL.setVowel (morph, sr);
            if (numChannels > 1)
                formantR.setVowel (morph, sr);

            for (int s = 0; s < numSamples; ++s)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& bank = (ch == 0) ? formantL : formantR;
                    const float dry = buffer.getSample (ch, s);
                    const float wet = bank.process (dry) * 2.6f;
                    buffer.setSample (ch, s, dry * (1.0f - mix) + wet * mix);
                }
            }
        }
    }

    // 3) 失真：drive 增益 + 可选曲线，按 mix 干湿混合。
    //
    //    四种曲线由 Fsp3（sp3 碳占比）选择：饱和分子用柔和的 tanh，
    //    随着不饱和度上升依次转向硬削波、波形折叠、降位量化。
    {
        const float drive = blockParam (organic::ParamId::Drive);
        const float mix   = blockParam (organic::ParamId::DistMix);
        const int   type  = juce::jlimit (0, 3, (int) std::lround (
                                blockParam (organic::ParamId::DistType)));

        const float driveGain = 1.0f + drive * 15.0f;   // 1 ~ 16

        // 降位量化的台阶数：drive 越大位深越低。
        const float bitLevels = juce::jmax (2.0f, 64.0f - drive * 60.0f);

        for (int s = 0; s < numSamples; ++s)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float x = buffer.getSample (ch, s);
                const float d = x * driveGain;
                float wet = d;

                switch (type)
                {
                    case 0:   // 柔和饱和
                        wet = std::tanh (d);
                        break;

                    case 1:   // 硬削波
                        wet = juce::jlimit (-1.0f, 1.0f, d);
                        break;

                    case 2:   // 波形折叠：超出 ±1 的部分反射回来
                    {
                        float v = d;
                        for (int i = 0; i < 4 && (v > 1.0f || v < -1.0f); ++i)
                            v = (v > 1.0f) ? (2.0f - v) : (-2.0f - v);
                        wet = v;
                        break;
                    }

                    case 3:   // 降位量化
                        wet = std::round (juce::jlimit (-1.0f, 1.0f, d) * bitLevels) / bitLevels;
                        break;

                    default:
                        break;
                }

                buffer.setSample (ch, s, x * (1.0f - mix) + wet * mix);
            }
    }

    // 4) 合唱：调制延迟（LFO 扫动读指针），左右声道同源 LFO。
    {
        const float rate  = blockParam (organic::ParamId::ChorusRate);
        const float depth = blockParam (organic::ParamId::ChorusDepth);
        const float mix   = blockParam (organic::ParamId::ChorusMix);
        const float baseDelaySec = 0.012f;               // 12ms 固定基础延迟

        for (int s = 0; s < numSamples; ++s)
        {
            chorusLfoPhase += juce::MathConstants<double>::twoPi
                            * (double) rate / (double) sr;
            if (chorusLfoPhase >= juce::MathConstants<double>::twoPi)
                chorusLfoPhase -= juce::MathConstants<double>::twoPi;

            const float lfo = static_cast<float> (std::sin (chorusLfoPhase));
            const float delaySamples = (baseDelaySec + depth * lfo) * sr;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& line = (ch == 0) ? chorusLineL[0] : chorusLineR[0];
                const float in = buffer.getSample (ch, s);
                const float wet = line.popSample (0, delaySamples, true);
                line.pushSample (0, in);
                buffer.setSample (ch, s, in * (1.0f - mix) + wet * mix);
            }
        }
    }

    // 5) 混响。juce::dsp::Reverb 按块处理，参数每块更新一次即可。
    {
        juce::Reverb::Parameters rp;
        rp.roomSize   = blockParam (organic::ParamId::ReverbRoomSize);
        rp.damping    = blockParam (organic::ParamId::ReverbDamping);
        rp.wetLevel   = blockParam (organic::ParamId::ReverbWet);
        rp.dryLevel   = 0.85f;
        rp.width      = blockParam (organic::ParamId::ReverbWidth);
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        juce::dsp::AudioBlock<float> block (buffer);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // 6) 延迟（带反馈）。逐样本推进，左右声道独立反馈。
    //    注意：delay 时间必须逐样本平滑并保留小数部分，否则 DelayLine 的
    //    线性插值会因 delayFrac 恒为 0 而退化为最近邻，读指针以整数步进
    //    造成每个回声波形的相位不连续（高频电流声 / 沙沙声）。
    {
        for (int s = 0; s < numSamples; ++s)
        {
            const float feedback = paramSmoothers[(size_t) organic::ParamId::DelayFeedback].getNextValue();
            const float mix      = paramSmoothers[(size_t) organic::ParamId::DelayMix].getNextValue();
            const float pingPong = paramSmoothers[(size_t) organic::ParamId::DelayPingPong].getNextValue();
            const float delaySamples = paramSmoothers[(size_t) organic::ParamId::DelayTimeSec].getNextValue() * sr;

            const float inL = buffer.getSample (0, s);
            const float inR = numChannels > 1 ? buffer.getSample (1, s) : inL;

            const float delayedL = delayLineL.popSample (0, delaySamples, true);
            const float delayedR = delayLineR.popSample (0, delaySamples, true);

            // 乒乓：把反馈按比例交叉送到对侧声道，回声便左右交替跳动。
            // pingPong = 0 时两条延迟线各自独立；= 1 时完全交叉。
            const float fbL = delayedL * (1.0f - pingPong) + delayedR * pingPong;
            const float fbR = delayedR * (1.0f - pingPong) + delayedL * pingPong;

            delayLineL.pushSample (0, inL + fbL * feedback);
            delayLineR.pushSample (0, inR + fbR * feedback);

            buffer.setSample (0, s, inL + delayedL * mix);
            if (numChannels > 1)
                buffer.setSample (1, s, inR + delayedR * mix);
        }
    }

    // 7) 总输出微调。
    buffer.applyGain (blockParam (organic::ParamId::MasterLevel));

    // 7b) LFO1 幅度 / 声像调制。
    //     从块首快照相位起算，与各 voice 完全同相，并使用同一个波形函数。
    {
        const float ampDepth = blockParam (organic::ParamId::LfoToAmp);
        const float panDepth = blockParam (organic::ParamId::LfoToPan);
        const float lfoRate  = getParam ((int) organic::ParamId::LfoRate);
        const float lfoShape = getParam ((int) organic::ParamId::LfoShape);

        if (ampDepth > 0.0001f || panDepth > 0.0001f)
        {
            const double phaseInc = juce::MathConstants<double>::twoPi
                                  * (double) lfoRate / (double) sr;

            double phase = lfoBlockStartPhase;

            for (int s = 0; s < numSamples; ++s)
            {
                const float lfo = lfoShapeValue (phase, lfoShape);
                phase += phaseInc;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;

                // 颤音：深度 1 时在 0..1 之间起伏。
                const float tremolo = 1.0f - ampDepth * 0.5f * (1.0f - lfo);

                if (numChannels > 1 && panDepth > 0.0001f)
                {
                    // 自动声像：等功率左右摆动。
                    const float pan = lfo * panDepth;
                    const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    const float gL = std::cos (angle) * juce::MathConstants<float>::sqrt2;
                    const float gR = std::sin (angle) * juce::MathConstants<float>::sqrt2;

                    buffer.setSample (0, s, buffer.getSample (0, s) * tremolo * gL);
                    buffer.setSample (1, s, buffer.getSample (1, s) * tremolo * gR);
                }
                else
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.setSample (ch, s, buffer.getSample (ch, s) * tremolo);
                }
            }
        }
    }

    // 8) 立体声宽度：mid/side 处理（width 0 = mono，1 = 原样）。
    {
        const float width = blockParam (organic::ParamId::StereoWidth);

        if (numChannels > 1)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                const float l = buffer.getSample (0, s);
                const float r = buffer.getSample (1, s);
                const float mid  = (l + r) * 0.5f;
                const float side = (l - r) * 0.5f;
                buffer.setSample (0, s, mid + side * width);
                buffer.setSample (1, s, mid - side * width);
            }
        }
    }

    // 9) 分子变化弱 fade：收到请求则启动一次淡出→淡入，并清空带记忆的
    //    效果器内部状态（delay / reverb / chorus），清除旧波形尾音。
    if (fadeRequested.exchange (false))
    {
        fadeState = FadeState::dip;
        fadeGain  = 1.0f;

        delayLineL.reset();
        delayLineR.reset();
        for (int i = 0; i < 4; ++i)
        {
            chorusLineL[i].reset();
            chorusLineR[i].reset();
        }
        reverb.reset();

        // 共振体带反馈、共振峰带谐振，切换波形时若不清零，残留能量会变成
        // 一声"叮"。
        combLineL.reset();
        combLineR.reset();
        combDampStateL = 0.0f;
        combDampStateR = 0.0f;
        formantL.reset();
        formantR.reset();
    }

    // 10) 末端处理：fade + 软削波（tanh 软限幅），一次遍历完成。
    for (int s = 0; s < numSamples; ++s)
    {
        float gain = 1.0f;

        if (fadeState == FadeState::dip)
        {
            fadeGain -= fadeDipStep;
            if (fadeGain <= 0.0f)
            {
                fadeGain  = 0.0f;
                fadeState = FadeState::recover;
            }
        }
        else if (fadeState == FadeState::recover)
        {
            fadeGain += fadeRecoverStep;
            if (fadeGain >= 1.0f)
            {
                fadeGain  = 1.0f;
                fadeState = FadeState::idle;
            }
        }

        // 空分子静音门与 fade 相乘：两者都是末端增益，合并成一次遍历。
        gain = fadeGain * silenceGate.getNextValue() * timbreGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = buffer.getSample (ch, s) * gain;
            buffer.setSample (ch, s, calibrationInstance ? x : std::tanh (x));
        }
    }
}

// ---------------------------------------------------------------------------
//  分子 → 声音映射
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::setSynthesisParameters (const organic::SynthesisParameters& p)
{
    targetParams[(size_t) organic::ParamId::MasterLevel].store (p[(size_t) organic::ParamId::MasterLevel]);
    targetParams[(size_t) organic::ParamId::OscLevel].store (p[(size_t) organic::ParamId::OscLevel]);
    if (loudnessCalibration != nullptr)
        loudnessCalibration->request (previewTable, p);
    else
        for (int i = 0; i < organic::kNumParams; ++i)
            targetParams[(size_t) i].store (p[(size_t) i], std::memory_order_relaxed);
}

void OrganicChemistryAudioProcessor::applyPendingTimbre()
{
    const juce::SpinLock::ScopedTryLockType lock (pendingTimbreLock);
    if (! lock.isLocked() || ! pendingTimbreReady || loudnessCalibration == nullptr)
        return;

    pendingTimbreReady = false;
    if (! loudnessCalibration->isCurrent (pendingTimbre.revision))
        return;

    const int active = activeTableIndex.load (std::memory_order_relaxed);
    const bool waveChanged = waveTables[(size_t) active] != pendingTimbre.table;
    if (waveChanged)
    {
        const int next = 1 - active;
        waveTables[(size_t) next] = pendingTimbre.table;
        activeTableIndex.store (next, std::memory_order_release);
        waveTableVersion.fetch_add (1, std::memory_order_release);
        triggerFade();
    }
    for (int i = 0; i < organic::kNumParams; ++i)
        if (i != (int) organic::ParamId::MasterLevel && i != (int) organic::ParamId::OscLevel)
            targetParams[(size_t) i].store (pendingTimbre.params[(size_t) i], std::memory_order_relaxed);

    calibratedGain = pendingTimbre.gain;
    if (! timbreReady)
        timbreGain.setCurrentAndTargetValue (calibratedGain);
    else
    {
        // 没有活动 voice 时仍可能存在混响和延迟尾音，不能硬切增益。
        const float current = timbreGain.getCurrentValue();
        timbreGain.reset (getSampleRate(), calibratedGain < current ? 0.015 : 0.10);
        timbreGain.setCurrentAndTargetValue (current);
        timbreGain.setTargetValue (calibratedGain);
    }
    timbreReady = true;
}

void OrganicChemistryAudioProcessor::setMoleculeEmpty (bool isEmpty) noexcept
{
    moleculeEmpty.store (isEmpty, std::memory_order_relaxed);
}

void OrganicChemistryAudioProcessor::reportNoteFrequency (float hz) noexcept
{
    if (hz > 20.0f && hz < 20000.0f)
        lastNoteHz.store (hz, std::memory_order_relaxed);
}

float OrganicChemistryAudioProcessor::getParam (int index) const noexcept
{
    return targetParams[(size_t) index].load (std::memory_order_relaxed);
}

void OrganicChemistryAudioProcessor::triggerFade()
{
    fadeRequested.store (true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
//  分子状态（DAW 工程持久化）
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::setMolecularState (const juce::ValueTree& tree)
{
    const juce::ScopedLock lock (molecularStateLock);
    molecularState = tree.createCopy();
}

juce::ValueTree OrganicChemistryAudioProcessor::getMolecularState() const
{
    const juce::ScopedLock lock (molecularStateLock);
    return molecularState.createCopy();
}

// ---------------------------------------------------------------------------
//  Wavetable 发布（无锁双缓冲）
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::setWaveTable (const organic::WaveTable& table)
{
    previewTable.fill (0.0f);
    const int n = juce::jmin (organic::kWaveTableSize, (int) table.samples.size());
    std::copy_n (table.samples.begin(), n, previewTable.begin());
    if (calibrationInstance)
    {
        waveTables[0] = previewTable;
        activeTableIndex.store (0);
        waveTableVersion.fetch_add (1);
    }
}

const float* OrganicChemistryAudioProcessor::getWaveTableData() const noexcept
{
    return waveTables[(size_t) activeTableIndex.load (std::memory_order_acquire)].data();
}

int OrganicChemistryAudioProcessor::getWaveTableVersion() const noexcept
{
    return waveTableVersion.load (std::memory_order_acquire);
}

std::vector<float> OrganicChemistryAudioProcessor::getPreviewWave() const
{
    std::vector<float> result ((size_t) kPreviewWaveSize, 0.0f);

    // 返回实时输出波形的最近 kPreviewWaveSize 个采样（示波器）。
    const int pos = outputWaveWritePos.load (std::memory_order_acquire);
    for (int i = 0; i < kPreviewWaveSize; ++i)
        result[(size_t) i] = outputWave[(size_t) ((pos + i) % kPreviewWaveSize)];

    return result;
}

// ---------------------------------------------------------------------------
//  编辑器 / 身份信息
// ---------------------------------------------------------------------------

juce::AudioProcessorEditor* OrganicChemistryAudioProcessor::createEditor()
{
    return new OrganicChemistryAudioProcessorEditor (*this);
}

bool OrganicChemistryAudioProcessor::hasEditor() const { return true; }

const juce::String OrganicChemistryAudioProcessor::getName() const { return "Organic Chemistry"; }

bool OrganicChemistryAudioProcessor::acceptsMidi() const { return true; }
bool OrganicChemistryAudioProcessor::producesMidi() const { return false; }
bool OrganicChemistryAudioProcessor::isMidiEffect() const { return false; }
double OrganicChemistryAudioProcessor::getTailLengthSeconds() const { return 0.0; }

// ---------------------------------------------------------------------------
//  Program（预置）
// ---------------------------------------------------------------------------

int OrganicChemistryAudioProcessor::getNumPrograms() { return 1; }
int OrganicChemistryAudioProcessor::getCurrentProgram() { return 0; }
void OrganicChemistryAudioProcessor::setCurrentProgram (int) {}
const juce::String OrganicChemistryAudioProcessor::getProgramName (int) { return {}; }
void OrganicChemistryAudioProcessor::changeProgramName (int, const juce::String&) {}

// ---------------------------------------------------------------------------
//  状态持久化：把分子拓扑（ValueTree）序列化为 XML 写入宿主工程
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // 统一包装：根节点下挂 ADSR 参数树 + 分子拓扑，便于日后扩展。
    juce::ValueTree root ("OrganicChemistryState");
    if (apvts != nullptr)
        root.appendChild (apvts->copyState(), nullptr);

    const juce::ValueTree mol = getMolecularState();
    if (mol.isValid())
        root.appendChild (mol.createCopy(), nullptr);

    if (auto xml = root.createXml())
    {
        const juce::String xmlString = xml->toString();
        destData.append (xmlString.toRawUTF8(), xmlString.getNumBytesAsUTF8());
    }
}

void OrganicChemistryAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const juce::String xmlString = juce::String::fromUTF8 ((const char*) data, sizeInBytes);
    if (xmlString.isEmpty())
        return;

    auto xml = juce::XmlDocument::parse (xmlString);
    if (xml == nullptr)
        return;

    const juce::ValueTree tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    if (tree.hasType ("OrganicChemistryState"))
    {
        // 新格式：分别恢复 ADSR 参数与分子拓扑。
        if (apvts != nullptr)
        {
            const auto adsrChild = tree.getChildWithName (apvts->state.getType());
            if (adsrChild.isValid())
                apvts->replaceState (adsrChild);
        }

        const auto molChild = tree.getChildWithName ("Molecule");
        if (molChild.isValid())
            setMolecularState (molChild);
    }
    else if (tree.hasType ("Molecule"))
    {
        // 旧格式（仅分子）：兼容早期保存的工程。
        setMolecularState (tree);
    }

    // 重建分子 → 映射到音频，使宿主在未打开界面时也能直接出声。
    applyMolecularStateToAudio();
}

// ---------------------------------------------------------------------------
//  总线布局：合成器仅需输出（mono / stereo）
// ---------------------------------------------------------------------------

bool OrganicChemistryAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out == juce::AudioChannelSet::disabled())
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// ---------------------------------------------------------------------------
//  JUCE 插件入口：宿主加载插件时调用，返回音频处理器实例
// ---------------------------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrganicChemistryAudioProcessor();
}
