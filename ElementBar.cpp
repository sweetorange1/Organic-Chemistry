#include "ElementBar.h"

#include <cmath>

namespace organic
{

namespace
{
// 四个包络段在曲线水平范围上的占比（从左到右）。
constexpr float kSegStart[4] = { 0.00f, 0.32f, 0.62f, 0.76f };
constexpr float kSegEnd[4]   = { 0.32f, 0.62f, 0.76f, 1.00f };

// 段名：化学隐喻包装（反应坐标，纯英文避免字体回退乱码）。
constexpr const char* kSegName[4] = {
    "Initiation",     // ATTACK：反应开始，强度快速上升
    "Relaxation",     // DECAY：过渡态后弛豫到中间体
    "Intermediate",   // SUSTAIN：稳定中间体平台
    "Release"         // RELEASE：产物释放，强度回落
};

// 对数归一化：时间跨度大（ms ~ s），对数映射让曲线段比例更合理。
float logNorm (float t, float t0, float t1)
{
    const float lo = std::log10 (juce::jmax (t0, 1e-9f));
    const float hi = std::log10 (juce::jmax (t1, 1e-9f));
    const float v  = std::log10 (juce::jmax (t, 1e-9f));
    return juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
}

// 四个 ADSR 旋钮：化学隐喻 → 包络段。
// 顺序固定：0=Temperature→Attack, 1=Pressure→Decay, 2=Yield→Sustain, 3=Mass→Release。
struct KnobDef
{
    const char* name;      // 参数名
    const char* unit;      // 化学单位（显示在读数后）
    float  displayMin;     // 显示量的下界
    float  displayMax;     // 显示量的上界
    bool   logScale;       // 显示量是否按对数映射
    int    decimals;       // 读数小数位
    uint32_t accent;       // 强调色（ARGB，用于值弧与指针）
};

constexpr KnobDef kKnobDefs[4] = {
    { "Temperature", "K",      100.0f, 800.0f, false, 0, 0xFFC05A3C },  // 温度：暖陶土
    { "Pressure",    "atm",    0.1f,   10.0f,  true,  1, 0xFF3E6FA8 },  // 压强：冷蓝
    { "Yield",       "%",      0.0f,   100.0f, false, 0, 0xFF3F7A52 },  // 产率：绿
    { "Mass",        "g/mol",  1.0f,   200.0f, true,  0, 0xFF6E6E68 },  // 质量：中性灰
};

// 旋钮 0..1 → 化学量读数（值 + 单位）。
juce::String knobDisplayText (int index, float k)
{
    const auto& def = kKnobDefs[index];
    const float v = def.logScale
        ? def.displayMin * std::pow (def.displayMax / def.displayMin, k)
        : def.displayMin + (def.displayMax - def.displayMin) * k;
    return juce::String (v, def.decimals) + " " + def.unit;
}

// 包络段真实值文本：Attack/Decay/Release 用 ms，Sustain 用 %。
juce::String envValueText (int index, float attackS, float decayS, float sustain, float releaseS)
{
    switch (index)
    {
        case 0:  // Attack
            return attackS * 1000.0f < 10.0f
                ? juce::String (attackS * 1000.0f, 1) + " ms"
                : juce::String (juce::roundToInt (attackS * 1000.0f)) + " ms";
        case 1:  // Decay
            return juce::String (juce::roundToInt (decayS * 1000.0f)) + " ms";
        case 2:  // Sustain
            return juce::String (juce::roundToInt (sustain * 100.0f)) + " %";
        default: // Release
            return juce::String (juce::roundToInt (releaseS * 1000.0f)) + " ms";
    }
}

// 指数衰减「趋平」时刻（归一化 t）：曲线衰减到 95%（视觉上已趋平）。
float flattenT (float rate)
{
    const float e = std::exp (-rate);
    return -std::log (0.05f + 0.95f * e) / rate;
}

// 双击输入：当前值 → 文本（attack/decay/release 用 ms，sustain 用 %）。
juce::String knobInputText (int index, float a, float d, float s, float r)
{
    switch (index)
    {
        case 0: return a * 1000.0f < 10.0f
                    ? juce::String (a * 1000.0f, 1)
                    : juce::String (juce::roundToInt (a * 1000.0f));
        case 1: return juce::String (juce::roundToInt (d * 1000.0f));
        case 2: return juce::String (juce::roundToInt (s * 100.0f));
        default:return juce::String (juce::roundToInt (r * 1000.0f));
    }
}

const char* knobInputUnit (int index)
{
    return (index == 2) ? "%" : "ms";
}

// 旋钮归一化值（0..1）→ 包络参数值（秒或 0..1）。
float knobToParam (int index, float k)
{
    k = juce::jlimit (0.0f, 1.0f, k);
    switch (index)
    {
        case 0: return 0.0005f * std::pow (1000.0f, 1.0f - k);  // attack: 高温 → 短 attack
        case 1: return 0.01f   * std::pow (600.0f,  1.0f - k);  // decay:  高压 → 短 decay
        case 2: return k;                                        // sustain: 直接
        default:return 0.01f   * std::pow (800.0f,  k);         // release: 大质量 → 长 release
    }
}

// 包络参数值 → 旋钮归一化值（0..1）。
float paramToKnob (int index, float v)
{
    switch (index)
    {
        case 0: return 1.0f - juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (0.0005f, v) / 0.0005f) / std::log (1000.0f));
        case 1: return 1.0f - juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (0.01f,   v) / 0.01f)   / std::log (600.0f));
        case 2: return juce::jlimit (0.0f, 1.0f, v);
        default:return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (0.01f,   v) / 0.01f)   / std::log (800.0f));
    }
}

