#pragma once

#include <array>
#include <vector>

namespace organic
{
class HalogenEffects
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setCounts (const std::array<int, 4>& counts, bool immediately = false) noexcept;
    void process (float* const* channels, int numChannels, int numSamples, float pitchHz = 261.63f) noexcept;
    static float strengthForCount (int count) noexcept;

private:
    struct Ramp
    {
        float current = 0.0f, target = 0.0f, step = 0.0f;
        int remaining = 0;
        void set (float value, int samples) noexcept;
        float next() noexcept;
    };

    struct Delay
    {
        std::vector<float> data;
        int write = 0;
        void prepare (int capacity);
        void clear() noexcept;
        float read (float samples) const noexcept;
        void push (float value) noexcept;
    };

    struct Shaper
    {
        float previous = 0.0f, dcInput = 0.0f, dcOutput = 0.0f;
        bool primed = false;
        float process (float input, float drive, float dcPole) noexcept;
    };

    void clearEffect (int index) noexcept;
    double sampleRate = 48000.0, ringPhase = 0.0, chorusPhase = 0.0;
    float combPole = 0.0f, dcPole = 0.0f;
    int rampSamples = 1920;
    std::array<Ramp, 4> levels;
    std::array<bool, 4> active {};
    std::array<Delay, 2> comb;
    std::array<std::array<Delay, 3>, 2> chorus;
    std::array<float, 2> combDamping {};
    std::array<Shaper, 2> shapers;
};
}
