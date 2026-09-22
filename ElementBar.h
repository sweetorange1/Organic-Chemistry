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
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    void advanceAnimation (float dt);

    Element getSelected() const { return selected; }
    void setSelected (Element e);

    /** Global UI scale from window resizing. */
    void setUiScale (float s) { uiScale = s; resized(); }

    std::function<void (Element)> onElementChosen;

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

    static constexpr float kDotRadius = 13.0f;
    static constexpr float kSlotWidth = 70.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElementBar)
};

} // namespace organic