// 输入值（ms / %）→ 旋钮 0..1（经 paramToKnob + 范围钳制）。
float inputToKnobValue (int index, float input)
{
    switch (index)
    {
        case 0: return paramToKnob (0, juce::jlimit (0.0005f, 0.5f, input / 1000.0f));   // Attack ms
        case 1: return paramToKnob (1, juce::jlimit (0.01f,   6.0f, input / 1000.0f));   // Decay ms
        case 2: return paramToKnob (2, juce::jlimit (0.0f,    1.0f, input / 100.0f));    // Sustain %
        default:return paramToKnob (3, juce::jlimit (0.01f,   8.0f, input / 1000.0f));   // Release ms
    }
}
}

ElementBar::ElementBar()
{
    for (auto e : selectableElements())
    {
        Slot s;
        s.element = e;
        s.selectAnim = (e == selected) ? 1.0f : 0.0f;
        slots.push_back (s);
    }

    // 初始旋钮位置由默认包络反推。
    knobValues[0] = paramToKnob (0, envAttack);
    knobValues[1] = paramToKnob (1, envDecay);
    knobValues[2] = paramToKnob (2, envSustain);
    knobValues[3] = paramToKnob (3, envRelease);

    setMouseCursor (juce::MouseCursor::NormalCursor);

    // 双击输入框（自定义样式，非 JUCE 默认外观）。
    knobEditor.setLookAndFeel (&knobEditorLaf);
    knobEditor.setJustification (juce::Justification::centred);
    knobEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    knobEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xFF2A2A28));
    knobEditor.setColour (juce::TextEditor::highlightColourId, juce::Colour (0x332A6FB0));
    knobEditor.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    knobEditor.setColour (juce::CaretComponent::caretColourId, juce::Colour (0xFF2A2A28));
    knobEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentWhite);
    knobEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xFF2A6FB0));
    knobEditor.setFont (juce::Font (juce::FontOptions (12.0f)));
    knobEditor.setInputRestrictions (0, "0123456789.");
    addChildComponent (knobEditor);

    knobEditor.onReturnKey = [this] { commitKnobEdit(); };
    knobEditor.onEscapeKey = [this] { cancelKnobEdit(); };
    knobEditor.onFocusLost = [this] { commitKnobEdit(); };

    // 注意：不要 setPaintingIsUnclipped(true)。否则展开时 g.fillAll(white) 会
    // 溢出本组件边界、覆盖父组件（顶部抬头区），导致悬停预设区后抬头内容消失。
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------

void ElementBar::resized()
{
    const auto area = getLocalBounds().toFloat();
    const float slotW = kSlotWidth * uiScale;
    const float totalW = slotW * (float) slots.size();
    float x = area.getCentreX() - totalW * 0.5f;

    // 元素/旋钮行固定在组件底部（72 设计像素），上方 20 设计像素是 tab 栏。
    const float chipH = 72.0f * uiScale;
    const float rowY = (float) getHeight() - chipH;
    const float rowH = chipH;

    for (auto& s : slots)
    {
        s.bounds = { x, rowY, slotW, rowH };
        x += slotW;
    }

    // 旋钮行：与色块同高，水平居中排布（展开时替代色块）。
    const float knobW = kKnobWidth * uiScale;
    const float knobTotalW = knobW * (float) kNumKnobs;
    float kx = area.getCentreX() - knobTotalW * 0.5f;
    for (int i = 0; i < kNumKnobs; ++i)
    {
        knobBounds[i] = { kx, rowY, knobW, rowH };
        kx += knobW;
    }
}

