#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <JuceHeader.h>
#include "MoleculeAudioMapper.h"
#include "BellEngine.h"

// ============================================================
//  MorphFilter — 可连续变形的多模态滤波器（TPT 状态变量结构）
//
//  一个 Zavalishin 拓扑保持变换（TPT）SVF 同时产出 LP / BP / HP 三路输出，
//  因此可以在它们之间连续插值，而不是硬切换滤波器类型：
//
//      type 0 --- 1 --- 2 --- 3
//           LP    BP    HP   Notch
//
//  这就是测试面板里的 "Shape type"。零延迟反馈结构在 cutoff 逐样本变化时
//  依然稳定，适合被 LFO / 包络扫动。
// ============================================================
struct MorphFilter
{
    void reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }

    /** 更新系数。cutoff 单位 Hz，q 为品质因数。 */
    void setCoefficients (float cutoffHz, float q, float sampleRate) noexcept
    {
        const float nyquist = sampleRate * 0.5f;
        const float fc = juce::jlimit (20.0f, nyquist * 0.98f, cutoffHz);

        g = std::tan (juce::MathConstants<float>::pi * fc / sampleRate);
        k = 1.0f / juce::jmax (0.05f, q);

        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    /** 处理一个样本，按 morph (0..3) 在四种响应间插值。 */
    float process (float v0, float morph) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;

        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        const float lp = v2;
        const float bp = v1;
        const float hp = v0 - k * v1 - v2;
        const float notch = hp + lp;

        const float m = juce::jlimit (0.0f, 3.0f, morph);

        if (m <= 1.0f)  return lp + (bp - lp) * m;
        if (m <= 2.0f)  return bp + (hp - bp) * (m - 1.0f);
        return hp + (notch - hp) * (m - 2.0f);
    }

private:
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};

// ============================================================
//  FormantFilter — 元音共振峰滤波器组
//
//  三个并联带通对应人声的前三个共振峰 F1/F2/F3。五个元音之间连续插值：
//
//      morph 0 --- 1 --- 2 --- 3 --- 4
//            A     E     I     O     U
//
//  共振峰中心频率取自 Peterson & Barney (1952) 的成年男声测量值，这是语音
//  合成里最常被引用的一组数据。三个共振峰的相对增益按经验取 0 / −7 / −12 dB。
//
//  为什么"有机"：元音共振峰是**生物发声腔体**的特征。把合成器输出送进这组
//  滤波器，声音立刻从"电子"变成"会说话的东西"——这是电子音乐里获得生命感
//  最有效的手段之一（talk box / vocoder 的原理内核）。
// ============================================================
struct FormantFilter
{
    void reset() noexcept
    {
        for (auto& f : bands)
            f.reset();
    }

    /** morph 0..4 在 A/E/I/O/U 之间插值，更新三路带通的中心频率。 */
    void setVowel (float morph, float sampleRate) noexcept
    {
        // Peterson-Barney 成年男声 F1/F2/F3 (Hz)
        static const float table[5][3] = {
            {  730.0f, 1090.0f, 2440.0f },   // A
            {  530.0f, 1840.0f, 2480.0f },   // E
            {  270.0f, 2290.0f, 3010.0f },   // I
            {  570.0f,  840.0f, 2410.0f },   // O
            {  300.0f,  870.0f, 2240.0f },   // U
        };

        const float m = juce::jlimit (0.0f, 4.0f, morph);
        const int   i0 = juce::jlimit (0, 4, (int) m);
        const int   i1 = juce::jmin (4, i0 + 1);
        const float t  = m - (float) i0;

        // 共振峰越高，带宽越宽（Q 大致恒定在 8~12 之间听感最自然）。
        static const float q[3] = { 9.0f, 11.0f, 12.0f };

        for (int b = 0; b < 3; ++b)
        {
            const float f = table[i0][b] + (table[i1][b] - table[i0][b]) * t;
            bands[(size_t) b].setCoefficients (f, q[b], sampleRate);
        }
    }

    /** 三路带通并联求和，按经验增益加权。 */
    float process (float x) noexcept
    {
        // morph = 1.0 正好是 MorphFilter 的纯带通输出。
        return bands[0].process (x, 1.0f) * 1.00f
             + bands[1].process (x, 1.0f) * 0.45f      // -7 dB
             + bands[2].process (x, 1.0f) * 0.25f;     // -12 dB
    }

private:
    MorphFilter bands[3];
};

