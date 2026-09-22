#include "MoleculeAudioMapper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace organic
{

namespace
{

/** Clamp a value into [lo, hi]. */
float clamp (float x, float lo, float hi)
{
    return juce::jlimit (lo, hi, x);
}

/** Map a non-negative integer onto a normalized 0..1 range, saturating at a
    soft ceiling so a handful of atoms already reads as "large". */
float saturateCount (int value, float ceiling)
{
    return clamp ((float) value / juce::jmax (ceiling, 1.0f), 0.0f, 1.0f);
}

/** FNV-1a 32-bit hash, used to turn a SMILES string into a stable seed. */
std::uint32_t fnv1a (const char* data, size_t length)
{
    std::uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i)
    {
        hash ^= static_cast<std::uint8_t> (data[i]);
        hash *= 16777619u;
    }
    return hash;
}

/** xorshift32 — a tiny deterministic PRNG seeded from a string hash. */
struct XorShift32
{
    explicit XorShift32 (std::uint32_t seed) : state (seed != 0 ? seed : 1u) {}

    std::uint32_t next()
    {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    /** Uniform in [0, 1). */
    float nextFloat()
    {
        return static_cast<float> (next() >> 8) / static_cast<float> (1u << 24);
    }

    /** Uniform in [-1, 1). */
    float nextBipolar() { return nextFloat() * 2.0f - 1.0f; }

    std::uint32_t state;
};

} // namespace

// ---------------------------------------------------------------------------
//  Mapping rules
//
//  Design goals (see PROJECT_OVERVIEW.md §9 for the full table):
//    - Every parameter is driven by a *chemical* property, not an arbitrary
//      counter, and the direction of the mapping is justifiable in words.
//      "Aromatic rings sustain a ring current, so they circulate the echoes"
//      is a better rule than "ring count times 0.3".
//    - Each mapping is monotonic, so a molecular edit always moves the sound
//      in a predictable direction.
//    - Ranges are wide enough that a single edit is clearly audible.
//
//  Chemistry -> parameter allocation:
//
//    heavyAtomCount      size            -> level, reverb size, stereo width
//    molecularWeight     inertia         -> attack time
//    heteroAtomCount     polarity        -> low cut, chorus mix
//    longestChainLength  extent          -> release, delay time
//    branchCount         branching       -> detune, chorus depth, delay mix
//    ringCount           cyclicity       -> low-cut res, chorus rate, feedback
//    aromaticRingCount   ring current    -> shape res, high-cut res, ping-pong
//    maxRingSize         ring size       -> sub-oscillator level
//    degreeOfUnsaturation rigidity       -> high cut, drive, reverb damping
//    tpsa                polar surface   -> filter shape morph
//    clogP               lipophilicity   -> shape filter cutoff
//    fractionSp3         saturation      -> distortion curve, wave warp
//    rotatableBondCount  flexibility     -> LFO rate, filter env, LFO->filter
//    hBondDonorCount     H-bond donation -> sustain level
//    hBondAcceptorCount  H-bond accept.  -> decay time
//    wienerIndex         compactness     -> reverb wet
//    randicIndex         connectivity    -> osc spread, reverb width
//    bondPolarity        dipole strength -> LFO->amp, LFO->pan
// ---------------------------------------------------------------------------

SynthesisParameters mapMoleculeToAudio (const ChemicalDescriptors& d)
{
    SynthesisParameters p;

    // 先以默认值填充，再按分子描述符覆盖。
    for (int i = 0; i < kNumParams; ++i)
        p[(size_t) i] = paramDef (i).defaultValue;

    // --- 归一化辅助值（0..1）---
    // 计数类天花板调低，让少量原子就逼近满值，变化更夸张。
    const float sizeN     = saturateCount (d.heavyAtomCount, 10.0f);
    const float heteroN   = saturateCount (d.heteroAtomCount, 5.0f);
    const float unsatN    = saturateCount (d.degreeOfUnsaturation, 5.0f);
    const float chainN    = saturateCount (d.longestChainLength, 8.0f);
    const float ringN     = saturateCount (d.ringCount, 3.0f);
    const float branchN   = saturateCount (d.branchCount, 4.0f);
    const float aromaticN = saturateCount (d.aromaticRingCount, 2.0f);
    const float ringSizeN = saturateCount (d.maxRingSize, 7.0f);
    const float rotN      = saturateCount (d.rotatableBondCount, 6.0f);
    const float donorN    = saturateCount (d.hBondDonorCount, 4.0f);
    const float acceptorN = saturateCount (d.hBondAcceptorCount, 4.0f);
    const float wienerN   = clamp ((float) d.wienerIndex / 220.0f, 0.0f, 1.0f);
    const float randicN   = clamp (d.randicIndex / 6.0f, 0.0f, 1.0f);
    const float polarityN = clamp (d.bondPolarity / 12.0f, 0.0f, 1.0f);
    const float massN     = clamp (d.molecularWeight / 220.0f, 0.0f, 1.0f);

    // TPSA: 0 (纯烃) .. ~140 A^2 (Lipinski 口服吸收上限附近)。
    const float tpsaN = clamp (d.tpsa / 140.0f, 0.0f, 1.0f);

    // cLogP: 典型范围 -3 (亲水) .. +6 (亲脂)，归一化到 0..1。
    const float logPN = clamp ((d.clogP + 3.0f) / 9.0f, 0.0f, 1.0f);

    // Fsp3: 已经是 0..1（sp3 碳占比）。
    const float sp3N = clamp (d.fractionSp3, 0.0f, 1.0f);

    // 环张力：小环（三元、四元）的键角被迫远离理想值，储存大量张力能
    // （环丙烷约 115 kJ/mol，环己烷约 0）。六元及以上视为无张力。
    //
    // 目前唯一的去向（ENV2 -> pitch）尚未接线，所以这里只保留计算，
    // 等手动调试确定弯折幅度后直接启用。
    const float strainN = (d.ringCount > 0)
        ? clamp ((6.0f - (float) d.maxRingSize) / 3.0f, 0.0f, 1.0f)
        : 0.0f;
    juce::ignoreUnused (strainN);

    // -----------------------------------------------------------------------
    //  Oscillator
    // -----------------------------------------------------------------------

    // 分子大小影响音色和空间感，不再直接增加发声电平。
    p[(size_t) ParamId::OscLevel] = 0.50f;

    // 支链让分子"不规整" -> 失谐。
    p[(size_t) ParamId::OscDetune] = clamp (0.01f + branchN * 0.12f, 0.01f, 0.13f);

    // Randic 连接性指数衡量骨架的分散程度 -> 立体展开度。
    p[(size_t) ParamId::OscSpread] = clamp (0.10f + randicN * 0.85f, 0.10f, 0.95f);

    // 大环（环己烷、苯环及更大）低频共振更强 -> 加入低八度子振荡器。
    p[(size_t) ParamId::SubLevel]  = clamp (ringSizeN * 0.55f, 0.0f, 0.55f);

    // sp3 饱和碳越少（越平面、越共轭）-> 波形相位畸变越强，音色越"扭"。
    p[(size_t) ParamId::WaveWarp]  = clamp ((1.0f - sp3N) * 0.75f, 0.0f, 0.75f);

    // NoiseLevel / NoiseColour / GlideSec 暂不接线（见函数末尾说明）。

    // -----------------------------------------------------------------------
    //  Envelope (ADSR)
    // -----------------------------------------------------------------------

    // 分子量 = 惯性：重分子起音更慢。
    p[(size_t) ParamId::AttackSec] = clamp (0.002f + massN * 0.060f, 0.002f, 0.062f);

    // 氢键受体越多，分子间相互作用越强 -> 衰减更缓。
    p[(size_t) ParamId::DecaySec]  = clamp (0.05f + acceptorN * 0.95f, 0.05f, 1.0f);

    // 氢键供体让分子"粘住"彼此（想想水和醇的高沸点）-> 持续电平更高。
    p[(size_t) ParamId::SustainLevel] = clamp (0.35f + donorN * 0.60f, 0.35f, 0.95f);

    // 碳链越长，尾巴越长。
    p[(size_t) ParamId::ReleaseSec] = clamp (0.08f + chainN * 0.90f, 0.08f, 0.98f);

    // EnvCurve / VelSens 暂不接线：它们会无条件改变音量与动态，
    // 必须保持中性值（0 / 1.0）才能还原 v0.12 的音色。

    // -----------------------------------------------------------------------
    //  Filter
    // -----------------------------------------------------------------------

    // 低切：杂原子越多，截止频率越高，音色越"亮/薄"。
    p[(size_t) ParamId::LowCutHz]  = clamp (30.0f + heteroN * 1170.0f, 30.0f, 1200.0f);
    p[(size_t) ParamId::LowCutRes] = clamp (0.707f + ringN * 0.30f, 0.707f, 1.0f);

    // 高切（低通）：不饱和度越高越亮。
    p[(size_t) ParamId::HighCutHz]  = clamp (2000.0f + unsatN * 9000.0f, 2000.0f, 11000.0f);
    // 芳香环的离域 π 体系是"共振稳定"的，给高切一点共振峰。
    p[(size_t) ParamId::HighCutRes] = clamp (0.707f + aromaticN * 0.80f, 0.707f, 1.5f);

    // 形态滤波器：TPSA（拓扑极性表面积）决定滤波器形状在
    // LP -> BP -> HP -> Notch 之间的连续变形。
    // 非极性烃 (TPSA = 0) 是温暖的低通；极性越强，形态越"开放/中空"。
    p[(size_t) ParamId::FilterType] = clamp (tpsaN * 3.0f, 0.0f, 3.0f);

    // cLogP：亲脂（油性）分子听感更暗，亲水分子更亮。
    p[(size_t) ParamId::FilterFreq] = clamp (13000.0f - logPN * 11500.0f, 1500.0f, 13000.0f);

    // 芳香性 -> 共振（环电流的"鸣响"）。
    p[(size_t) ParamId::FilterRes]  = clamp (0.70f + aromaticN * 4.5f, 0.70f, 5.2f);

    // 可旋转键 = 构象自由度 -> 让滤波器随包络动起来（单位：倍频程）。
    //
    // 上限 2.1 倍频程是刻意对齐 v0.12 的：那一版是 envAmt(0..1) x 2.5 oct，
    // 映射给出 rotN * 0.85 -> 最大 2.125 oct。现在参数单位直接就是倍频程，
    // 所以系数写 2.1 才能保持同样的扫动幅度（区别只在驱动源从"包络跟随器"
    // 换成了 per-voice ADSR，后者更干净、无跨音符串扰）。
    p[(size_t) ParamId::FilterEnvAmt] = clamp (rotN * 2.1f, 0.0f, 2.1f);

    // FilterKeyTrk / FilterVelTrk / FilterDrive 暂不接线。
    //
    // 前两个必须保持 0：只有所有 voice 共享同一截止频率时，8 个 per-voice
    // SVF 才与 v0.12 的单个全局滤波器等价（线性系统满足叠加原理，
    // "逐 voice 滤波再求和" == "求和后滤波"）。一旦非零，每个音符的截止
    // 频率就不同，音色随键位漂移——这是 v0.13 听起来变了的主因之一。
    // FilterDrive 非零则会在滤波器前引入 tanh 非线性，叠加原理随之失效。

    // -----------------------------------------------------------------------
    //  Mod envelope (ENV 2) — 暂不接线
    //
    //  ENV2 的 DSP 仍在运行（每个 voice 都有一条独立包络），但三个去向
    //  （-> filter / -> pitch / -> warp）默认深度为 0，因此对声音无影响。
    //  设计意图记录在这里，等手动试听确定幅度后再接：
    //    · 时间常数：分子量 -> attack，Wiener 紧凑度 -> decay，
    //      氢键供体 -> sustain，最长链 -> release
    //    · -> filter：双向。不饱和分子音头把滤波器推开（明亮的"咬"），
    //      高度饱和分子反向（音头闷，随后打开，像木质乐器）
    //    · -> pitch：环张力。三元环储存约 115 kJ/mol，释放时"绷"一下
    //    · -> warp：芳香 π 电子在音头期间"重新排布"
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    //  Distortion
    // -----------------------------------------------------------------------

    p[(size_t) ParamId::Drive]   = clamp (unsatN * 0.70f, 0.0f, 0.70f);
    p[(size_t) ParamId::DistMix] = clamp (unsatN * 0.50f, 0.0f, 0.50f);

    // 失真曲线由 sp3 占比选择：饱和分子用柔和的 tanh，
    // 不饱和 / 芳香分子逐步转向硬削波、折叠、降位。
    p[(size_t) ParamId::DistType] = std::floor (clamp ((1.0f - sp3N) * 3.99f, 0.0f, 3.0f));

    // -----------------------------------------------------------------------
    //  Chorus
    // -----------------------------------------------------------------------

    p[(size_t) ParamId::ChorusRate]  = clamp (0.30f + ringN * 1.20f, 0.30f, 1.50f);
    p[(size_t) ParamId::ChorusDepth] = clamp (0.001f + branchN * 0.005f, 0.001f, 0.006f);
    p[(size_t) ParamId::ChorusMix]   = clamp (0.05f + heteroN * 0.25f, 0.05f, 0.30f);

    // -----------------------------------------------------------------------
    //  LFO
    // -----------------------------------------------------------------------

    // 可旋转键越多，分子构象翻转越频繁 -> LFO 越快。
    p[(size_t) ParamId::LfoRate]     = clamp (0.08f + rotN * 3.4f, 0.08f, 3.5f);
    p[(size_t) ParamId::LfoToFilter] = clamp (rotN * 0.70f, 0.0f, 0.70f);

    // LfoShape / LfoFadeSec / LfoToPitch 暂不接线（shape 0 = 正弦，
    // 正是 v0.12 唯一的波形；fade 0 = 立即全深度）。设计意图：
    //   · shape：支链越多，构象跳变越离散 -> 三角 / 方波 / 阶梯
    //   · fade：重分子起振慢 -> 颤音延迟进入
    //   · -> pitch：刚性环不"抖"，柔性链才有音高晃动

    // 键极性 = 偶极矩强度：极性分子在电场中会摆动 -> 幅度 / 声像调制。
    p[(size_t) ParamId::LfoToAmp] = clamp (polarityN * 0.45f, 0.0f, 0.45f);
    p[(size_t) ParamId::LfoToPan] = clamp (polarityN * 0.60f, 0.0f, 0.60f);

    // -----------------------------------------------------------------------
    //  LFO 2 / Organic / Resonator / Formant — 全部暂不接线
    //
    //  这四组是 v0.13 新增的能力，DSP 已就位但深度 / 湿声全为 0，声音链上
    //  完全透明。设计意图保留在此，等你逐个试听定幅度后再接：
    //
    //  LFO 2（per voice，自由运行）
    //    · rate：简谐振子 ω ∝ √(k/m)，分子越重本征振动越慢（有物理依据）
    //    · shape：最大环尺寸；-> warp：不饱和度；-> filter：芳香环电流
    //
    //  Organic（每音随机 + 慢漂移）—— 这组不对应某个"效果"，而是决定声音
    //  有多"活"。化学依据是构象异构：n 个可旋转键约有 3^n 个能量相近的
    //  构象，室温下不断转换，所以分子每次"存在"的形状都不同；刚性环没有
    //  这个自由度，听感上也就该更机械。
    //    · rand pitch / filter：可旋转键数；rand pan：支链数
    //    · drift rate：Graham 扩散定律，速率 ∝ 1/√M，轻分子游走更快
    //    · drift depth：(1 − 环数)，链状分子才漂
    //
    //  Resonator（跟随音高的梳状共振体）—— 环 = 腔体
    //    · mix / tune：最大环尺寸（tune = ringSize/4，六元环正好给纯五度）
    //    · feedback：芳香性 = 共振稳定，环电流可长时间维持
    //    · damp：sp3 饱和骨架是"软"的，高频损耗更快（木头 vs 金属）
    //
    //  Formant（元音腔体）—— 氢键供体（O–H、N–H）是生物分子的标志性官能团，
    //  水、醇、糖、氨基酸全靠它们组织起来，供体越多越像"会说话的东西"
    //    · morph：杂原子数；mix：氢键供体数
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    //  Reverb
    // -----------------------------------------------------------------------

    p[(size_t) ParamId::ReverbRoomSize] = clamp (0.08f + sizeN * 0.90f, 0.08f, 0.98f);
    p[(size_t) ParamId::ReverbDamping]  = clamp (0.95f - unsatN * 0.90f, 0.05f, 0.95f);

    // Wiener 指数衡量骨架的"铺展"程度（紧凑的环 vs 伸展的长链）。
    p[(size_t) ParamId::ReverbWet]   = clamp (0.05f + wienerN * 0.70f, 0.05f, 0.75f);
    p[(size_t) ParamId::ReverbWidth] = clamp (0.35f + randicN * 0.60f, 0.35f, 0.95f);

    // -----------------------------------------------------------------------
    //  Delay
    // -----------------------------------------------------------------------

    p[(size_t) ParamId::DelayTimeSec]  = clamp (0.04f + chainN * 1.16f, 0.04f, 1.20f);
    p[(size_t) ParamId::DelayFeedback] = clamp (0.05f + ringN * 0.80f, 0.05f, 0.85f);
    p[(size_t) ParamId::DelayMix]      = clamp (0.05f + branchN * 0.60f, 0.05f, 0.65f);

    // 芳香环支持一个真实存在的环形电子流（NMR 中的 ring current）。
    // 让它驱动左右交替的乒乓回声，是本映射里最贴切的一条。
    p[(size_t) ParamId::DelayPingPong] = clamp (aromaticN * 0.95f, 0.0f, 0.95f);

    // -----------------------------------------------------------------------
    //  Master
    // -----------------------------------------------------------------------

    // MasterLevel 不被分子映射，保持默认值（0.90）。
    p[(size_t) ParamId::StereoWidth] = clamp (0.70f + sizeN * 0.30f, 0.70f, 1.0f);

    return p;
}

// ---------------------------------------------------------------------------
//  Wavetable construction
//
//  从分子的规范 SMILES 生成一个单周期波形数组。波形形状完全不受限于
//  正弦/方波/锯齿等预设：谐波数量、幅度衰减、相位都由 SMILES 哈希种子
//  与分子描述符共同决定，因此每个分子都有一个独特且可复现的音色。
// ---------------------------------------------------------------------------

WaveTable buildWaveTable (const ChemicalDescriptors& d,
                          const juce::String& smiles,
                          int size)
{
    WaveTable table;
    table.samples.resize (static_cast<size_t> (juce::jmax (16, size)));

    // 1) 结构式字符串 → 确定性种子（同一分子永远得到同一波形）。
    const std::uint32_t seed = fnv1a (smiles.toRawUTF8(), smiles.length());
    XorShift32 rng (seed);

    // 2) 谐波数量由分子复杂度决定：越复杂谐波越多、音色越"毛糙"。
    const int complexity = d.heavyAtomCount
                         + 2 * d.degreeOfUnsaturation
                         + 2 * d.branchCount
                         + d.heteroAtomCount;
    const int numHarmonics = juce::jlimit (4, 160, complexity);

    // 3) 谐波衰减指数：不饱和度越高衰减越慢 → 更多高频 → 更尖锐/方波化；
    //    饱和小分子衰减快 → 更接近正弦。
    const float unsatN = saturateCount (d.degreeOfUnsaturation, 5.0f);
    const float falloff = 1.7f - unsatN * 1.1f;   // 0.6 (尖锐) ~ 1.7 (平滑)

    // 3b) 化学性质进一步塑造谐波结构：
    //
    //   - 芳香性：离域 π 体系是高度对称、共振稳定的闭合环流。对应到频谱上，
    //     偏向"只保留奇次谐波"的中空音色（单簧管式），并且随机抖动更小、
    //     音色更纯净。
    //   - 键极性：偶极矩带来正负半周不对称，对应偶次谐波增强。
    //   - sp3 占比：饱和分子相位更规整，不饱和分子相位更散乱。
    const float aromaticN = saturateCount (d.aromaticRingCount, 2.0f);
    const float polarityN = clamp (d.bondPolarity / 12.0f, 0.0f, 1.0f);
    const float sp3N      = clamp (d.fractionSp3, 0.0f, 1.0f);

    // 4) 预先生成每个谐波的幅度与相位。
    std::vector<float> amp ((size_t) numHarmonics);
    std::vector<float> phase ((size_t) numHarmonics);

    for (int k = 0; k < numHarmonics; ++k)
    {
        // 基频主导，其余按幂律衰减 + 随机抖动，保证波形平滑又有纹理。
        const float harmonic = static_cast<float> (k + 1);
        float magnitude = std::pow (harmonic, -falloff);

        const bool isOddHarmonic = ((k % 2) == 0);   // k=0 是基频（1 次）

        if (isOddHarmonic)
        {
            // 奇次谐波：芳香性越强越突出。
            magnitude *= 1.0f + aromaticN * 0.35f;
        }
        else
        {
            // 偶次谐波：芳香环压制它们，极性键则重新带回来。
            magnitude *= (1.0f - aromaticN * 0.70f) + polarityN * 0.55f;
        }

        // 芳香分子谐波幅度抖动更小 → 音色更"纯"。
        const float jitter = 0.6f + rng.nextFloat() * 0.8f;
        const float pure   = 1.0f;
        amp[(size_t) k] = magnitude * juce::jmap (aromaticN, jitter, pure);

        // sp3 饱和 → 相位更规整；不饱和 → 相位更散乱。
        phase[(size_t) k] = rng.nextBipolar()
                          * juce::MathConstants<float>::pi
                          * (1.0f - sp3N * 0.55f);
    }

    // 5) 累加正弦合成单周期波形。
    const int n = static_cast<int> (table.samples.size());
    for (int i = 0; i < n; ++i)
    {
        const float theta = juce::MathConstants<float>::twoPi
                          * static_cast<float> (i) / static_cast<float> (n);
        float v = 0.0f;
        for (int k = 0; k < numHarmonics; ++k)
            v += amp[(size_t) k]
               * std::sin (static_cast<float> (k + 1) * theta + phase[(size_t) k]);
        table.samples[(size_t) i] = v;
    }

    // 6) 限定原始波表峰值；整条信号链的响度补偿由处理器后台标定。
    float peak = 0.0f;
    for (float x : table.samples)
        peak = std::max (peak, std::fabs (x));

    if (peak > 1e-6f)
    {
        const float norm = 0.95f / peak;
        for (float& x : table.samples)
            x *= norm;
    }

    return table;
}

} // namespace organic
