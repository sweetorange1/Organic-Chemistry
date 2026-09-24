#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::String kWebsiteText = "iisaacbeats.cn";
const juce::String kWebsiteUrl  = "https://iisaacbeats.cn";
const juce::String kClearText   = "Clear";

// Shown in place of the formula when the canvas is empty, so the preset
// picker is discoverable rather than hidden behind an invisible hit area.
const juce::String kPresetHint  = "Presets";

// Layout constants, all expressed at the design size and scaled at runtime.
constexpr float kWebsiteFontSize = 15.0f;
constexpr float kWebsiteMarginX  = 16.0f;
constexpr float kWebsiteMarginY  = 14.0f;

// 版本号：小号浅灰，紧跟网址右侧，便于多轮测试时区分构建。
constexpr float kVersionFontSize = 11.5f;
constexpr float kVersionMarginX  = 8.0f;

constexpr float kTopBarHeight    = 46.0f;
constexpr float kBottomBarHeight = 92.0f;   // 元素栏总高：72 元素/旋钮行 + 20 页签条
constexpr float kChipRowHeight   = 72.0f;   // 元素/旋钮行高度（色块与旋钮共用，保持分界线一致）

constexpr float kFormulaFontSize = 17.0f;
constexpr float kDetailFontSize  = 11.5f;
constexpr float kClearFontSize   = 12.5f;

// Test 面板开关：正式版暂时屏蔽调试面板入口，需要调试参数时改回 true。
constexpr bool kTestPanelEnabled = false;

constexpr float kResizerSize = 16.0f;

// Preset picker metrics (design size, scaled at runtime).
constexpr float kArrowWidth    = 18.0f;
constexpr float kArrowGap      = 9.0f;
constexpr float kFormulaMinW   = 84.0f;
constexpr float kFormulaHeight = 26.0f;
}

OrganicChemistryAudioProcessorEditor::OrganicChemistryAudioProcessorEditor (OrganicChemistryAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (canvas);
    addAndMakeVisible (elementBar);
    addChildComponent (testPanel);   // 初始隐藏，点 Test 后展开
    addChildComponent (presetMenu);  // 初始隐藏，点分子式后展开

    presetMenu.onChosen = [this] (int index)
    {
        hidePresetMenu();
        applyPreset (index);
    };
    presetMenu.onDismiss = [this] { hidePresetMenu(); };

    elementBar.onElementChosen = [this] (organic::Element e)
    {
        canvas.setCurrentElement (e);
        canvas.repaint();
    };

    // 反应剖面页签：展开/收起时重排布局，并刷新顶部按钮文案（Clear/Reset）。
    elementBar.onToggleExpanded = [this]
    {
        resized();
        repaint();
    };

    // ADSR 旋钮：调节包络 → 写入参数树（通知宿主 + 同步音频引擎）。
    elementBar.onEnvelopeChanged = [this] (float attack, float decay, float sustain, float release)
    {
        processor.setAdsrParams (attack, decay, sustain, release);
    };

    canvas.onMoleculeChanged = [this]
    {
        // 用户手动改动分子后，抬头不再对应任何预设。
        if (! applyingPreset)
            currentPreset = -1;

        // 分子拓扑变化 → 波形 + 效果参数（自动参数跟随映射，手动参数保留）。
        applyMoleculeToAudio();

        repaint (0, 0, getWidth(), (int) (kTopBarHeight * uiScale()) + 2);
    };

    canvas.setCurrentElement (elementBar.getSelected());

    // 波形预览：显示当前分子 wavetable（静态单周期波形）。
    canvas.previewWaveProvider = [this] { return processor.getPreviewWave(); };

    // 点击画布时提交正在进行的旋钮输入框。
    canvas.onPointerDown = [this] { elementBar.commitKnobEdit(); };

    // 测试面板：拖动滑杆 → 手动覆盖该参数；Reset 恢复全自动。
    testPanel.onParamChanged = [this] (int index, float value)
    {
        processor.setBellParam (index, value);
    };
    testPanel.onResetToAuto = [this]
    {
        const auto def = organic::defaultBellParams();
        for (int i = 0; i < organic::kNumBellParams; ++i)
            processor.setBellParam (i, def[(size_t) i]);
        testPanel.updateFromMapping (def);
    };

    // Bell 音色参数：初始化为复刻默认值。
    testPanel.updateFromMapping (organic::defaultBellParams());

    // 从 DAW 工程恢复上次保存的分子（若有）。restoreMolecule 内部会触发
    // onMoleculeChanged，从而重新派生波形与效果参数；没有保存状态时则为
    // 空分子，应用默认音色。
    {
        const juce::ValueTree saved = processor.getMolecularState();
        if (saved.isValid() && saved.getNumChildren() > 0)
        {
            canvas.restoreMolecule (saved);
        }
        else
        {
            applyMoleculeToAudio();
        }
    }

    // --- Proportional resizing (problem 2) ---
    // The aspect ratio is locked to the design ratio, so dragging the grip
    // scales the whole interface instead of reflowing it.
    constrainer.setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    constrainer.setSizeLimits (kDesignWidth / 2,          // 50 %
                               kDesignHeight / 2,
                               kDesignWidth * 2,          // 200 %
                               kDesignHeight * 2);

    setConstrainer (&constrainer);
    setResizable (true, false);   // resizable, but we supply our own corner grip

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
    addAndMakeVisible (*resizer);

    setSize (kDesignWidth, kDesignHeight);

    startTimerHz (60);

    telemetrySession = std::make_unique<iisaac::telemetry::Session> (
        iisaac::telemetry::forPlugin ("organic-chemistry", JucePlugin_VersionString,
                                     JucePlugin_VersionString, processor.wrapperType));
}

