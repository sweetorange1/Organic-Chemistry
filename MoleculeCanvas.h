#pragma once

#include <JuceHeader.h>
#include "MoleculeModel.h"

// ============================================================
//  Molecule drawing canvas
//
//  Interaction:
//    - Left click on empty space  -> add current element
//    - Left click on a heavy atom -> grow a new atom outwards from it
//    - Left click on a bond       -> cycle single / double / triple
//    - Right click on a heavy atom-> delete it (largest fragment survives)
//    - Hover                      -> atom or bond highlight
//
//  Coordinate spaces:
//    The molecule lives in *model space*, which is unbounded. The canvas
//    fits it into the visible area with an auto-computed viewScale, so the
//    drawing shrinks as the molecule grows. All hit testing converts screen
//    coordinates back into model space first.
//
//  Visual language: light clinical minimal, thin strokes, soft fills,
//  slow breathing motion so the picture always feels alive.
// ============================================================

namespace organic
{

class MoleculeCanvas : public juce::Component
{
public:
    MoleculeCanvas();
    ~MoleculeCanvas() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /** Driven by the editor's single animation clock. */
    void advanceAnimation (float dt);

    void setCurrentElement (Element e) { currentElement = e; }
    Element getCurrentElement() const { return currentElement; }

    /** Global UI scale from window resizing (problem 2). */
    void setUiScale (float s) { uiScale = s; }

    void clearMolecule();

    /** Replace the current molecule with the one serialised in @p tree.

        Used to restore a saved project when the editor opens. After restoring,
        the onMoleculeChanged callback fires so the audio mapping re-derives.
        Invalid trees are ignored (molecule left untouched). */
    void restoreMolecule (const juce::ValueTree& tree);

    const Molecule& getMolecule() const { return molecule; }

    std::function<void()> onMoleculeChanged;

    /** 波形预览数据提供者：返回一组 -1..1 的采样，供右上角波形窗口绘制。

        由编辑器注入，指向处理器的 getPreviewWave()。无音频输出时返回分子
        wavetable 波形，有输出时返回输出信号波形。 */
    std::function<std::vector<float>()> previewWaveProvider;

private:
    // --- Coordinate transforms (model space <-> screen space) ---

    /** Effective drawing scale: window scale times the auto-fit view scale. */
    float drawScale() const { return uiScale * viewScale; }

    juce::Point<float> modelToScreen (juce::Point<float> p) const;
    juce::Point<float> screenToModel (juce::Point<float> p) const;

    /** Recompute viewScale so the molecule's bounding box fits the canvas.
        Approaches the target smoothly to avoid visual jumps. */
    void updateViewScale (float dt);

    juce::Point<float> canvasCentre() const;

    // --- Painting ---

    void paintBonds (juce::Graphics& g) const;
    void paintAtoms (juce::Graphics& g) const;
    void paintEmptyHint (juce::Graphics& g) const;
    void paintStructuralFormula (juce::Graphics& g) const;
    void paintGhost (juce::Graphics& g) const;
    void paintWavePreview (juce::Graphics& g) const;

    /** Recompute dragValid / dragTargetAtom from the current pointer. */
    void updateDragValidity();

    Molecule molecule;
    Element  currentElement = Element::Carbon;

    // Window scale (1.0 at the design size of 800x520)
    float uiScale = 1.0f;

    // Auto-fit scale so a growing molecule stays inside the canvas
    float viewScale = 1.0f;
    float viewScaleTarget = 1.0f;

    // Model-space centre of the molecule, tracked for smooth panning
    juce::Point<float> modelCentre { 0.0f, 0.0f };

    int   hoveredAtom = -1;
    int   hoveredBond = -1;
    float hoverAnim = 0.0f;

    // Drag-to-place / drag-to-connect interaction state.
    bool dragActive = false;                 // left button is being held
    int  dragSourceAtom = -1;                // heavy atom pressed on, -1 = empty space
    int  dragTargetAtom = -1;                // heavy atom currently hovered for bonding
    bool dragValid = false;                  // release position is placeable / bondable
    juce::Point<float> dragCurrentModel;     // ghost position in model space

    juce::Point<float> ripplePos;   // screen space
    float rippleAnim = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoleculeCanvas)
};

} // namespace organic