int ElementBar::slotIndexAt (juce::Point<float> p) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].bounds.contains (p))
            return (int) i;
    return -1;
}

int ElementBar::knobIndexAt (juce::Point<float> p) const
{
    for (int i = 0; i < kNumKnobs; ++i)
        if (knobBounds[i].contains (p))
            return i;
    return -1;
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void ElementBar::paint (juce::Graphics& g)
{
    // --- 展开时：上方绘制反应剖面（ADSR 图形）---
    if (expanded)
    {
        g.fillAll (juce::Colours::white);
        paintEnvelope (g, envelopeRect());
    }

    // --- 双 Tab：MOLECULE / REACTION（互斥切换，文字 + 下划线，无边框）---
    {
        const float s = uiScale;

        auto paintTab = [&] (const juce::Rectangle<float>& tb, const char* text,
                             bool active, bool hovered)
        {
            const juce::Colour textColour = active ? juce::Colour (0xFF2A2A28)
                                                   : (hovered ? juce::Colour (0xFF6E6E68)
                                                              : juce::Colour (0xFF9A9A94));
            g.setColour (textColour);
            g.setFont (juce::Font (juce::FontOptions (11.0f * s).withStyle (active ? "Bold" : "Regular")));
            g.drawText (text, tb, juce::Justification::centred, false);

            // 选中指示：底部一条短横线（替代黑框，更柔和）。
            if (active)
            {
                const float lineW = 22.0f * s;
                const float lineX = tb.getCentreX() - lineW * 0.5f;
                const float lineY = tb.getBottom() - 2.0f * s;
                g.setColour (juce::Colour (0xFF2A2A28));
                g.drawLine (lineX, lineY, lineX + lineW, lineY, 2.0f * s);
            }
        };

        paintTab (moleculeTabBounds(), "MOLECULE", ! expanded, hoveredTab == 0);
        paintTab (reactionTabBounds(), "REACTION", expanded,  hoveredTab == 1);
    }

    // --- 展开时：左侧「反应条件」分组标题，随旋钮淡入 ---
    if (knobAnim > 0.001f)
    {
        const float s = uiScale;
        const float tabY = (float) getHeight() - 92.0f * s;
        g.setColour (juce::Colour (0xFFA8A8A2).withAlpha (knobAnim));
        g.setFont (juce::Font (juce::FontOptions (8.5f * s).withStyle ("Bold")));
        g.drawText ("REACTION CONDITIONS",
                    juce::Rectangle<float> (16.0f * s, tabY, 180.0f * s, 20.0f * s),
                    juce::Justification::centredLeft, false);
    }

    // --- 分割线（仅 tab 栏底部 = 色块行顶部）---
    {
        const float s = uiScale;
        const float chipTopY = (float) getHeight() - 72.0f * s;
        g.setColour (juce::Colour (0xFFEDEDE8));
        g.drawLine (0.0f, chipTopY, (float) getWidth(), chipTopY, 1.0f * s);
    }

    // --- 元素色块行（收起时显示；展开时淡出并下滑，被旋钮替换）---
    const float fade = 1.0f - knobAnim;
    if (fade > 0.001f)
    {
        const float dotR = kDotRadius * uiScale;
        const float slide = knobAnim * 24.0f * uiScale;

        for (const auto& s : slots)
        {
            const auto& info = elementInfo (s.element);
            const auto colour = juce::Colour (info.colour);

            const float lift = (s.hoverAnim * 3.0f + s.selectAnim * 2.0f) * uiScale;
            const auto centre = juce::Point<float> (s.bounds.getCentreX(),
                                                    s.bounds.getCentreY() - 6.0f * uiScale - lift + slide);

            // Selected state: a slow breathing halo.
            if (s.selectAnim > 0.01f)
            {
                const float breath = 0.5f + 0.5f * std::sin (breathPhase);
                const float ringR = dotR + (6.0f + breath * 3.0f) * uiScale;

                g.setColour (colour.withAlpha (0.16f * s.selectAnim * fade));
                g.fillEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (centre));

                g.setColour (colour.withAlpha (0.42f * s.selectAnim * fade));
                g.drawEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (centre),
                               1.0f * uiScale);
            }

            // Main chip.
            const float r = dotR * (1.0f + s.selectAnim * 0.16f + s.hoverAnim * 0.07f);
            const float dim = (0.45f + 0.55f * juce::jmax (s.selectAnim, s.hoverAnim * 0.7f)) * fade;

            g.setColour (colour.withAlpha (dim));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));

            g.setColour (colour.withAlpha ((0.30f + 0.45f * s.selectAnim) * fade));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.0f * uiScale);

            // Element symbol inside the chip.
            g.setColour (juce::Colours::white.withAlpha ((0.72f + 0.28f * s.selectAnim) * fade));
            g.setFont (juce::Font (juce::FontOptions (r * 0.95f).withStyle ("Bold")));
            g.drawText (info.symbol,
                        juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre),
                        juce::Justification::centred, false);

            // Element name below the chip.
            const float labelAlpha = (0.30f + 0.55f * juce::jmax (s.selectAnim, s.hoverAnim)) * fade;
            g.setColour (juce::Colour (0xFF3A3A3A).withAlpha (labelAlpha));
            g.setFont (juce::Font (juce::FontOptions (10.5f * uiScale)));
            g.drawText (info.name,
                        juce::Rectangle<float> (s.bounds.getX(),
                                                centre.y + r + 6.0f * uiScale,
                                                s.bounds.getWidth(), 14.0f * uiScale),
                        juce::Justification::centred, false);
        }
    }

    // --- ADSR 旋钮行（展开时显示）---
    paintKnobs (g, knobAnim);

    // --- 双击输入框的单位提示（ms / %）---
    if (editingKnob >= 0)
    {
        const float s = uiScale;
        const auto eb = knobEditor.getBounds().toFloat();
        g.setColour (juce::Colour (0xFF6E6E68));
        g.setFont (juce::Font (juce::FontOptions (9.0f * s)));
        g.drawText (knobInputUnit (editingKnob),
                    juce::Rectangle<float> (eb.getRight() + 3.0f * s, eb.getY(),
                                            30.0f * s, eb.getHeight()),
                    juce::Justification::centredLeft, false);
    }
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------