OrganicChemistryAudioProcessorEditor::~OrganicChemistryAudioProcessorEditor()
{
    telemetrySession.reset();
    stopTimer();
    setConstrainer (nullptr);
}

// ---------------------------------------------------------------------------
//  Scale
// ---------------------------------------------------------------------------

float OrganicChemistryAudioProcessorEditor::uiScale() const
{
    // The aspect ratio is locked, so width alone defines the scale.
    return (float) getWidth() / (float) kDesignWidth;
}

// ---------------------------------------------------------------------------
//  Molecule -> audio mapping (with manual overrides)
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessorEditor::applyMoleculeToAudio()
{
    const auto& mol = canvas.getMolecule();

    // 保存分子拓扑 + 映射到音频（处理器侧：波形 / Bell 参数 / 采样 / 静音）。
    processor.setMolecularState (mol.toValueTree());
    processor.applyMolecularStateToAudio();

    // 刷新反应剖面（ADSR）显示（ADSR 与分子解绑，读取处理器当前包络）。
    elementBar.setEnvelopeValues (
        processor.getBellParam ((int) organic::BellParamId::AmpAttack),
        processor.getBellParam ((int) organic::BellParamId::AmpDecay),
        processor.getBellParam ((int) organic::BellParamId::AmpSustain),
        processor.getBellParam ((int) organic::BellParamId::AmpRelease));
}

