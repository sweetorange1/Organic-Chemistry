#include "HalogenEffects.h"

#include <algorithm>
#include <cmath>

namespace organic
{
namespace
{
constexpr double twoPi = 6.2831853071795864769;

float finite (float value) noexcept
{
    return std::isfinite (value) ? std::clamp (value, -32.0f, 32.0f) : 0.0f;
}

float logCosh (float value) noexcept
{
    const float magnitude = std::abs (value);
    return magnitude + std::log1p (std::exp (-2.0f * magnitude)) - 0.69314718056f;
}
}

void HalogenEffects::Ramp::set (float value, int samples) noexcept
{
    if (samples <= 0)
    {
        current = target = value;
        step = 0.0f;
        remaining = 0;
    }
    else if (value != target)
    {
        target = value;
        remaining = samples;
        step = (target - current) / (float) samples;
    }
}

float HalogenEffects::Ramp::next() noexcept
{
    if (remaining > 0)
    {
        current += step;
        if (--remaining == 0)
            current = target;
    }
    return current;
}

void HalogenEffects::Delay::prepare (int capacity)
{
    data.assign ((size_t) std::max (4, capacity + 3), 0.0f);
    write = 0;
}

void HalogenEffects::Delay::clear() noexcept
{
    std::fill (data.begin(), data.end(), 0.0f);
    write = 0;
}

float HalogenEffects::Delay::read (float samples) const noexcept
{
    if (data.empty())
        return 0.0f;
    const int size = (int) data.size();
    float position = (float) write - std::clamp (samples, 1.0f, (float) size - 2.0f);
    if (position < 0.0f)
        position += (float) size;
    const int a = (int) position;
    const int b = a + 1 == size ? 0 : a + 1;
    const float fraction = position - (float) a;
    return data[(size_t) a] + fraction * (data[(size_t) b] - data[(size_t) a]);
}

void HalogenEffects::Delay::push (float value) noexcept
{
    if (data.empty())
        return;
    data[(size_t) write] = finite (value);
    if (++write == (int) data.size())
        write = 0;
}

float HalogenEffects::Shaper::process (float input, float drive, float pole) noexcept
{
    const float x = finite (input) * drive;
    const float delta = x - previous;
    const float shaped = primed && std::abs (delta) > 0.001f
        ? (logCosh (x) - logCosh (previous)) / delta
        : std::tanh (primed ? (x + previous) * 0.5f : x);
    previous = x;
    primed = true;
    const float scaled = shaped / std::sqrt (drive);
    const float output = finite (scaled - dcInput + pole * dcOutput);
    dcInput = scaled;
    dcOutput = output;
    return output;
}

float HalogenEffects::strengthForCount (int count) noexcept
{
    return 1.0f - std::exp (-0.7f * (float) std::clamp (count, 0, 8));
}

void HalogenEffects::prepare (double rate)
{
    sampleRate = std::isfinite (rate) ? std::clamp (rate, 8000.0, 384000.0) : 48000.0;
    rampSamples = std::max (1, (int) std::lround (sampleRate * 0.040));
    combPole = (float) std::exp (-twoPi * 5200.0 / sampleRate);
    dcPole = (float) std::exp (-twoPi * 18.0 / sampleRate);
    for (int ch = 0; ch < 2; ++ch)
    {
        // 梳状滤波延迟 = sampleRate / pitch，最低音 30 Hz 下约 33 ms，
        // 因此容量放宽到 45 ms，避免低音区被 read() 的 clamp 截断。
        comb[(size_t) ch].prepare ((int) std::ceil (sampleRate * 0.045));
        for (int v = 0; v < 3; ++v)
            chorus[(size_t) ch][(size_t) v].prepare ((int) std::ceil (sampleRate * 0.03));
    }
    reset();
}

void HalogenEffects::clearEffect (int index) noexcept
{
    if (index == 0)
    {
        for (auto& line : comb)
            line.clear();
        combDamping.fill (0.0f);
    }
    else if (index == 1)
        ringPhase = 0.0;
    else if (index == 2)
        shapers = {};
    else
    {
        for (auto& channel : chorus)
            for (auto& line : channel)
                line.clear();
        chorusPhase = 0.0;
    }
    active[(size_t) index] = false;
}

void HalogenEffects::reset() noexcept
{
    for (int i = 0; i < 4; ++i)
    {
        levels[(size_t) i].set (0.0f, 0);
        clearEffect (i);
    }
}

void HalogenEffects::setCounts (const std::array<int, 4>& counts, bool immediately) noexcept
{
    for (size_t i = 0; i < levels.size(); ++i)
        levels[i].set (strengthForCount (counts[i]), immediately ? 0 : rampSamples);
}

void HalogenEffects::process (float* const* channels, int numChannels, int numSamples, float pitchHz) noexcept
{
    if (numSamples <= 0 || numChannels <= 0 || channels == nullptr)
        return;

    // 音高跟踪源：最近触发的音符频率。梳状滤波与环形调制都需要它以保持谐波关系。
    const float noteHz = std::isfinite (pitchHz)
        ? std::clamp (pitchHz, 30.0f, 5000.0f)
        : 261.63f;

    const int count = std::min (2, numChannels);
    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<float, 4> amount;
        for (size_t i = 0; i < levels.size(); ++i)
        {
            amount[i] = levels[i].next();
            if (amount[i] > 0.0f)
                active[i] = true;
            else if (active[i])
                clearEffect ((int) i);
        }

        // 环形调制调制器：跟随音高，2:1 谐波比（边带为 f 与 3f，无直流、不跑调）。
        const float modulator = amount[1] > 0.0f ? (float) std::sin (ringPhase) : 0.0f;
        if (amount[1] > 0.0f)
        {
            ringPhase += twoPi * (2.0 * noteHz) / sampleRate;
            if (ringPhase >= twoPi)
                ringPhase -= twoPi;
        }

        // 合唱 LFO：三声部共用同一相位，声部间相位差 120°。
        if (amount[3] > 0.0f)
        {
            chorusPhase += twoPi * 0.6 / sampleRate;
            if (chorusPhase >= twoPi)
                chorusPhase -= twoPi;
        }

        for (int ch = 0; ch < count; ++ch)
        {
            const size_t c = (size_t) ch;
            float value = finite (channels[ch][sample]);

            // ---- F：梳状滤波（跟随音高，基频 = 演奏频率）----
            if (amount[0] > 0.0f)
            {
                const float delaySamples = std::clamp (
                    (float) sampleRate / noteHz, 2.0f, (float) sampleRate * 0.045f);
                const float tap = comb[c].read (delaySamples);
                combDamping[c] = finite (tap + combPole * (combDamping[c] - tap));
                const float feedback = 0.48f + 0.32f * amount[0];
                comb[c].push ((1.0f - feedback) * value + feedback * combDamping[c]);
                const float mix = 0.92f * amount[0];
                // 增加 F 原子同时增强强度（mix/feedback）与整体增益。
                const float gain = 1.0f + 0.7f * amount[0];
                value = (value + mix * ((1.4f * tap - 0.4f * value) - value)) * gain;
            }

            // ---- Cl：环形调制（跟随音高，谐波边带，不再跑调）----
            if (amount[1] > 0.0f)
            {
                const float mix = 0.92f * amount[1];
                const float gain = std::sqrt ((1.0f - mix) * (1.0f - mix) + mix * mix);
                value *= ((1.0f - mix) + mix * 1.41421356f * modulator) / gain;
            }

            // ---- Br：饱和失真（附带响度补偿）----
            if (amount[2] > 0.0f)
            {
                const float drive = 1.0f + 19.0f * amount[2];
                const float saturated = shapers[c].process (value, drive, dcPole);
                value += 0.95f * amount[2] * (saturated - value);
                // 非线性会抬升感知响度，随 Br 数量略微压低输出。
                value *= 1.0f - 0.20f * amount[2];
            }

            // ---- I：合唱（三声部调制延迟）----
            if (amount[3] > 0.0f)
            {
                const float mix = 0.85f * amount[3];
                float wetSum = 0.0f;
                for (int v = 0; v < 3; ++v)
                {
                    const float lfo = (float) std::sin (chorusPhase + v * twoPi / 3.0);
                    const float delaySamples =
                        (0.008f + 0.004f * (float) v + 0.0025f * lfo) * (float) sampleRate;
                    const float wet = chorus[c][(size_t) v].read (delaySamples);
                    chorus[c][(size_t) v].push (value);
                    wetSum += wet;
                }
                const float wet = wetSum * (1.0f / 3.0f);
                value = value * (1.0f - mix) + wet * mix;
            }

            channels[ch][sample] = finite (value);
        }
    }
}
}