void ElementBar::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position.toFloat();

    // 正在编辑旋钮时，点击输入框以外区域先提交当前值。
    if (editingKnob >= 0)
        commitKnobEdit();

    // REACTION tab：展开反应剖面。
    if (reactionTabBounds().contains (p))
    {
        setExpanded (true);
        if (onToggleExpanded != nullptr)
            onToggleExpanded();
        repaint();
        return;
    }

    // MOLECULE tab：回到分子编辑器。
    if (moleculeTabBounds().contains (p))
    {
        setExpanded (false);
        if (onToggleExpanded != nullptr)
            onToggleExpanded();
        repaint();
        return;
    }

    // 展开态：底部是旋钮，双击进入输入框，单击开始拖拽。
    if (expanded)
    {
        const int ki = knobIndexAt (p);
        if (ki >= 0)
        {
            if (e.getNumberOfClicks() >= 2)
            {
                beginKnobEdit (ki);
                return;
            }

            dragKnob = ki;
            dragStartValue = knobValues[(size_t) ki];
            dragStartY = p.y;
        }
        return;
    }

    const int idx = slotIndexAt (e.position);
    if (idx < 0)
        return;

    setSelected (slots[(size_t) idx].element);
}

void ElementBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob < 0)
        return;

    // 上拖增大、下拖减小；归一化到 0..1。
    const float dy = e.position.y - dragStartY;
    const float v = juce::jlimit (0.0f, 1.0f, dragStartValue - dy / (160.0f * uiScale));

    if (knobValues[(size_t) dragKnob] != v)
    {
        knobValues[(size_t) dragKnob] = v;
        syncEnvelopeFromKnobs();
        repaint();
    }
}

void ElementBar::mouseUp (const juce::MouseEvent&)
{
    dragKnob = -1;
}

void ElementBar::setSelected (Element e)
{
    if (selected == e)
        return;

    selected = e;

    if (onElementChosen != nullptr)
        onElementChosen (e);

    repaint();
}

void ElementBar::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position.toFloat();
    int overTab = -1;
    if (moleculeTabBounds().contains (p))        overTab = 0;
    else if (reactionTabBounds().contains (p)) overTab = 1;

    if (overTab != hoveredTab)
    {
        hoveredTab = overTab;
        repaint();
    }

    // 展开态：底部是旋钮，不做色块 hover。
    if (expanded)
    {
        if (hoveredIndex != -1)
        {
            hoveredIndex = -1;
            repaint();
        }
        setMouseCursor (overTab >= 0 ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
        return;
    }

    const int idx = slotIndexAt (e.position);
    if (idx != hoveredIndex)
    {
        hoveredIndex = idx;
        setMouseCursor ((idx >= 0 || overTab >= 0) ? juce::MouseCursor::PointingHandCursor
                                                   : juce::MouseCursor::NormalCursor);
    }
}