// ---------------------------------------------------------------------------
//  Animation clock
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f;   // one frame

    // 宿主自动化 / MIDI CC 改变 ADSR 时，刷新旋钮与反应剖面图。
    if (processor.consumeAdsrDirty())
    {
        elementBar.setEnvelopeValues (
            processor.getBellParam ((int) organic::BellParamId::AmpAttack),
            processor.getBellParam ((int) organic::BellParamId::AmpDecay),
            processor.getBellParam ((int) organic::BellParamId::AmpSustain),
            processor.getBellParam ((int) organic::BellParamId::AmpRelease));
    }

    canvas.advanceAnimation (dt);
    // 展开/收起动画进行中时，每帧重排布局让高度平滑过渡。
    if (elementBar.advanceAnimation (dt))
        resized();
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void OrganicChemistryAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float s = uiScale();

    g.fillAll (juce::Colours::white);

    // Website link, top left.
    const auto bounds = getWebsiteBounds();
    const auto textColour = isWebsiteHovered ? juce::Colour (0xFF666666)
                                             : juce::Colours::black;

    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (kWebsiteFontSize * s)));
    g.drawText (kWebsiteText, bounds, juce::Justification::topLeft, false);

    // 版本号标识：紧跟网址右侧，浅灰小号字，方便多轮测试时区分构建。
    const juce::String versionText = juce::String ("v") + JucePlugin_VersionString;
    const auto versionBounds = juce::Rectangle<int> (
        bounds.getRight() + (int) (kVersionMarginX * s),
        bounds.getY(),
        (int) (100.0f * s),
        bounds.getHeight());

    g.setColour (juce::Colour (0xFFA8A8A2));
    g.setFont (juce::Font (juce::FontOptions (kVersionFontSize * s)));
    g.drawText (versionText, versionBounds, juce::Justification::centredLeft, false);

    if (isWebsiteHovered)
    {
        g.setColour (juce::Colour (0xFF666666));
        g.drawLine ((float) bounds.getX(),     (float) bounds.getBottom(),
                    (float) bounds.getRight(), (float) bounds.getBottom(), 1.0f * s);
    }

    // Test 按钮（正式版暂时屏蔽：kTestPanelEnabled = false）。
    if (kTestPanelEnabled)
    {
        const auto tb = getTestButtonBounds();
        const auto colour = isTestHovered ? juce::Colour (0xFF2A6FB0)
                                          : juce::Colour (0xFF9A9A94);

        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (kClearFontSize * s)));
        g.drawText ("Test", tb, juce::Justification::centred, false);

        if (isTestHovered)
            g.drawLine ((float) tb.getX() + 2.0f * s,     (float) tb.getBottom() - 3.0f * s,
                        (float) tb.getRight() - 2.0f * s, (float) tb.getBottom() - 3.0f * s, 1.0f * s);
    }

    paintInfoBar (g);

    // Hairline separator：仅顶部信息栏底部。
    // （底部栏顶部的横线已按要求移除，MOLECULE/REACTION tab 上方不再有分隔线。）
    const float topY = kTopBarHeight * s;

    g.setColour (juce::Colour (0xFFEDEDE8));
    g.drawLine (0.0f, topY, (float) getWidth(), topY, 1.0f * s);
}

void OrganicChemistryAudioProcessorEditor::paintInfoBar (juce::Graphics& g) const
{
    const float s = uiScale();
    const auto& mol = canvas.getMolecule();
    const bool  hasMolecule = mol.heavyAtomCount() > 0;

    const int barHeight = (int) (kTopBarHeight * s);

    // --- Preset cluster:   < |  C6H6 v  |  >   ---
    const auto formulaBounds = getFormulaBounds();
    const auto prevBounds    = getPrevPresetBounds();
    const auto nextBounds    = getNextPresetBounds();

    // Formula (or the "Presets" prompt when the canvas is empty).
    {
        const auto text = headerText();

        const bool active = isFormulaHovered || presetMenuVisible;

        if (active)
        {
            g.setColour (juce::Colour (0x142A6FB0));
            g.fillRoundedRectangle (formulaBounds.toFloat(), 4.0f * s);
        }

        const auto colour = hasMolecule ? juce::Colour (0xFF2A2A28)
                                        : juce::Colour (0xFFB0B0AA);
        g.setColour (active ? juce::Colour (0xFF2A6FB0) : colour);
        g.setFont (juce::Font (juce::FontOptions (kFormulaFontSize * s)));
        g.drawText (text, formulaBounds, juce::Justification::centred, false);

        // 悬停时在下方给一条细下划线，暗示"可点击"，比倒三角安静得多。
        if (active)
        {
            const float y = (float) formulaBounds.getBottom() - 2.0f * s;
            const float inset = 8.0f * s;
            g.setColour (juce::Colour (0x552A6FB0));
            g.drawLine ((float) formulaBounds.getX() + inset, y,
                        (float) formulaBounds.getRight() - inset, y, 1.0f * s);
        }
    }

    // Step arrows.
    {
        g.setFont (juce::Font (juce::FontOptions (kFormulaFontSize * s * 0.85f)));

        g.setColour (isPrevHovered ? juce::Colour (0xFF2A6FB0) : juce::Colour (0xFFC0C0BA));
        g.drawText ("<", prevBounds, juce::Justification::centred, false);

        g.setColour (isNextHovered ? juce::Colour (0xFF2A6FB0) : juce::Colour (0xFFC0C0BA));
        g.drawText (">", nextBounds, juce::Justification::centred, false);
    }

    // Molecular weight, to the right of the cluster in a lighter tone.
    if (hasMolecule)
    {
        g.setColour (juce::Colour (0xFFA8A8A2));
        g.setFont (juce::Font (juce::FontOptions (kDetailFontSize * s)));
        g.drawText (juce::String (mol.molecularWeight(), 2) + " g/mol",
                    juce::Rectangle<int> (nextBounds.getRight() + (int) (10.0f * s),
                                          0, (int) (120.0f * s), barHeight),
                    juce::Justification::centredLeft, false);
    }

    // 右上角按钮：REACTION 下为 Reset（重置 ADSR），否则为 Clear（清空分子）。
    if (elementBar.isExpanded() || hasMolecule)
    {
        const auto cb = getClearButtonBounds();
        const auto colour = isClearHovered ? juce::Colour (0xFF666660)
                                          : juce::Colour (0xFFB0B0AA);

        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (kClearFontSize * s)));
        g.drawText (clearButtonText(), cb, juce::Justification::centred, false);

        if (isClearHovered)
            g.drawLine ((float) cb.getX() + 2.0f * s,     (float) cb.getBottom() - 3.0f * s,
                        (float) cb.getRight() - 2.0f * s, (float) cb.getBottom() - 3.0f * s, 1.0f * s);
    }
}