// ============================================================
//  "Organic Chemistry" — 合成器音频处理器
//
//  声音链：
//    MIDI → 8 复音 wavetable 振荡器（+ 子振荡器 / 波形 warp / ADSR / 声像展开）
//         → 每声部形态滤波器 → 复音混合 → 低切(高通) → 高切(低通)
//         → 共振体/共振峰 → 失真(四种曲线) → 合唱 → 混响 → 延迟(可乒乓)
//         → 主电平 → LFO 幅度/声像 → 立体声宽度 → 响度补偿/fade → 软削波 → 输出
//
//  波形不再固定为正弦：分子的规范 SMILES 经 MoleculeAudioMapper 生成一个
//  单周期 wavetable（任意形状、可复现），voice 查表播放。效果器参数同样
//  由分子描述符映射。编辑器在 UI 线程计算并调用 setWaveTable() /
//  setSynthesisParameters()。后台用固定演奏标定整条信号链的响度，再将
//  波形、参数及补偿增益整体交给音频线程；音频线程不等待标定或访问 Molecule。
// ============================================================
class OrganicChemistryAudioProcessor : public juce::AudioProcessor
{
public:
    OrganicChemistryAudioProcessor();
    ~OrganicChemistryAudioProcessor() override;

    // ===== AudioProcessor 生命周期 =====
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // ===== 编辑器 =====
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // ===== 身份信息 =====
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // ===== Program（预置）=====
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    // ===== 状态持久化 =====
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ===== 总线布局 =====
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // ===== 分子 → 声音映射 =====

    /** UI 线程提交参数及最近一次 setWaveTable 的波形。
        后台完成响度标定后，音频线程整体接收并平滑切换。 */
    void setSynthesisParameters (const organic::SynthesisParameters& p);

    /** UI 线程设置预览及待提交波形；随后调用 setSynthesisParameters
        将波形、参数及对应的响度补偿作为同一音色发布。 */
    void setWaveTable (const organic::WaveTable& table);

    /** 当前活跃 wavetable 的数据指针（仅供 voice 在音频线程读取）。 */
    const float* getWaveTableData() const noexcept;

    /** wavetable 单周期采样点数。 */
    int getWaveTableSize() const noexcept { return organic::kWaveTableSize; }

    /** 音频线程实际接收的波形版本号，用于保持包络的波形交叉淡化。 */
    int getWaveTableVersion() const noexcept;

    /** 读取第 index 个合成参数的目标值（供 voice 在音频线程读取，如
        osc level / detune / attack / release）。线程安全。 */
    float getParam (int index) const noexcept;

    /** 返回一段用于 UI 波形预览的采样（长度 kPreviewWaveSize，约 -1..1）。

        始终返回当前分子 wavetable 的单周期波形（静态预览，不随输出信号
        变化）。编辑器（UI 线程）在绘制右上角波形窗口时调用。线程安全。 */
    std::vector<float> getPreviewWave() const;

    /** 分子拓扑变化时由编辑器调用：请求一次弱 fade-out / fade-in。

        波形随分子变化而重建，切换瞬间若不做包络会出现电流声（click）。
        此方法置位一个原子标志，音频线程在下一块据此做一次快速的淡出再
        淡入。线程安全。 */
    void triggerFade();

    /** 由编辑器（UI 线程）调用：保存当前分子拓扑，供 DAW 工程持久化。

        处理器持有这份状态并在 getStateInformation 中写出。线程安全。 */
    void setMolecularState (const juce::ValueTree& tree);

    /** 返回最近一次 setMolecularState 保存的分子拓扑（可能为空树）。 */
    juce::ValueTree getMolecularState() const;

    /** 由编辑器（UI 线程）调用：画布上没有任何原子时静音。

        没有分子就没有音色可言，继续发出默认正弦并不合理。置位后音频线程
        会用一个 12 ms 的平滑门把输出压到零，并停掉所有发声中的音符，因此
        不会留下混响 / 延迟尾音，也不会有 click。线程安全。 */
    void setMoleculeEmpty (bool isEmpty) noexcept;

    /** 由 voice 在音频线程调用：报告最近触发的音符频率。

        梳状共振体需要跟随音高才能形成"弦 / 腔体"的音程感，但它位于全局
        效果链上（在所有 voice 混合之后），拿不到单个 voice 的音高。这里
        用"最近触发的音符"作为跟踪源——单音演奏完全准确，和弦时以最后按下
        的音为准。这是一个刻意的简化，见 PROJECT_OVERVIEW.md §9.1。 */
    void reportNoteFrequency (float hz) noexcept;