void ElementBar::mouseExit (const juce::MouseEvent&)
{
    hoveredIndex = -1;
    if (hoveredTab != -1)
    {
        hoveredTab = -1;
        repaint();
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

bool ElementBar::hitTest (int x, int y)
{
    // 元素/旋钮行（底部 72 设计像素）与两个 tab 区域拦截，其余空白透传给下层画布。
    const float chipH = 72.0f * uiScale;
    if ((float) y >= (float) getHeight() - chipH)
        return true;
    const float fx = (float) x, fy = (float) y;
    return moleculeTabBounds().contains (fx, fy) || reactionTabBounds().contains (fx, fy);
}

// ---------------------------------------------------------------------------
//  Animation
// ---------------------------------------------------------------------------

bool ElementBar::advanceAnimation (float dt)
{
    breathPhase += dt * 0.055f;
    if (breathPhase > juce::MathConstants<float>::twoPi)
        breathPhase -= juce::MathConstants<float>::twoPi;

    bool needsRepaint = false;

    // 展开动画：向目标（0/1）平滑逼近，变化时通知布局刷新。
    const float target = expanded ? 1.0f : 0.0f;
    const float prevAnim = expandAnim;
    expandAnim += (target - expandAnim) * dt * 0.16f;
    if (std::abs (target - expandAnim) < 0.0005f)
        expandAnim = target;
    const bool layoutChanged = std::abs (expandAnim - prevAnim) > 0.0005f;

    // 旋钮过渡：随展开/收起同步，色块 ⇄ 旋钮平滑切换。
    const float knobTarget = expanded ? 1.0f : 0.0f;
    const float prevKnob = knobAnim;
    knobAnim += (knobTarget - knobAnim) * dt * 0.20f;
    if (std::abs (knobTarget - knobAnim) < 0.0005f)
        knobAnim = knobTarget;
    if (std::abs (knobAnim - prevKnob) > 0.0005f)
        needsRepaint = true;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& s = slots[i];

        const float targetSelect = (s.element == selected) ? 1.0f : 0.0f;
        const float targetHover  = ((int) i == hoveredIndex) ? 1.0f : 0.0f;

        const float ns = s.selectAnim + (targetSelect - s.selectAnim) * dt * 0.16f;
        const float nh = s.hoverAnim  + (targetHover  - s.hoverAnim)  * dt * 0.22f;

        if (std::abs (ns - s.selectAnim) > 0.0005f || std::abs (nh - s.hoverAnim) > 0.0005f)
            needsRepaint = true;

        s.selectAnim = ns;
        s.hoverAnim  = nh;
    }

    // The breathing halo needs a steady repaint while something is selected.
    for (const auto& s : slots)
        if (s.selectAnim > 0.01f)
            needsRepaint = true;

    if (needsRepaint || layoutChanged)
        repaint();

    return layoutChanged;
}

// ---------------------------------------------------------------------------
//  展开 / 收起
// ---------------------------------------------------------------------------

void ElementBar::setExpanded (bool e)
{
    expanded = e;   // 只改目标，展开动画在 advanceAnimation 里推进
}

void ElementBar::setEnvelopeValues (float attack, float decay, float sustain, float release)
{
    envAttack  = attack;
    envDecay   = decay;
    envSustain = sustain;
    envRelease = release;

    // 同步旋钮位置（由参数反推归一化角度）。
    knobValues[0] = paramToKnob (0, attack);
    knobValues[1] = paramToKnob (1, decay);
    knobValues[2] = paramToKnob (2, sustain);
    knobValues[3] = paramToKnob (3, release);

    repaint();
}

void ElementBar::syncEnvelopeFromKnobs()
{
    envAttack  = knobToParam (0, knobValues[0]);
    envDecay   = knobToParam (1, knobValues[1]);
    envSustain = knobToParam (2, knobValues[2]);
    envRelease = knobToParam (3, knobValues[3]);

    if (onEnvelopeChanged != nullptr)
        onEnvelopeChanged (envAttack, envDecay, envSustain, envRelease);
}

void ElementBar::beginKnobEdit (int index)
{
    if (index < 0 || index >= kNumKnobs)
        return;

    editingKnob = index;
    dragKnob = -1;

    knobEditor.setText (knobInputText (index, envAttack, envDecay, envSustain, envRelease));

    const float s = uiScale;
    const auto cell = knobBounds[index];
    const int w = juce::jmax (48, (int) (cell.getWidth() - 10.0f * s));
    const int h = (int) (22.0f * s);
    const float cy = cell.getY() + 26.0f * s;
    knobEditor.setBounds ((int) (cell.getCentreX() - w * 0.5f), (int) (cy - h * 0.5f), w, h);

    knobEditor.setVisible (true);
    knobEditor.toFront (false);
    knobEditor.grabKeyboardFocus();
    knobEditor.selectAll();
    repaint();
}

void ElementBar::commitKnobEdit()
{
    if (editingKnob < 0)
        return;

    const int i = editingKnob;
    editingKnob = -1;

    knobValues[(size_t) i] = inputToKnobValue (i, knobEditor.getText().getFloatValue());
    syncEnvelopeFromKnobs();

    knobEditor.setVisible (false);
    repaint();
}

void ElementBar::cancelKnobEdit()
{
    if (editingKnob < 0)
        return;

    editingKnob = -1;
    knobEditor.setVisible (false);
    repaint();
}

// ---------------------------------------------------------------------------
//  反应剖面（ADSR）几何与绘制
// ---------------------------------------------------------------------------

juce::Rectangle<float> ElementBar::reactionTabBounds() const
{
    const float s = uiScale;
    const float w = 88.0f * s;
    const float h = 20.0f * s;
    const float margin = 16.0f * s;
    // tab 栏位于色块行上方（底部 72~92 设计像素区间），tab 不凸出。
    const float tabY = (float) getHeight() - 92.0f * s;
    return { (float) getWidth() - w - margin, tabY, w, h };
}

juce::Rectangle<float> ElementBar::moleculeTabBounds() const
{
    const float s = uiScale;
    const float w = 92.0f * s;
    const float gap = 6.0f * s;
    const auto r = reactionTabBounds();
    return { r.getX() - gap - w, r.getY(), w, r.getHeight() };
}

juce::Rectangle<float> ElementBar::envelopeRect() const
{
    const float s = uiScale;
    const float padL = 52.0f * s;
    const float padR = 26.0f * s;
    const float top  = 44.0f * s;
    const float bottom = (float) getHeight() - 92.0f * s - 60.0f * s;
    return { padL, top, (float) getWidth() - padL - padR, bottom - top };
}

void ElementBar::paintEnvelope (juce::Graphics& g, const juce::Rectangle<float>& r)
{
    if (r.getWidth() < 20.0f || r.getHeight() < 20.0f)
        return;

    const float s = uiScale;
    const float left   = r.getX();
    const float right  = r.getRight();
    const float top    = r.getY();
    const float bottom = r.getBottom();
    const float width  = r.getWidth();
    const float height = r.getHeight();

    const float sustainY = bottom - envSustain * height;

    const float xA = left + (kSegStart[0] + logNorm (envAttack, 0.0005f, 0.5f) * (kSegEnd[0] - kSegStart[0])) * width;
    const float xR = left + kSegStart[3] * width;

    // 衰减/释放的指数速率：时间越短（对应旋钮越大）曲线越陡。
    const float decayRate   = 3.0f + 11.0f * (1.0f - logNorm (envDecay,   0.01f, 6.0f));
    const float releaseRate = 3.0f + 11.0f * (1.0f - logNorm (envRelease, 0.01f, 8.0f));

    // 动态段边界（供竖线与标签使用）：
    //   left → xA(峰值) → xDecayFlat(弛豫趋平) → xR(释放起点) → xReleaseFlat(释放趋平)。
    const float relaxSpan    = xR - xA;
    const float xDecayFlat   = xA + relaxSpan * flattenT (decayRate);
    const float xReleaseFlat = xR + (right - xR) * flattenT (releaseRate);
    const float segBounds[5] = { left, xA, xDecayFlat, xR, xReleaseFlat };

    // --- 标题 ---
    g.setColour (juce::Colour (0xFFA8A8A2));
    g.setFont (juce::Font (juce::FontOptions (9.0f * s)));
    g.drawText ("REACTION PROFILE",
                juce::Rectangle<int> ((int) left, (int) (top - 32.0f * s), (int) (220.0f * s), (int) (18.0f * s)),
                juce::Justification::topLeft, false);

    // --- 纵轴标签（反应强度，左侧竖排）---
    {
        const float cx = left - 22.0f * s;
        const float cy = top + height * 0.5f;
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, cx, cy));
        g.setColour (juce::Colour (0xFF9A9A94));
        g.setFont (juce::Font (juce::FontOptions (10.0f * s).withStyle ("Bold")));
        g.drawText ("INTENSITY",
                    juce::Rectangle<float> (cx - height * 0.5f, cy - 12.0f * s, height, 24.0f * s),
                    juce::Justification::centred, false);
        g.restoreState();
    }

    // --- 横轴标签（反应时间，底部居中）---
    g.setColour (juce::Colour (0xFF9A9A94));
    g.setFont (juce::Font (juce::FontOptions (9.5f * s).withStyle ("Bold")));
    g.drawText ("REACTION TIME",
                juce::Rectangle<int> ((int) left, (int) (bottom + 6.0f * s), (int) width, (int) (16.0f * s)),
                juce::Justification::centred, false);

    // --- 坐标轴 ---
    g.setColour (juce::Colour (0xFFEDEDE8));
    g.drawLine (left, bottom, right, bottom, 1.0f * s);
    g.drawLine (left, top, left, bottom, 1.0f * s);

    // --- 浅色网格线（营造实验数据图感）---
    g.setColour (juce::Colour (0xFFF1F1EC));
    for (int i = 1; i <= 3; ++i)
    {
        const float gy = bottom - height * i / 4.0f;
        g.drawLine (left, gy, right, gy, 1.0f * s);
    }
    for (int i = 1; i <= 3; ++i)
    {
        const float gx = left + width * i / 4.0f;
        g.drawLine (gx, top, gx, bottom, 1.0f * s);
    }

    // --- 主曲线：激发 → 弛豫 → 中间体平台 → 释放 ---
    // 弛豫+中间体合并为一条平滑指数衰减：从峰值逐渐趋平到 sustain 平台，
    // 第三段顺着第二段的曲度自然过渡为直线，不再在交界处生硬转折。
    juce::Path curve;
    curve.startNewSubPath (left, bottom);
    curve.lineTo (xA, top);

    const float decayK = std::exp (-decayRate);
    const float decayDenom = 1.0f - decayK;
    for (int i = 1; i <= 32; ++i)
    {
        const float t = (float) i / 32.0f;
        const float f = (std::exp (-decayRate * t) - decayK) / decayDenom;
        curve.lineTo (xA + relaxSpan * t, sustainY + (top - sustainY) * f);
    }

    const float releaseK = std::exp (-releaseRate);
    const float releaseDenom = 1.0f - releaseK;
    for (int i = 1; i <= 32; ++i)
    {
        const float t = (float) i / 32.0f;
        const float f = (std::exp (-releaseRate * t) - releaseK) / releaseDenom;
        curve.lineTo (xR + (right - xR) * t, bottom + (sustainY - bottom) * f);
    }
    g.setColour (juce::Colour (0xFF2A2A28));
    g.strokePath (curve, juce::PathStrokeType (2.2f * s));

    // --- Sustain 参考线：直接读出 sustain 真实百分比 ---
    if (envSustain > 0.001f)
    {
        const float sy = sustainY;
        g.setColour (juce::Colour (0x332A6FB0));
        g.drawLine (left, sy, right, sy, 1.0f * s);

        g.setColour (juce::Colour (0xFF6E6E68));
        g.setFont (juce::Font (juce::FontOptions (8.5f * s)));
        g.drawText (juce::String (juce::roundToInt (envSustain * 100.0f)) + "%",
                    juce::Rectangle<float> (right - 34.0f * s, sy - 18.0f * s, 34.0f * s, 14.0f * s),
                    juce::Justification::centredRight, false);
    }

    // --- 段边界竖线（与 Sustain 水平线同风格，标记各段关键时间点）---
    g.setColour (juce::Colour (0x332A6FB0));
    g.drawLine (xA, top, xA, bottom, 1.0f * s);                       // Attack 峰值
    g.drawLine (xDecayFlat, top, xDecayFlat, bottom, 1.0f * s);       // Relaxation 趋平点
    g.drawLine (xReleaseFlat, top, xReleaseFlat, bottom, 1.0f * s);   // Release 趋平点

    // --- 过渡态记号 ‡（峰值上方，呼应活化能垒）---
    g.setColour (juce::Colour (0x662A6FB0));
    g.setFont (juce::Font (juce::FontOptions (13.0f * s)));
    g.drawText (juce::String::fromUTF8 ("\xE2\x80\xA1"),
                juce::Rectangle<int> ((int) (xA - 14.0f * s), (int) (top - 22.0f * s),
                                      (int) (28.0f * s), (int) (18.0f * s)),
                juce::Justification::centred, false);

    // --- 段标签（化学隐喻 + 真实值，动态居中于各自段区间）---
    for (int i = 0; i < 4; ++i)
    {
        const float cx = (segBounds[i] + segBounds[i + 1]) * 0.5f;
        const int labelW = (int) (150.0f * s);

        g.setColour (juce::Colour (0xFF2A2A28));
        g.setFont (juce::Font (juce::FontOptions (9.5f * s)));
        g.drawText (kSegName[i],
                    juce::Rectangle<int> ((int) (cx - labelW * 0.5f), (int) (bottom + 24.0f * s),
                                          labelW, (int) (13.0f * s)),
                    juce::Justification::centred, false);

        // 真实值：Attack/Decay/Release 用 ms，Sustain 用 %。
        g.setColour (juce::Colour (0xFF6E6E68));
        g.setFont (juce::Font (juce::FontOptions (9.0f * s)));
        g.drawText (envValueText (i, envAttack, envDecay, envSustain, envRelease),
                    juce::Rectangle<int> ((int) (cx - labelW * 0.5f), (int) (bottom + 37.0f * s),
                                          labelW, (int) (13.0f * s)),
                    juce::Justification::centred, false);
    }
}