// ---------------------------------------------------------------------------
//  Preset picker
// ---------------------------------------------------------------------------

juce::String OrganicChemistryAudioProcessorEditor::headerText() const
{
    const auto& mol = canvas.getMolecule();

    if (mol.heavyAtomCount() == 0)
        return kPresetHint;

    return mol.formula();
}

juce::Rectangle<int> OrganicChemistryAudioProcessorEditor::getFormulaBounds() const
{
    const float s = uiScale();
    const juce::Font font { juce::FontOptions (kFormulaFontSize * s) };

    int w = font.getStringWidth (headerText()) + (int) (26.0f * s);
    w = juce::jmax (w, (int) (kFormulaMinW * s));

    const int h = (int) (kFormulaHeight * s);
    const int barHeight = (int) (kTopBarHeight * s);

    return juce::Rectangle<int> (getWidth() / 2 - w / 2, (barHeight - h) / 2, w, h);
}

juce::Rectangle<int> OrganicChemistryAudioProcessorEditor::getPrevPresetBounds() const
{
    const float s = uiScale();
    const auto f = getFormulaBounds();
    const int w = (int) (kArrowWidth * s);

    return juce::Rectangle<int> (f.getX() - (int) (kArrowGap * s) - w,
                                 f.getY(), w, f.getHeight());
}

juce::Rectangle<int> OrganicChemistryAudioProcessorEditor::getNextPresetBounds() const
{
    const float s = uiScale();
    const auto f = getFormulaBounds();

    return juce::Rectangle<int> (f.getRight() + (int) (kArrowGap * s),
                                 f.getY(), (int) (kArrowWidth * s), f.getHeight());
}

void OrganicChemistryAudioProcessorEditor::showPresetMenu()
{
    const float s = uiScale();

    presetMenu.setUiScale (s);
    presetMenu.setCurrentIndex (currentPreset);
    presetMenu.setAnchorY ((int) ((kTopBarHeight + 8.0f) * s));
    presetMenu.setBounds (getLocalBounds());
    presetMenu.setVisible (true);
    presetMenu.toFront (false);

    presetMenuVisible = true;
    repaint();
}

void OrganicChemistryAudioProcessorEditor::hidePresetMenu()
{
    presetMenu.setVisible (false);
    presetMenuVisible = false;
    repaint();
}

void OrganicChemistryAudioProcessorEditor::applyPreset (int index)
{
    const auto& presets = organic::moleculePresets();
    if (presets.empty())
        return;

    const int count = (int) presets.size();
    index = ((index % count) + count) % count;   // wrap both directions

    // restoreMolecule fires onMoleculeChanged, which would normally clear the
    // selection; the guard keeps the header showing which preset is loaded.
    applyingPreset = true;
    canvas.restoreMolecule (organic::presetValueTree (index));
    applyingPreset = false;

    currentPreset = index;
    repaint();
}

