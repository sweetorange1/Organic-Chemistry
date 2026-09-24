#include "EnvelopePanel.h"

#include <cmath>

namespace organic
{

namespace
{
// 四个段在曲线水平范围上的占比（从左到右）：
//   ATTACK  0.00 ~ 0.32
//   DECAY   0.32 ~ 0.62
//   SUSTAIN 0.62 ~ 0.76（平台，节点在其中点垂直拖动）
//   RELEASE 0.76 ~ 1.00
constexpr float kSegStart[4] = { 0.00f, 0.32f, 0.62f, 0.76f };
constexpr float kSegEnd[4]   = { 0.32f, 0.62f, 0.76f, 1.00f };

constexpr const char* kSegName[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };

// 段 → BellParamId。
constexpr int kSegParam[4] = {
    (int) BellParamId::AmpAttack,
    (int) BellParamId::AmpDecay,
    (int) BellParamId::AmpSustain,
    (int) BellParamId::AmpRelease
};

// 节点 CPK 低饱和色（呼应每段背后的化学隐喻）：
//   ATTACK  → 碳（质量 / 惯性）
//   DECAY   → 氮（氢键受体）
//   SUSTAIN → 氧（氢键供体）
//   RELEASE → 碳（链长）
juce::Colour nodeColour (int seg)
{
    switch (seg)
    {
        case 0:  return juce::Colour (0xFF3A3A3A);   // C 黑
        case 1:  return juce::Colour (0xFF4A7BC4);   // N 蓝
        case 2:  return juce::Colour (0xFFD9544D);   // O 红
        default: return juce::Colour (0xFF3A3A3A);   // C 黑
    }
}
}

// ---------------------------------------------------------------------------
//  构造
// ---------------------------------------------------------------------------

EnvelopePanel::EnvelopePanel()
{
    nodeScale.fill (1.0f);
}

// ---------------------------------------------------------------------------
//  几何
// ---------------------------------------------------------------------------

juce::Rectangle<float> EnvelopePanel::curveRect() const
{
    const float s = uiScale;
    const float padL = 26.0f * s;
    const float padR = 26.0f * s;
    const float top  = 22.0f * s;
    const float bottom = 78.0f * s;
    return { padL, top, (float) getWidth() - padL - padR, bottom - top };
}

juce::Rectangle<float> EnvelopePanel::resetBounds() const
{
    const float s = uiScale;
    const float w = 46.0f * s;
    return { (float) getWidth() - w - 14.0f * s, 3.0f * s, w, 16.0f * s };
}

float EnvelopePanel::logNorm (float t, float t0, float t1) const noexcept
{
    const float lo = std::log10 (juce::jmax (t0, 1e-9f));
    const float hi = std::log10 (juce::jmax (t1, 1e-9f));
    const float v  = std::log10 (juce::jmax (t, 1e-9f));
    return juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
}

float EnvelopePanel::denorm (float n, float t0, float t1) const noexcept
{
    const float lo = std::log10 (juce::jmax (t0, 1e-9f));
    const float hi = std::log10 (juce::jmax (t1, 1e-9f));
    return std::pow (10.0f, lo + n * (hi - lo));
}

juce::Point<float> EnvelopePanel::nodePosition (int seg) const
{
    const auto r = curveRect();
    const float left  = r.getX();
    const float right = r.getRight();
    const float top   = r.getY();
    const float bottom = r.getBottom();
    const float width = r.getWidth();

    const float sustainY = bottom - sustain * (bottom - top);

    switch (seg)
    {
        case 0:   // Attack：峰值顶点
        {
            const float n = logNorm (attack, 0.0005f, 0.5f);
            const float x = left + (kSegStart[0] + n * (kSegEnd[0] - kSegStart[0])) * width;
            return { x, top };
        }
        case 1:   // Decay：衰减段手柄
        {
            const float n = logNorm (decay, 0.01f, 6.0f);
            const float x = left + (kSegStart[1] + n * (kSegEnd[1] - kSegStart[1])) * width;
            const float y = sustainY + (top - sustainY) * std::exp (-n * 4.0f);
            return { x, y };
        }
        case 2:   // Sustain：平台中点，垂直拖动
        {
            const float x = left + ((kSegStart[2] + kSegEnd[2]) * 0.5f) * width;
            return { x, sustainY };
        }
        default:  // Release：释放段手柄
        {
            const float n = logNorm (release, 0.01f, 8.0f);
            const float x = left + (kSegStart[3] + n * (kSegEnd[3] - kSegStart[3])) * width;
            const float y = bottom + (sustainY - bottom) * std::exp (-n * 4.0f);
            return { x, y };
        }
    }
}

float EnvelopePanel::valueFromPosition (int seg, juce::Point<float> pos) const
{
    const auto r = curveRect();
    const float left  = r.getX();
    const float top   = r.getY();
    const float bottom = r.getBottom();
    const float width = r.getWidth();

    // 把指针的水平位置归一到所在段的 0..1。
    auto segNorm = [&] (int s) -> float
    {
        const float n = (pos.x - left) / width;
        return juce::jlimit (0.0f, 1.0f,
                             (n - kSegStart[s]) / (kSegEnd[s] - kSegStart[s]));
    };

    switch (seg)
    {
        case 0:   return denorm (segNorm (0), 0.0005f, 0.5f);          // attack 秒
        case 1:   return denorm (segNorm (1), 0.01f, 6.0f);            // decay 秒
        case 2:   return juce::jlimit (0.0f, 1.0f,                     // sustain 0..1
                                       1.0f - (pos.y - top) / (bottom - top));
        default:  return denorm (segNorm (3), 0.01f, 8.0f);            // release 秒
    }
}

int EnvelopePanel::hitTest (juce::Point<float> pos) const
{
    const float s = uiScale;
    for (int i = 0; i < 4; ++i)
        if (nodePosition (i).getDistanceFrom (pos) <= 11.0f * s)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------
//  外部接口
// ---------------------------------------------------------------------------

int EnvelopePanel::bellParamIdForSegment (int seg) noexcept
{
    return kSegParam[juce::jlimit (0, 3, seg)];
}

void EnvelopePanel::setEnvelopeValues (float a, float d, float s, float r)
{
    if (! manual[0]) attack  = a;
    if (! manual[1]) decay   = d;
    if (! manual[2]) sustain = s;
    if (! manual[3]) release = r;
    repaint();
}

bool EnvelopePanel::isSegmentManual (int seg) const
{
    return manual[(size_t) juce::jlimit (0, 3, seg)];
}

void EnvelopePanel::clearManual()
{
    manual.fill (false);
    repaint();
}

void EnvelopePanel::advanceAnimation (float dt)
{
    juce::ignoreUnused (dt);
    bool changed = false;
    for (int i = 0; i < 4; ++i)
    {
        const float target = (i == dragSeg || i == hoveredSeg) ? 1.25f : 1.0f;
        nodeScale[(size_t) i] += (target - nodeScale[(size_t) i]) * 0.22f;
        if (std::abs (target - nodeScale[(size_t) i]) > 0.002f)
            changed = true;
    }
    if (changed)
        repaint();
}

// ---------------------------------------------------------------------------
//  鼠标
// ---------------------------------------------------------------------------

void EnvelopePanel::mouseDown (const juce::MouseEvent& e)
{
    if (resetBounds().contains (e.position.toFloat()))
    {
        if (onResetToAuto)
            onResetToAuto();
        return;
    }

    dragSeg = hitTest (e.position.toFloat());
}

void EnvelopePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSeg < 0)
        return;

