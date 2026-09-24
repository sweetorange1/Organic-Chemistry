#pragma once

#include <JuceHeader.h>
#include "MoleculeModel.h"

// ============================================================
//  Element selection bar
//
//  A row of circular colour chips: C / O / N / S / P
//    - selected : chip grows, a breathing ring appears, label darkens
//    - hovered  : slight lift
//
//  Animation is advanced by the editor's single clock via
//  advanceAnimation(), so no component owns its own Timer.
// ============================================================

namespace organic
{

class ElementBar : public juce::Component
{
public:
    ElementBar();
    ~ElementBar() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** 仅拦截元素行与页签，其余区域（展开时的空白）透传给下层。 */
    bool hitTest (int x, int y) override;

    /** 推进呼吸 + 展开动画。返回 true 表示展开动画仍在进行（布局需要刷新）。 */
    bool advanceAnimation (float dt);

    Element getSelected() const { return selected; }
    void setSelected (Element e);

    /** Global UI scale from window resizing. */
    void setUiScale (float s) { uiScale = s; resized(); }

    std::function<void (Element)> onElementChosen;

    // ===== 展开的反应剖面（ADSR 图形，页签切换）=====

    /** 点击页签时回调，请求 Editor 调整本组件高度（展开 / 收起）。 */
    std::function<void()> onToggleExpanded;

    /** 旋钮改变 ADSR 时回调（attack/decay/sustain/release），供 Editor 更新音频。 */
    std::function<void (float attack, float decay, float sustain, float release)> onEnvelopeChanged;

    /** 展开 / 收起。 */
    void setExpanded (bool e);
    bool isExpanded() const { return expanded; }

    /** 当前展开动画进度（0 = 收起，1 = 展开），供 Editor 插值高度。 */
    float getExpandAnim() const { return expandAnim; }

    /** 用当前 Bell 参数刷新包络图形（attack/decay/release 秒，sustain 0..1）。 */
    void setEnvelopeValues (float attack, float decay, float sustain, float release);

    /** 是否正在编辑某个旋钮（双击输入框打开中）。 */
    bool isKnobEditing() const { return editingKnob >= 0; }

    /** 提交并关闭旋钮输入框（未编辑时为空操作）。 */
    void commitKnobEdit();

private:
    struct Slot
    {
        Element element = Element::Carbon;
        juce::Rectangle<float> bounds;
        float selectAnim = 0.0f;
        float hoverAnim  = 0.0f;
    };

    int slotIndexAt (juce::Point<float> p) const;

    std::vector<Slot> slots;
    Element selected = Element::Carbon;
    int hoveredIndex = -1;
    float breathPhase = 0.0f;
    float uiScale = 1.0f;

    // ===== 反应剖面（ADSR）状态 =====
    bool expanded = false;
    float expandAnim = 0.0f;   // 展开动画进度 0..1
    float knobAnim   = 0.0f;   // 旋钮过渡动画：0 = 元素色块，1 = 旋钮
    int hoveredTab = -1;       // 0 = ATOMS, 1 = REACTION, -1 = 无
    float envAttack  = 0.0005f;
    float envDecay   = 1.1f;
    float envSustain = 0.0f;
    float envRelease = 2.6f;

    // ===== ADSR 旋钮（化学隐喻包装）=====
    // 顺序固定：0=Temperature→Attack, 1=Pressure→Decay, 2=Yield→Sustain, 3=Mass→Release。
    static constexpr int kNumKnobs = 4;
    float knobValues[kNumKnobs] = { 0.0f, 0.0f, 0.0f, 0.0f };  // 0..1
    juce::Rectangle<float> knobBounds[kNumKnobs];
    int   dragKnob = -1;
    float dragStartValue = 0.0f;
    float dragStartY = 0.0f;

    int  knobIndexAt (juce::Point<float> p) const;
    void syncEnvelopeFromKnobs();

    // ===== 双击输入（自定义样式输入框，专业音频单位 ms/%）=====
    struct KnobEditorLookAndFeel : juce::LookAndFeel_V4
    {
        void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
        {
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f);
        }
        void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&) override
        {
            g.setColour (juce::Colour (0xFFC9C9C2));
            g.drawRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f, 1.0f);
        }
    };

    void beginKnobEdit (int index);
    void cancelKnobEdit();

    KnobEditorLookAndFeel knobEditorLaf;
    juce::TextEditor knobEditor;
    int editingKnob = -1;

    juce::Rectangle<float> moleculeTabBounds() const;
    juce::Rectangle<float> reactionTabBounds() const;
    juce::Rectangle<float> envelopeRect() const;   // 展开时曲线绘制区（不含底部元素行）
    void paintEnvelope (juce::Graphics& g, const juce::Rectangle<float>& r);
    void paintKnobs (juce::Graphics& g, float alpha);

    static constexpr float kDotRadius = 13.0f;
    static constexpr float kSlotWidth = 70.0f;
    static constexpr float kKnobWidth = 80.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElementBar)
};

} // namespace organic