void OrganicChemistryAudioProcessorEditor::stepPreset (int delta)
{
    // No preset loaded yet: stepping forward starts at the first entry,
    // stepping backward at the last.
    if (currentPreset < 0)
    {
        applyPreset (delta > 0 ? 0 : (int) organic::moleculePresets().size() - 1);
        return;
    }

    applyPreset (currentPreset + delta);
}

void OrganicChemistryAudioProcessorEditor::switchToMolecule()
{
    if (elementBar.isExpanded())
    {
        elementBar.setExpanded (false);
        resized();
        repaint();   // 刷新右上角按钮文案（Reset → Clear）
    }
}

void OrganicChemistryAudioProcessorEditor::resetAdsrToDefault()
{
    const auto def = organic::defaultBellParams();
    processor.setAdsrParams (
        def[(size_t) organic::BellParamId::AmpAttack],
        def[(size_t) organic::BellParamId::AmpDecay],
        def[(size_t) organic::BellParamId::AmpSustain],
        def[(size_t) organic::BellParamId::AmpRelease]);
}

juce::String OrganicChemistryAudioProcessorEditor::clearButtonText() const
{
    return elementBar.isExpanded() ? "Reset" : "Clear";
}

void OrganicChemistryAudioProcessorEditor::resized()
{
    const float s = uiScale();

    // Propagate the scale so children can size their own strokes and fonts.
    canvas.setUiScale (s);
    elementBar.setUiScale (s);
    testPanel.setUiScale (s);

    auto area = getLocalBounds();
    area.removeFromTop ((int) (kTopBarHeight * s));

    // 展开动画：元素栏高度从底部条平滑过渡到占满整个编辑区。
    const int bottomH = (int) (kBottomBarHeight * s);
    const int fullH   = area.getHeight();
    const int barH    = bottomH + (int) ((fullH - bottomH) * elementBar.getExpandAnim());

    elementBar.setBounds (area.removeFromBottom (barH));

    // 收起（MOLECULE 模式）时画布向下多延伸「页签条」的高度（20 设计像素），
    // 盖住页签条所在的横向区域，使分子编辑区不被白条占位；展开动画期间该
    // 重叠量随 expandAnim 平滑归零。
    const int overlap = (int) ((kBottomBarHeight - kChipRowHeight) * s
                               * (1.0f - elementBar.getExpandAnim()));

    // 测试面板：仅在收起且可见时占据画布顶部。
    if (testPanelVisible && ! elementBar.isExpanded())
    {
        const int panelH = (int) (170.0f * s);
        testPanel.setBounds (area.removeFromTop (panelH));
    }
    else
    {
        testPanel.setBounds (juce::Rectangle<int> ());
    }

    // 画布底部额外向下延伸 overlap，覆盖元素栏透明的页签条（元素栏绘制在
    // 其上方，命中测试仍只拦截页签与色块行，其余透传给画布）。
    canvas.setBounds (area.getX(), area.getY(), area.getWidth(),
                      area.getHeight() + overlap);

    if (presetMenuVisible)
    {
        presetMenu.setUiScale (s);
        presetMenu.setAnchorY ((int) ((kTopBarHeight + 8.0f) * s));
        presetMenu.setBounds (getLocalBounds());
    }

    if (resizer != nullptr)
    {
        const int rs = (int) (kResizerSize * s);
        resizer->setBounds (getWidth() - rs, getHeight() - rs, rs, rs);
    }
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------

juce::Rectangle<int> OrganicChemistryAudioProcessorEditor::getWebsiteBounds() const
{
    const float s = uiScale();
    const juce::Font font { juce::FontOptions (kWebsiteFontSize * s) };

    return juce::Rectangle<int> ((int) (kWebsiteMarginX * s),
                                 (int) (kWebsiteMarginY * s),
                                 font.getStringWidth (kWebsiteText),
                                 (int) font.getHeight());
}

juce::Rectangle<int> OrganicChemistryAudioProcessorEditor::getClearButtonBounds() const
{
    const float s = uiScale();
    const juce::Font font { juce::FontOptions (kClearFontSize * s) };
    const int w = font.getStringWidth (clearButtonText()) + (int) (16.0f * s);

    return juce::Rectangle<int> (getWidth() - w - (int) (14.0f * s),
                                 (int) (12.0f * s),
                                 w,
                                 (int) (22.0f * s));
}

juce::Rectangle<int> OrganicChemistryAudioProcessorEditor::getTestButtonBounds() const
{
    const float s = uiScale();
    const juce::Font font { juce::FontOptions (kClearFontSize * s) };
    const int w = font.getStringWidth ("Test") + (int) (16.0f * s);

    // 位于 Clear 按钮左侧，始终显示（不依赖分子是否为空）。
    const auto clear = getClearButtonBounds();
    return juce::Rectangle<int> (clear.getX() - w - (int) (8.0f * s),
                                 (int) (12.0f * s),
                                 w,
                                 (int) (22.0f * s));
}

void OrganicChemistryAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    // 点击编辑器空白区时，提交正在进行的旋钮输入。
    elementBar.commitKnobEdit();

    if (kTestPanelEnabled && getTestButtonBounds().contains (event.getPosition()))
    {
        testPanelVisible = ! testPanelVisible;
        testPanel.setVisible (testPanelVisible);
        resized();
        repaint();
        return;
    }

    if (getWebsiteBounds().contains (event.getPosition()))
    {
        juce::URL (kWebsiteUrl).launchInDefaultBrowser();
        return;
    }

    // Preset picker: arrows step, the formula itself drops the list.
    // 切到预设时自动回到 MOLECULE 页签。
    if (getPrevPresetBounds().contains (event.getPosition()))
    {
        switchToMolecule();
        stepPreset (-1);
        return;
    }

    if (getNextPresetBounds().contains (event.getPosition()))
    {
        switchToMolecule();
        stepPreset (1);
        return;
    }

    if (getFormulaBounds().contains (event.getPosition()))
    {
        switchToMolecule();
        if (presetMenuVisible)
            hidePresetMenu();
        else
            showPresetMenu();
        return;
    }

    // 右上角按钮：REACTION 下重置 ADSR，否则清空分子。
    if ((elementBar.isExpanded() || canvas.getMolecule().heavyAtomCount() > 0)
        && getClearButtonBounds().contains (event.getPosition()))
    {
        if (elementBar.isExpanded())
            resetAdsrToDefault();
        else
            canvas.clearMolecule();
        repaint();
    }
}