    /** LFO 1 在当前音频块开头的相位（仅音频线程读取）。

        LFO 1 是"全局"调制源，但它同时被每个 voice（滤波 / 音高）和全局段
        （幅度 / 声像）消费。voice 在 synth.renderNextBlock() 里先跑，此时
        主相位还没推进，所以大家统一从这个块首相位起算、按相同速率各自推进，
        算出的序列就完全一致。 */
    double getLfoBlockStartPhase() const noexcept { return lfoBlockStartPhase; }

    // ===== Bell 音色参数（BELL Reflections 复刻）=====

    /** 读取第 index 个 Bell 参数（见 organic::BellParamId）。线程安全。 */
    float getBellParam (int index) const noexcept;

    /** 设置第 index 个 Bell 参数。线程安全。 */
    void setBellParam (int index, float value);

    /** 读取第 index 个宏旋钮（0..3：WET / BITCRUSH / DETUNE / ATTACK）。
        供 BellVoice 在音频线程读取。线程安全。 */
    float getMacro (int index) const noexcept;

    /** 设置第 index 个宏旋钮（0..1）。线程安全。 */
    void setMacro (int index, float value);

    // ===== 噪声击打采样库（noise 目录下的瞬态采样）=====

    /** 当前活跃击打采样的 mono 数据指针（仅音频线程读取）。 */
    const float* getNoiseSampleData() const noexcept;

    /** 当前活跃击打采样的采样点数。 */
    int getNoiseSampleLength() const noexcept;

    /** 当前活跃击打采样的原始采样率。 */
    double getNoiseSampleRate() const noexcept;

    /** 采样库中的采样总数。 */
    int getNoiseSampleCount() const noexcept { return (int) noiseLibrary.size(); }

    /** 选择第 index 个采样（分子哈希决定）。UI 线程调用。 */
    void setNoiseSampleIndex (int index);

    /** 批量设置全部 Bell 参数（分子映射结果）。线程安全。 */
    void setBellParams (const std::array<float, organic::kNumBellParams>& p);

    /** 设置分子波表（近正弦），osc_1 使用。UI 线程调用。 */
    void setMoleculeWave (const organic::WaveTable& table);

    /** osc_1 的波表数据指针（分子波表，默认 kOsc1Wave）。音频线程读取。 */
    const float* getOsc1WaveData() const noexcept { return moleculeWave.data(); }

    // 合成器实例（由 processBlock 驱动；未来 Editor 可访问以扩展控制）
    juce::Synthesiser synth;

private:
    friend class AudioRegressionTests;
    explicit OrganicChemistryAudioProcessor (bool isCalibrationInstance);
    class LoudnessCalibrationThread;
    std::unique_ptr<LoudnessCalibrationThread> loudnessCalibration;
    const bool calibrationInstance;

    using WaveTableBuffer = std::array<float, organic::kWaveTableSize>;
    struct TimbreSnapshot
    {
        WaveTableBuffer table {};
        organic::SynthesisParameters params {};
        float gain = 1.0f;
        unsigned int revision = 0;
    };

    void applyPendingTimbre();
    void processBellBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    void captureOutputWave (const juce::AudioBuffer<float>& buffer, int numSamples);

    // Bell 音色参数（BELL Reflections）。初始值为复刻默认值，
    // 由 TestPanel 滑杆实时覆盖。
    std::array<std::atomic<float>, organic::kNumBellParams> bellParams;

    // 噪声击打采样库（noise 目录下的瞬态采样，构造时加载一次，之后只读）。
    struct NoiseSample
    {
        std::vector<float> data;
        double sampleRate = 44100.0;
    };
    void loadNoiseSamples();
    std::vector<NoiseSample> noiseLibrary;
    std::atomic<int> activeNoiseIndex { 0 };

    // 分子波表（近正弦，osc_1 使用）。默认 kOsc1Wave，分子变化时更新。
    WaveTableBuffer moleculeWave;

    // Bell 效果链：多段压缩 + 降采样。
    organic::MultibandCompressor bellCompressor;
    organic::DownsampleDistortion bellDownsample[2];

