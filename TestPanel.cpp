#include "TestPanel.h"

namespace organic
{

namespace
{
const juce::Colour kPanelBg        { 0xFFF4F4F0 };
const juce::Colour kPanelBorder    { 0xFFD8D8D2 };
const juce::Colour kLabelAuto      { 0xFF4A4A48 };
const juce::Colour kCategoryColour { 0xFF9A9A94 };
}

TestPanel::TestPanel()
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false, true, false);   // 仅垂直滚动条

    for (int i = 0; i < kNumBellParams; ++i)
    {
        const auto& def = bellParamDef (i);

        sliders[i].setRange (def.minValue, def.maxValue, def.step);
        sliders[i].setSliderStyle (juce::Slider::LinearHorizontal);
        sliders[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        sliders[i].setValue (def.defaultValue, juce::dontSendNotification);
        content.addAndMakeVisible (sliders[i]);

        labels[i].setText (juce::String (def.name), juce::dontSendNotification);
        labels[i].setFont (juce::Font (juce::FontOptions (11.0f)));
        labels[i].setJustificationType (juce::Justification::centredLeft);
        labels[i].setColour (juce::Label::textColourId, kLabelAuto);
        content.addAndMakeVisible (labels[i]);

        // 仅用户拖动时回调（程序化 setValue 带 dontSendNotification 不会触发）。
        sliders[i].onValueChange = [this, i]
        {
            if (onParamChanged != nullptr)
                onParamChanged (i, static_cast<float> (sliders[i].getValue()));
        };
    }

    for (int c = 0; c < kNumCategories; ++c)
    {
        categoryLabels[c].setText (kCategoryNames[c], juce::dontSendNotification);
        categoryLabels[c].setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
        categoryLabels[c].setJustificationType (juce::Justification::bottomLeft);
        categoryLabels[c].setColour (juce::Label::textColourId, kCategoryColour);
        content.addAndMakeVisible (categoryLabels[c]);
    }

    resetButton.setButtonText ("Reset to default");
    resetButton.onClick = [this]
    {
        if (onResetToAuto != nullptr)
            onResetToAuto();
    };
    content.addAndMakeVisible (resetButton);
}

void TestPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (1));
    layoutContent();
}

void TestPanel::layoutContent()
{
    const int margin = 10;
    const int contentW = juce::jmax (200, viewport.getWidth() - viewport.getScrollBarThickness());

    const int colW    = contentW / 2;
    const int rowH    = 44;
    const int headerH = 24;
    const int labelH  = 14;
    const int sliderH = 22;

    int y = margin;

    // 顶部 Reset 按钮。
    resetButton.setBounds (margin, y, 130, 22);
    y += 30;

    int currentCategory = -1;
    int col = 0;

    for (int i = 0; i < kNumBellParams; ++i)
    {
        const auto& def = bellParamDef (i);

        // 找到当前参数分类的索引。
        int cat = 0;
        for (int c = 0; c < kNumCategories; ++c)
            if (juce::String (def.category) == kCategoryNames[c]) { cat = c; break; }

        if (cat != currentCategory)
        {
            if (currentCategory != -1 && col != 0)
                y += rowH;

            currentCategory = cat;
            categoryLabels[cat].setBounds (margin, y, contentW - margin * 2, headerH);
            y += headerH;
            col = 0;
        }

        const int x = margin + col * colW;
        labels[i].setBounds (x, y, colW - 8, labelH);
        sliders[i].setBounds (x, y + labelH, colW - 8, sliderH);

        col = 1 - col;
        if (col == 0)
            y += rowH;
    }

    if (col != 0)
        y += rowH;

    content.setSize (contentW, y + margin);
}

void TestPanel::paint (juce::Graphics& g)
{
    g.fillAll (kPanelBg);
    g.setColour (kPanelBorder);
    g.drawRect (getLocalBounds().toFloat(), 1.0f);
}

void TestPanel::updateFromMapping (const std::array<float, kNumBellParams>& p)
{
    for (int i = 0; i < kNumBellParams; ++i)
        sliders[i].setValue (p[(size_t) i], juce::dontSendNotification);
}

} // namespace organic
