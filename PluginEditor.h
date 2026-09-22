#pragma once

#include <JuceHeader.h>
#include <array>

#include "PluginProcessor.h"
#include "MoleculeCanvas.h"
#include "ElementBar.h"
#include "TestPanel.h"
#include "PresetMenu.h"
#include "shared/IisaacTelemetry.h"

// ============================================================
//  "Organic Chemistry" - synthesiser editor
//
//  v0.3.0 - molecular structure editor
//     - top    : website link, molecular formula, weight, clear button
//     - centre : molecule canvas (dish), left click adds, right click removes,
//                clicking a bond cycles single / double / triple
//     - bottom : C / O / N / S / P element bar
//     - corner : resize grip, the whole UI scales proportionally
//
//  A single 60 Hz Timer drives every child animation, which keeps the
//  motion coherent and avoids competing repaint clocks.
//
//  No audio is produced at this stage: topology changes only refresh the UI.
// ============================================================
class OrganicChemistryAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    OrganicChemistryAudioProcessorEditor (OrganicChemistryAudioProcessor&);
    ~OrganicChemistryAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    // Design size: every layout constant below is expressed at this size and
    // multiplied by uiScale when the window is resized.
    static constexpr int kDesignWidth  = 820;
    static constexpr int kDesignHeight = 560;

private:
    void timerCallback() override;

    /** Current window scale relative to the design size. */
    float uiScale() const;

    juce::Rectangle<int> getWebsiteBounds() const;
    juce::Rectangle<int> getClearButtonBounds() const;
    juce::Rectangle<int> getTestButtonBounds() const;

    // --- Preset picker in the header ---

    /** Text shown in the header: the molecular formula, or a placeholder
        prompting the user to pick a preset when the canvas is empty. */
    juce::String headerText() const;

    /** Clickable area around the formula that opens the preset list. */
    juce::Rectangle<int> getFormulaBounds() const;
    juce::Rectangle<int> getPrevPresetBounds() const;
    juce::Rectangle<int> getNextPresetBounds() const;

    /** Show or hide the self-drawn preset panel. */
    void showPresetMenu();
    void hidePresetMenu();

    /** Load preset @p index onto the canvas (wraps around the library). */
    void applyPreset (int index);

    /** Step the preset selection by +1 / -1, wrapping at both ends. */
    void stepPreset (int delta);

    void paintInfoBar (juce::Graphics& g) const;

    /** 把当前分子重新映射到音频：波形始终跟随分子，效果参数仅在
        「自动」时被映射覆盖，「手动」参数保留用户设定值。 */
    void applyMoleculeToAudio();

    OrganicChemistryAudioProcessor& processor;

    organic::MoleculeCanvas canvas;
    organic::ElementBar     elementBar;
    organic::TestPanel      testPanel;
    organic::PresetMenu     presetMenu;

    // 当前生效的完整参数集：自动参数 = 映射值，手动参数 = 用户设定值。
    organic::SynthesisParameters currentParams {};
    // 每个参数是否被手动覆盖（索引同 organic::ParamId）。
    std::array<bool, organic::kNumParams> manualParams {};

    // Bottom-right resize grip plus an aspect-ratio lock, so the UI can only
    // scale proportionally (problem 2).
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    bool isWebsiteHovered = false;
    bool isClearHovered   = false;
    bool isTestHovered    = false;
    bool testPanelVisible = false;

    // Preset picker state. currentPreset is -1 whenever the molecule on the
    // canvas no longer corresponds to a library entry, i.e. as soon as the
    // user edits it by hand.
    bool isFormulaHovered = false;
    bool isPrevHovered    = false;
    bool isNextHovered    = false;
    int  currentPreset    = -1;
    bool applyingPreset   = false;   // guards the change callback
    bool presetMenuVisible = false;

    std::unique_ptr<iisaac::telemetry::Session> telemetrySession;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrganicChemistryAudioProcessorEditor)
};