void OrganicChemistryAudioProcessorEditor::mouseMove (const juce::MouseEvent& event)
{
    const bool websiteHovered = getWebsiteBounds().contains (event.getPosition());
    const bool clearHovered   = (elementBar.isExpanded() || canvas.getMolecule().heavyAtomCount() > 0)
                                && getClearButtonBounds().contains (event.getPosition());
    const bool testHovered    = kTestPanelEnabled && getTestButtonBounds().contains (event.getPosition());
    const bool formulaHovered = getFormulaBounds().contains (event.getPosition());
    const bool prevHovered    = getPrevPresetBounds().contains (event.getPosition());
    const bool nextHovered    = getNextPresetBounds().contains (event.getPosition());

    if (websiteHovered != isWebsiteHovered
        || clearHovered != isClearHovered
        || testHovered != isTestHovered
        || formulaHovered != isFormulaHovered
        || prevHovered != isPrevHovered
        || nextHovered != isNextHovered)
    {
        isWebsiteHovered = websiteHovered;
        isClearHovered   = clearHovered;
        isTestHovered    = testHovered;
        isFormulaHovered = formulaHovered;
        isPrevHovered    = prevHovered;
        isNextHovered    = nextHovered;

        setMouseCursor ((websiteHovered || clearHovered || testHovered
                         || formulaHovered || prevHovered || nextHovered)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void OrganicChemistryAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    if (isWebsiteHovered || isClearHovered || isTestHovered
        || isFormulaHovered || isPrevHovered || isNextHovered)
    {
        isWebsiteHovered = false;
        isClearHovered   = false;
        isTestHovered    = false;
        isFormulaHovered = false;
        isPrevHovered    = false;
        isNextHovered    = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}