void ElementBar::paintKnobs (juce::Graphics& g, float alpha)
{
    if (alpha < 0.001f)
        return;

    const float s = uiScale;

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const auto& def = kKnobDefs[i];
        const auto cell = knobBounds[i];
        const float cx = cell.getCentreX();
        const float r  = 11.0f * s;
        const float cy = cell.getY() + 26.0f * s - (1.0f - alpha) * 24.0f * s;   // 上滑入场

        const auto centre = juce::Point<float> (cx, cy);
        const float v = knobValues[(size_t) i];

        // -135° 起，按当前值扫最多 270°。
        const float a0 = -0.75f * juce::MathConstants<float>::pi;
        const float a1 = a0 + v * 1.5f * juce::MathConstants<float>::pi;

        // 刻度线（-135° ~ +135° 等距 5 个，仪表感）。
        g.setColour (juce::Colour (0xFFD8D8D1).withAlpha (alpha));
        for (int j = 0; j <= 4; ++j)
        {
            const float a = a0 + (j / 4.0f) * 1.5f * juce::MathConstants<float>::pi;
            const float r1 = r + 5.0f * s;
            const float r2 = r + 8.0f * s;
            g.drawLine (cx + std::sin (a) * r1, cy - std::cos (a) * r1,
                        cx + std::sin (a) * r2, cy - std::cos (a) * r2, 1.0f * s);
        }

        // 值弧（强调色）。
        juce::Path arc;
        const int steps = 24;
        for (int j = 0; j <= steps; ++j)
        {
            const float t = (float) j / (float) steps;
            const float aa = a0 + (a1 - a0) * t;
            const float ax = cx + std::sin (aa) * (r + 3.0f * s);
            const float ay = cy - std::cos (aa) * (r + 3.0f * s);
            if (j == 0) arc.startNewSubPath (ax, ay);
            else        arc.lineTo (ax, ay);
        }
        g.setColour (juce::Colour (def.accent).withAlpha (alpha));
        g.strokePath (arc, juce::PathStrokeType (2.0f * s));

        // 旋钮主体。
        g.setColour (juce::Colour (0xFFF7F7F3).withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
        g.setColour (juce::Colour (0xFFD2D2CB).withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.0f * s);

        // 指针（强调色）。
        const float px = cx + std::sin (a1) * (r - 2.5f * s);
        const float py = cy - std::cos (a1) * (r - 2.5f * s);
        g.setColour (juce::Colour (def.accent).withAlpha (alpha));
        g.drawLine (cx, cy, px, py, 2.0f * s);

        // 名称标签（浅灰，小号）。
        g.setColour (juce::Colour (0xFF9A9A94).withAlpha (alpha));
        g.setFont (juce::Font (juce::FontOptions (8.5f * s)));
        g.drawText (def.name,
                    juce::Rectangle<float> (cell.getX(), cy + r + 3.0f * s,
                                            cell.getWidth(), 11.0f * s),
                    juce::Justification::centred, false);

        // 读数（值 + 单位，深色加粗）。
        g.setColour (juce::Colour (0xFF2A2A28).withAlpha (alpha));
        g.setFont (juce::Font (juce::FontOptions (9.5f * s).withStyle ("Bold")));
        g.drawText (knobDisplayText (i, v),
                    juce::Rectangle<float> (cell.getX(), cy + r + 14.0f * s,
                                            cell.getWidth(), 13.0f * s),
                    juce::Justification::centred, false);
    }
}

} // namespace organic
