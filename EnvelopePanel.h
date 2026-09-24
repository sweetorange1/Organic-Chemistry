#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>

#include "BellEngine.h"

// ============================================================
//  EnvelopePanel — Bell 引擎的幅度包络图形编辑器
//
//  把 Bell 引擎的 ADSR 画成一条「激发 → 弛豫 → 余音」曲线：
//      ATTACK    音头（激发 / 敲击，线性爬升）
//      DECAY     衰减（振动弛豫，指数衰减到平台）
//      SUSTAIN   平台（稳定中间体电平）
//      RELEASE   余音（释放，指数衰减到 0）
//
//  四个节点：A / D / R 水平拖动（对数时间轴），S 垂直拖动（电平）。
//  拖动即把该段标记为「手动」，分子变化不再覆盖它；Reset 交还。
//
//  视觉语言与画布一致：白底、细描边、无渐变、CPK 低饱和配色。
//  峰值处画一个过渡态记号（‡），呼应「翻越活化能垒」的化学隐喻。
// ============================================================

namespace organic
{

class EnvelopePanel : public juce::Component
{
public:
    EnvelopePanel();
    ~EnvelopePanel() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;

    /** 全局 UI 缩放（随窗口等比缩放）。 */
    void setUiScale (float s) { uiScale = s; }

    /** 用当前包络值刷新节点（手动锁定的段不会被覆盖）。 */
    void setEnvelopeValues (float attack, float decay, float sustain, float release);

    /** 该段是否被用户手动锁定。seg: 0=A 1=D 2=S 3=R。 */
    bool isSegmentManual (int seg) const;

    /** 全部交还分子驱动。 */
    void clearManual();

    /** 段索引 → BellParamId（单一事实来源，供 Editor 映射跳过锁定段）。 */
    static int bellParamIdForSegment (int seg) noexcept;

    /** 每帧动画（悬停呼吸），由 Editor 的 60Hz Timer 驱动。 */
    void advanceAnimation (float dt);

    /** 用户拖动某段：第一个参数为 BellParamId 索引，第二个为归一后的值。 */
    std::function<void (int bellParamIndex, float value)> onParamChanged;

    /** 用户点击 Reset。 */
    std::function<void()> onResetToAuto;

private:
    // 当前值（attack / decay / release 单位为秒，sustain 0..1）。
    float attack  = 0.0005f;
    float decay   = 1.1f;
    float sustain = 0.0f;
    float release = 2.6f;

    std::array<bool, 4> manual {};
    std::array<float, 4> nodeScale {};   // 悬停呼吸缩放，1.0 为基准

    int  hoveredSeg = -1;
    int  dragSeg    = -1;
    bool isResetHovered = false;

    float uiScale = 1.0f;

    // 曲线绘制区（局部坐标，已乘 uiScale）。
    juce::Rectangle<float> curveRect() const;

    // 节点位置（局部坐标）。
    juce::Point<float> nodePosition (int seg) const;

    // 把指针位置反算成参数值（seg 决定轴方向与范围）。
    float valueFromPosition (int seg, juce::Point<float> pos) const;

    // 命中测试：返回段索引，未命中返回 -1。
    int hitTest (juce::Point<float> pos) const;

    // Reset 按钮区域。
    juce::Rectangle<float> resetBounds() const;

    // 对数归一化 / 反归一化（用于时间轴）。
    float logNorm (float t, float t0, float t1) const noexcept;
    float denorm  (float n, float t0, float t1) const noexcept;
};

} // namespace organic