    // 一旦拖动就锁定该段，分子变化不再覆盖。
    if (! manual[(size_t) dragSeg])
        manual[(size_t) dragSeg] = true;

    const float value = valueFromPosition (dragSeg, e.position.toFloat());

    switch (dragSeg)
    {
        case 0:  attack  = value; break;
        case 1:  decay   = value; break;
        case 2:  sustain = value; break;
        default: release = value; break;
    }

    if (onParamChanged)
        onParamChanged (kSegParam[dragSeg], value);

    repaint();
}

void EnvelopePanel::mouseUp (const juce::MouseEvent&)
{
    dragSeg = -1;
}

void EnvelopePanel::mouseMove (const juce::MouseEvent& e)
{
    const int  hovered = hitTest (e.position.toFloat());
    const bool reset   = resetBounds().contains (e.position.toFloat());

    if (hovered != hoveredSeg || reset != isResetHovered)
    {
        hoveredSeg = hovered;
        isResetHovered = reset;
        setMouseCursor ((hovered >= 0 || reset)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvelopePanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredSeg != -1 || isResetHovered)
    {
        hoveredSeg = -1;
        isResetHovered = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

// ---------------------------------------------------------------------------
//  绘制
// ---------------------------------------------------------------------------

void EnvelopePanel::paint (juce::Graphics& g)
{
    const float s = uiScale;

    g.fillAll (juce::Colours::white);

    const auto r = curveRect();
    const float left  = r.getX();
    const float right = r.getRight();
    const float top   = r.getY();
    const float bottom = r.getBottom();
    const float width = r.getWidth();

    const float sustainY = bottom - sustain * (bottom - top);

    // 关键横坐标。
    const float xA  = nodePosition (0).x;                       // 峰值顶点
    const float xS  = left + kSegStart[2] * width;              // 平台起点
    const float xNO = left + kSegStart[3] * width;              // note-off / 释放起点

    // --- 标题 ---
    g.setColour (juce::Colour (0xFFA8A8A2));
    g.setFont (juce::Font { juce::FontOptions (8.5f * s) });
    g.drawText ("AMP ENVELOPE",
                juce::Rectangle<int> ((int) left, 2, (int) (140.0f * s), (int) (16.0f * s)),
                juce::Justification::topLeft, false);

    // --- Reset 按钮 ---
    {
        const auto rb = resetBounds();
        g.setColour (isResetHovered ? juce::Colour (0xFF2A6FB0)
                                    : juce::Colour (0xFF9A9A94));
        g.setFont (juce::Font { juce::FontOptions (9.0f * s) });
        g.drawText ("Reset", rb.toNearestInt(), juce::Justification::centred, false);
        if (isResetHovered)
            g.drawLine (rb.getX() + 2.0f * s, rb.getBottom(),
                        rb.getRight() - 2.0f * s, rb.getBottom(), 1.0f * s);
    }

    // --- 基线参考（浅灰虚线）---
    g.setColour (juce::Colour (0xFFEDEDE8));
    {
        const float dashes[2] = { 3.0f * s, 3.0f * s };
        g.drawDashedLine (juce::Line<float> (left, bottom, right, bottom),
                          dashes, 2, 1.0f * s);
    }

    // --- 主曲线：attack → decay → sustain 平台 → release ---
    juce::Path curve;
    curve.startNewSubPath (left, bottom);
    curve.lineTo (xA, top);                                       // 线性起音

    for (int i = 1; i <= 32; ++i)                                 // 指数衰减
    {
        const float t = (float) i / 32.0f;
        const float x = xA + (xS - xA) * t;
        const float y = sustainY + (top - sustainY) * std::exp (-4.0f * t);
        curve.lineTo (x, y);
    }
    curve.lineTo (xS, sustainY);                                  // 平台
    curve.lineTo (xNO, sustainY);

    for (int i = 1; i <= 32; ++i)                                 // 指数释放
    {
        const float t = (float) i / 32.0f;
        const float x = xNO + (right - xNO) * t;
        const float y = bottom + (sustainY - bottom) * std::exp (-4.0f * t);
        curve.lineTo (x, y);
    }

    g.setColour (juce::Colour (0xFF2A2A28));
    g.strokePath (curve, juce::PathStrokeType (2.0f * s));

    // --- 过渡态记号 ‡（峰值上方，呼应活化能垒）---
    g.setColour (juce::Colour (0x662A6FB0));
    g.setFont (juce::Font { juce::FontOptions (11.0f * s) });
    g.drawText (juce::String::fromUTF8 ("\xE2\x80\xA1"),
                juce::Rectangle<int> ((int) (xA - 12.0f * s), (int) (top - 16.0f * s),
                                      (int) (24.0f * s), (int) (16.0f * s)),
                juce::Justification::centred, false);

    // --- 节点 ---
    for (int i = 0; i < 4; ++i)
    {
        const auto p = nodePosition (i);
        const float rad = 4.5f * s * nodeScale[(size_t) i];

        if (manual[(size_t) i])                                    // 手动锁定光环
        {
            g.setColour (juce::Colour (0xFF2A6FB0));
            g.drawEllipse (p.x - rad - 3.0f * s, p.y - rad - 3.0f * s,
                           2.0f * (rad + 3.0f * s), 2.0f * (rad + 3.0f * s), 1.5f * s);
        }

        g.setColour (nodeColour (i));
        g.fillEllipse (p.x - rad, p.y - rad, 2.0f * rad, 2.0f * rad);
        g.setColour (juce::Colours::white);
        g.drawEllipse (p.x - rad, p.y - rad, 2.0f * rad, 2.0f * rad, 1.0f * s);
    }

    // --- 段标签：数值 + 段名 ---
    for (int i = 0; i < 4; ++i)
    {
        const float cx = left + ((kSegStart[i] + kSegEnd[i]) * 0.5f) * width;
        const int labelW = (int) (120.0f * s);

        juce::String value;
        switch (i)
        {
            case 0:  value = juce::String (attack * 1000.0f, 1) + " ms"; break;
            case 1:  value = juce::String (decay, 2) + " s"; break;
            case 2:  value = juce::String ((int) std::lround (sustain * 100.0f)) + " %"; break;
            default: value = juce::String (release, 2) + " s"; break;
        }

        const int valueY = (int) (82.0f * s);
        const int nameY  = (int) (98.0f * s);

        g.setColour (manual[(size_t) i] ? juce::Colour (0xFF2A6FB0)
                                        : juce::Colour (0xFF2A2A28));
        g.setFont (juce::Font { juce::FontOptions (10.0f * s) });
        g.drawText (value,
                    juce::Rectangle<int> ((int) (cx - labelW * 0.5f), valueY, labelW, (int) (14.0f * s)),
                    juce::Justification::centred, false);

        g.setColour (juce::Colour (0xFF9A9A94));
        g.setFont (juce::Font { juce::FontOptions (8.0f * s) });
        g.drawText (kSegName[i],
                    juce::Rectangle<int> ((int) (cx - labelW * 0.5f), nameY, labelW, (int) (11.0f * s)),
                    juce::Justification::centred, false);
    }
}

void EnvelopePanel::resized()
{
}

} // namespace organic