    // 实时输出波形（示波器）：音频线程写入环形缓冲，UI 线程读取。
    static constexpr int kPreviewWaveSize = 512;
    std::array<float, kPreviewWaveSize> outputWave {};
    std::atomic<int> outputWaveWritePos { 0 };

    WaveTableBuffer previewTable {};   // UI 线程：预览和下一次标定的输入
    juce::SpinLock pendingTimbreLock;
    TimbreSnapshot pendingTimbre;
    bool pendingTimbreReady = false;
    bool timbreReady = false;   // 音频线程：首次标定前保持静音
    juce::LinearSmoothedValue<float> timbreGain;
    float calibratedGain = 1.0f;

    // 波形只由音频线程切换，voice 在整个块内读取同一份快照。
    std::array<WaveTableBuffer, 2> waveTables;
    std::atomic<int> activeTableIndex { 0 };
    std::atomic<int> waveTableVersion { 0 };

    // ===== 分子状态（DAW 工程持久化）=====
    // UI 线程通过 setMolecularState 写入；宿主保存线程通过
    // getStateInformation 读取。用 CriticalSection 保护。
    juce::CriticalSection molecularStateLock;
    juce::ValueTree molecularState;

    // ===== 效果链 =====
    // 低切：高通 StateVariableTPT 滤波器（12 dB/oct）
    juce::dsp::StateVariableTPTFilter<float> lowCutFilter;

    // 高切：低通 StateVariableTPT 滤波器（音色塑形）
    juce::dsp::StateVariableTPTFilter<float> highCutFilter;

    // 注意：形态滤波器已在 v0.13 下沉到每个 voice 内部，这样 key tracking /
    // velocity / ENV2 才能各自作用于自己的音符。全局链上只保留低切、高切
    // 这类整体音色修剪，以及下面的共振体与共振峰。

    // 梳状共振体：跟随音高的短延迟反馈线（Karplus-Strong 家族），给声音
    // 一个"共鸣腔 / 弦"的物理体感。反馈回路里带一个一阶低通做阻尼。
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> combLineL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> combLineR;
    float combDampStateL = 0.0f;
    float combDampStateR = 0.0f;

    // 共振峰滤波器（元音腔体）。
    FormantFilter formantL, formantR;

    // 最近触发的音符频率，供梳状共振体跟踪音高。
    std::atomic<float> lastNoteHz { 261.63f };

    // 全局 LFO 相位（仅音频线程访问）。
    double lfoPhase = 0.0;
    double lfoBlockStartPhase = 0.0;   // 本块开头的快照，供 voice 对齐

    // 混响：juce::Reverb 的 dsp 封装
    juce::dsp::Reverb reverb;

    // 延迟：带反馈的 DelayLine（左右声道各一条，反馈在 processBlock 内完成）
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLineL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLineR;

    // 合唱：4 声部调制延迟（左右声道各四条），LFO 相位仅音频线程使用。
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> chorusLineL[4];
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> chorusLineR[4];
    double chorusLfoPhase = 0.0;

    // ===== 参数平滑 =====
    // 目标值（原子数组，UI 线程写 / 音频线程读）；平滑器数组（仅音频线程）。
    // 两者均以 ParamId 为索引。原子数组在构造函数体中逐项初始化为默认值。
    std::array<std::atomic<float>, organic::kNumParams> targetParams;
    std::array<juce::LinearSmoothedValue<float>, organic::kNumParams> paramSmoothers;

    // ===== 波形预览（仅静态 wavetable，UI 线程只读）=====

    // ===== 空分子静音门 =====
    // 画布上没有原子时不应该有任何声音。UI 线程置位，音频线程用平滑门执行。
    std::atomic<bool> moleculeEmpty { true };
    juce::LinearSmoothedValue<float> silenceGate;
    bool voicesKilledForSilence = false;   // 仅音频线程

    // ===== 分子变化弱 fade（消除添加/删除元素时的电流声）=====
    std::atomic<bool> fadeRequested { false };   // UI 线程置位，音频线程消费

    // 以下状态仅在音频线程使用（不跨线程共享）。
    enum class FadeState { idle, dip, recover };
    FadeState fadeState = FadeState::idle;
    float fadeGain       = 1.0f;   // 当前 fade 增益（1.0 = 无衰减）
    float fadeDipStep    = 0.0f;   // 每样本淡出步长
    float fadeRecoverStep = 0.0f;  // 每样本淡入步长

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrganicChemistryAudioProcessor)
};
