#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>

#include "BellEngine.h"

// ============================================================
//  Test panel — 合成器参数调试面板（可滚动，按处理类型分类）
//
//  顶部 "Test" 按钮展开面板，按处理阶段（Oscillator / Envelope /
//  Filter / Noise / Distortion / Compressor / Chorus / Delay / Reverb /
//  Macro）分组展示 Bell 音色的全部可调参数。参数定义集中在
//  BellEngine.h（BellParamId / bellParamDef）。
//
//  用户拖动滑杆即通过 onParamChanged 把该参数写入处理器。
// ============================================================

namespace organic
{

class TestPanel : public juce::Component
{
public:
    TestPanel();
    ~TestPanel() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** 全局 UI 缩放（随窗口等比缩放）。 */
    void setUiScale (float s) { uiScale = s; }

    /** 刷新全部滑杆为给定参数集。 */
    void updateFromMapping (const std::array<float, kNumBellParams>& p);

    /** 用户拖动滑杆（仅手动操作触发）。 */
    std::function<void (int index, float value)> onParamChanged;

    /** 用户点击「Reset to auto」。 */
    std::function<void()> onResetToAuto;

private:
    /** 按分类重排所有控件并更新滚动内容高度。 */
    void layoutContent();

    static constexpr int kNumCategories = 10;
    static constexpr const char* kCategoryNames[kNumCategories] = {
        "OSCILLATOR", "ENVELOPE", "FILTER", "NOISE", "DISTORTION",
        "COMPRESSOR", "CHORUS", "DELAY", "REVERB", "MACRO"
    };

    juce::Viewport  viewport;
    juce::Component content;

    juce::Slider sliders[kNumBellParams];
    juce::Label  labels[kNumBellParams];
    juce::Label  categoryLabels[kNumCategories];
    juce::TextButton resetButton;

    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestPanel)
};

} // namespace organic
