#include "PresetMenu.h"

namespace organic
{

namespace
{
// 设计尺寸（800x520 基准），运行时统一乘 uiScale。
constexpr float kColumnWidth  = 176.0f;
constexpr float kRowHeight    = 23.0f;
constexpr float kCardPadding  = 15.0f;
constexpr float kHeaderHeight = 27.0f;
constexpr int   kColumns      = 3;

// 与插件其它部分一致的调色板。
const juce::Colour kCardFill    { 0xFFFFFFFF };
const juce::Colour kCardBorder  { 0xFFE2E2DC };
const juce::Colour kScrim       { 0x40FFFFFF };
const juce::Colour kHeaderText  { 0xFFB0B0AA };
const juce::Colour kHairline    { 0xFFEDEDE8 };
const juce::Colour kItemText    { 0xFF3A3A38 };
const juce::Colour kFormulaText { 0xFFB4B4AE };
const juce::Colour kAccent      { 0xFF2A6FB0 };
const juce::Colour kHoverFill   { 0xFFF3F7FB };
}

PresetMenu::PresetMenu()
{
    setWantsKeyboardFocus (false);
}

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------

int PresetMenu::rowCount() const
{
    const int n = (int) moleculePresets().size();
    return (n + kColumns - 1) / kColumns;
}

juce::Rectangle<int> PresetMenu::cardBounds() const
{
    const float s = uiScale;

    const int w = (int) ((kColumnWidth * kColumns + kCardPadding * 2.0f) * s);
    const int h = (int) ((kHeaderHeight + kCardPadding
                          + (float) rowCount() * kRowHeight) * s);

    return juce::Rectangle<int> (getWidth() / 2 - w / 2, anchorY, w, h);
}

juce::Rectangle<int> PresetMenu::itemBounds (int index) const
{
    const float s = uiScale;
    const auto card = cardBounds();

    // 逐列填充：先填满第一列，再第二列。眼睛从上往下扫比从左往右扫快。
    const int rows = rowCount();
    const int col = index / rows;
    const int row = index % rows;

    return juce::Rectangle<int> (
        card.getX() + (int) (kCardPadding * s) + (int) ((float) col * kColumnWidth * s),
        card.getY() + (int) (kHeaderHeight * s) + (int) ((float) row * kRowHeight * s),
        (int) (kColumnWidth * s),
        (int) (kRowHeight * s));
}

int PresetMenu::itemAt (juce::Point<int> p) const
{
    const int count = (int) moleculePresets().size();

    for (int i = 0; i < count; ++i)
        if (itemBounds (i).contains (p))
            return i;

    return -1;
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void PresetMenu::paint (juce::Graphics& g)
{
    const float s = uiScale;
    const auto card = cardBounds();
    const auto cardF = card.toFloat();
    const float radius = 5.0f * s;

    // 极淡的白纱：把下方的分子画布压下去一点，但不做成"弹窗遮罩"那种黑幕，
    // 保持整体的明亮感。
    g.setColour (kScrim);
    g.fillRect (getLocalBounds());

    // 卡片投影：三层递减的半透明圆角矩形，比 DropShadow 更可控也更干净。
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, 0.018f));
        g.fillRoundedRectangle (cardF.expanded ((float) i * 2.0f * s), radius + (float) i * 2.0f * s);
    }

    g.setColour (kCardFill);
    g.fillRoundedRectangle (cardF, radius);
    g.setColour (kCardBorder);
    g.drawRoundedRectangle (cardF, radius, 1.0f * s);

    // 表头。
    {
        const auto header = juce::Rectangle<int> (
            card.getX() + (int) (kCardPadding * s),
            card.getY(),
            card.getWidth() - (int) (kCardPadding * 2.0f * s),
            (int) (kHeaderHeight * s));

        g.setColour (kHeaderText);
        g.setFont (juce::Font (juce::FontOptions (10.0f * s)));
        g.drawText ("PRESETS", header.withTrimmedBottom ((int) (6.0f * s)),
                    juce::Justification::bottomLeft, false);

        g.setFont (juce::Font (juce::FontOptions (9.5f * s)));
        g.drawText (juce::String ((int) moleculePresets().size()) + " molecules",
                    header.withTrimmedBottom ((int) (6.0f * s)),
                    juce::Justification::bottomRight, false);

        const float y = (float) header.getBottom() - 3.0f * s;
        g.setColour (kHairline);
        g.drawLine ((float) header.getX(), y, (float) header.getRight(), y, 1.0f * s);
    }

    // 列之间的极淡分隔线。
    {
        g.setColour (kHairline);
        for (int c = 1; c < kColumns; ++c)
        {
            const float x = (float) card.getX() + kCardPadding * s
                          + (float) c * kColumnWidth * s - 4.0f * s;
            g.drawLine (x, (float) card.getY() + kHeaderHeight * s + 2.0f * s,
                        x, (float) card.getBottom() - kCardPadding * s * 0.6f, 1.0f * s);
        }
    }

    // 条目。
    const auto& presets = moleculePresets();
    const juce::Font nameFont { juce::FontOptions (12.5f * s) };
    const juce::Font formulaFont { juce::FontOptions (10.5f * s) };

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto row = itemBounds (i);
        const bool hovered = (i == hoveredIndex);
        const bool current = (i == currentIndex);

        if (hovered)
        {
            g.setColour (kHoverFill);
            g.fillRoundedRectangle (row.toFloat().reduced (2.0f * s, 1.0f * s), 3.0f * s);
        }

        if (current)
        {
            // 选中项：左侧一条 2px 强调竖条，不用勾选符号（更克制）。
            g.setColour (kAccent);
            g.fillRect (juce::Rectangle<float> ((float) row.getX() + 2.0f * s,
                                                (float) row.getY() + 4.0f * s,
                                                2.0f * s,
                                                (float) row.getHeight() - 8.0f * s));
        }

        auto text = row.reduced ((int) (10.0f * s), 0);

        g.setColour (hovered || current ? kAccent : kItemText);
        g.setFont (nameFont);
        g.drawText (presets[(size_t) i].name, text,
                    juce::Justification::centredLeft, false);

        g.setColour (hovered ? kAccent.withAlpha (0.55f) : kFormulaText);
        g.setFont (formulaFont);
        g.drawText (presetFormula (i), text.withTrimmedRight ((int) (4.0f * s)),
                    juce::Justification::centredRight, false);
    }
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------

void PresetMenu::mouseMove (const juce::MouseEvent& e)
{
    const int hit = itemAt (e.getPosition());

    if (hit != hoveredIndex)
    {
        hoveredIndex = hit;
        setMouseCursor (hit >= 0 ? juce::MouseCursor::PointingHandCursor
                                 : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PresetMenu::mouseDown (const juce::MouseEvent& e)
{
    const int hit = itemAt (e.getPosition());

    if (hit >= 0)
    {
        if (onChosen != nullptr)
            onChosen (hit);
        return;
    }

    // 点在卡片以外（含卡片内的空白边距）→ 关闭。
    if (onDismiss != nullptr)
        onDismiss();
}

void PresetMenu::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIndex >= 0)
    {
        hoveredIndex = -1;
        repaint();
    }
}

} // namespace organic
