#include "ElementBar.h"

#include <cmath>

namespace organic
{

ElementBar::ElementBar()
{
    for (auto e : selectableElements())
    {
        Slot s;
        s.element = e;
        s.selectAnim = (e == selected) ? 1.0f : 0.0f;
        slots.push_back (s);
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
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

    for (auto& s : slots)
    {
        s.bounds = { x, area.getY(), slotW, area.getHeight() };
        x += slotW;
    }
}

int ElementBar::slotIndexAt (juce::Point<float> p) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].bounds.contains (p))
            return (int) i;
    return -1;
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void ElementBar::paint (juce::Graphics& g)
{
    const float dotR = kDotRadius * uiScale;

    for (const auto& s : slots)
    {
        const auto& info = elementInfo (s.element);
        const auto colour = juce::Colour (info.colour);

        const float lift = (s.hoverAnim * 3.0f + s.selectAnim * 2.0f) * uiScale;
        const auto centre = juce::Point<float> (s.bounds.getCentreX(),
                                                s.bounds.getCentreY() - 6.0f * uiScale - lift);

        // Selected state: a slow breathing halo.
        if (s.selectAnim > 0.01f)
        {
            const float breath = 0.5f + 0.5f * std::sin (breathPhase);
            const float ringR = dotR + (6.0f + breath * 3.0f) * uiScale;

            g.setColour (colour.withAlpha (0.16f * s.selectAnim));
            g.fillEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (centre));

            g.setColour (colour.withAlpha (0.42f * s.selectAnim));
            g.drawEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (centre),
                           1.0f * uiScale);
        }

        // Main chip.
        const float r = dotR * (1.0f + s.selectAnim * 0.16f + s.hoverAnim * 0.07f);
        const float dim = 0.45f + 0.55f * juce::jmax (s.selectAnim, s.hoverAnim * 0.7f);

        g.setColour (colour.withAlpha (dim));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));

        g.setColour (colour.withAlpha (0.30f + 0.45f * s.selectAnim));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.0f * uiScale);

        // Element symbol inside the chip.
        g.setColour (juce::Colours::white.withAlpha (0.72f + 0.28f * s.selectAnim));
        g.setFont (juce::Font (juce::FontOptions (r * 0.95f).withStyle ("Bold")));
        g.drawText (info.symbol,
                    juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre),
                    juce::Justification::centred, false);

        // Element name below the chip.
        const float labelAlpha = 0.30f + 0.55f * juce::jmax (s.selectAnim, s.hoverAnim);
        g.setColour (juce::Colour (0xFF3A3A3A).withAlpha (labelAlpha));
        g.setFont (juce::Font (juce::FontOptions (10.5f * uiScale)));
        g.drawText (info.name,
                    juce::Rectangle<float> (s.bounds.getX(),
                                            centre.y + r + 6.0f * uiScale,
                                            s.bounds.getWidth(), 14.0f * uiScale),
                    juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------

void ElementBar::mouseDown (const juce::MouseEvent& e)
{
    const int idx = slotIndexAt (e.position);
    if (idx < 0)
        return;

    setSelected (slots[(size_t) idx].element);
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
    const int idx = slotIndexAt (e.position);
    if (idx != hoveredIndex)
    {
        hoveredIndex = idx;
        setMouseCursor (idx >= 0 ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
    }
}

void ElementBar::mouseExit (const juce::MouseEvent&)
{
    hoveredIndex = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

// ---------------------------------------------------------------------------
//  Animation
// ---------------------------------------------------------------------------

void ElementBar::advanceAnimation (float dt)
{
    breathPhase += dt * 0.055f;
    if (breathPhase > juce::MathConstants<float>::twoPi)
        breathPhase -= juce::MathConstants<float>::twoPi;

    bool needsRepaint = false;

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

    if (needsRepaint)
        repaint();
}

} // namespace organic
