#include "MoleculeCanvas.h"

#include <cmath>

namespace organic
{

namespace
{
// Light clinical palette
const juce::Colour kBondHeavy    { 0xFF5A5A56 };
const juce::Colour kBondHydrogen { 0xFFC8C8C2 };
const juce::Colour kHintText     { 0xFFAAAAA4 };
const juce::Colour kFormulaText  { 0xFF6E6E68 };

// Pixel margin around the molecule when fitting it into the canvas.
constexpr float kFitPadding = 24.0f;
}

MoleculeCanvas::MoleculeCanvas()
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MoleculeCanvas::resized()
{
    // Layout is derived from bounds at paint time; the auto-fit scale settles
    // on its own over the next few frames.
}

juce::Point<float> MoleculeCanvas::canvasCentre() const
{
    return getLocalBounds().toFloat().getCentre();
}

// ---------------------------------------------------------------------------
//  Coordinate transforms
// ---------------------------------------------------------------------------

juce::Point<float> MoleculeCanvas::modelToScreen (juce::Point<float> p) const
{
    return canvasCentre() + (p - modelCentre) * drawScale();
}

juce::Point<float> MoleculeCanvas::screenToModel (juce::Point<float> p) const
{
    const float s = drawScale();
    if (s < 0.0001f)
        return modelCentre;

    return modelCentre + (p - canvasCentre()) / s;
}

// ---------------------------------------------------------------------------
//  Auto-fit  (problem 3: molecule grows -> view scales down)
// ---------------------------------------------------------------------------

void MoleculeCanvas::updateViewScale (float dt)
{
    const auto area = getLocalBounds().toFloat();

    // Usable drawing area: the full canvas with a small margin on each side.
    const float usableW = juce::jmax (1.0f, area.getWidth()  - kFitPadding * 2.0f * uiScale);
    const float usableH = juce::jmax (1.0f, area.getHeight() - kFitPadding * 2.0f * uiScale);

    if (molecule.atoms().empty())
    {
        viewScaleTarget = 1.0f;
        modelCentre = { 0.0f, 0.0f };
    }
    else
    {
        const auto box = molecule.boundingBox();

        // Track the molecule's centre so it stays visually centred.
        modelCentre = box.getCentre();

        // Fit the box into the rectangular canvas. Never scale above 1.0, so
        // small molecules keep their natural size.
        if ((box.getWidth() > 1.0f || box.getHeight() > 1.0f) && uiScale > 0.0001f)
        {
            // usable is already in screen px, so divide out uiScale to get the
            // pure view component of the transform.
            const float fitW = (usableW / uiScale) / box.getWidth();
            const float fitH = (usableH / uiScale) / box.getHeight();
            viewScaleTarget = juce::jlimit (0.22f, 1.0f, juce::jmin (fitW, fitH));
        }
        else
        {
            viewScaleTarget = 1.0f;
        }
    }

    // Ease towards the target so growth never snaps.
    viewScale += (viewScaleTarget - viewScale) * juce::jmin (1.0f, dt * 0.07f);
}

// ---------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------

void MoleculeCanvas::paint (juce::Graphics& g)
{
    if (molecule.atoms().empty())
    {
        paintEmptyHint (g);
        paintGhost (g);
        paintWavePreview (g);
        return;
    }

    paintBonds (g);
    paintAtoms (g);
    paintStructuralFormula (g);
    paintGhost (g);

    if (rippleAnim < 1.0f)
    {
        const float t = rippleAnim;
        const float r = (8.0f + t * 34.0f) * uiScale;
        const float alpha = (1.0f - t) * 0.30f;

        g.setColour (juce::Colour (elementInfo (currentElement).colour).withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (ripplePos),
                       1.2f * uiScale);
    }

    // 右上角波形预览窗口（无论分子是否为空都显示）。
    paintWavePreview (g);
}

void MoleculeCanvas::paintBonds (juce::Graphics& g) const
{
    const auto& atoms = molecule.atoms();
    const float s = drawScale();

    for (size_t bi = 0; bi < molecule.bonds().size(); ++bi)
    {
        const auto& b = molecule.bonds()[bi];
        const auto& A = atoms[(size_t) b.a];
        const auto& B = atoms[(size_t) b.b];

        const bool involvesH = A.isHydrogen || B.isHydrogen;

        const float anim = juce::jmin (A.spawnAnim, B.spawnAnim);
        if (anim < 0.02f)
            continue;

        const auto pa = modelToScreen (A.pos);
        const auto pb = modelToScreen (B.pos);

        auto dir = pb - pa;
        const float len = dir.getDistanceFromOrigin();
        if (len < 0.01f)
            continue;
        dir /= len;

        // Perpendicular offset direction, used to fan out multiple bond lines.
        const juce::Point<float> normal { -dir.y, dir.x };

        // Trim the line back to each atom's edge.
        const float rA = elementInfo (A.element).radius * s * (0.55f + 0.45f * A.spawnAnim);
        const float rB = elementInfo (B.element).radius * s * (0.55f + 0.45f * B.spawnAnim);

        const auto p1 = pa + dir * (rA - 1.0f * uiScale);
        const auto p2 = pb - dir * (rB - 1.0f * uiScale);

        const bool isHovered = ((int) bi == hoveredBond);
        const float hoverBoost = isHovered ? hoverAnim : 0.0f;

        auto colour = involvesH ? kBondHydrogen : kBondHeavy;
        if (hoverBoost > 0.01f)
            colour = colour.interpolatedWith (juce::Colour (0xFF2A6FB0), hoverBoost * 0.75f);

        const float baseThickness = (involvesH ? 1.0f : 1.6f) * uiScale
                                        * (1.0f + hoverBoost * 0.35f);
        const float alpha = anim * (involvesH ? 0.72f : 0.9f);

        g.setColour (colour.withAlpha (alpha));

        // Bond order rendering: 1 line, 2 parallel lines, 3 lines.
        // orderAnim eases the gap open when the order changes.
        const int order = juce::jlimit (1, 3, b.order);
        const float gap = 3.2f * s * b.orderAnim;

        if (order == 1)
        {
            g.drawLine (p1.x, p1.y, p2.x, p2.y, baseThickness);
        }
        else if (order == 2)
        {
            const auto o = normal * gap;
            g.drawLine (p1.x + o.x, p1.y + o.y, p2.x + o.x, p2.y + o.y, baseThickness * 0.85f);
            g.drawLine (p1.x - o.x, p1.y - o.y, p2.x - o.x, p2.y - o.y, baseThickness * 0.85f);
        }
        else
        {
            const auto o = normal * (gap * 1.35f);
            g.drawLine (p1.x, p1.y, p2.x, p2.y, baseThickness * 0.8f);
            g.drawLine (p1.x + o.x, p1.y + o.y, p2.x + o.x, p2.y + o.y, baseThickness * 0.7f);
            g.drawLine (p1.x - o.x, p1.y - o.y, p2.x - o.x, p2.y - o.y, baseThickness * 0.7f);
        }

        // Hovering a bond hints that it is clickable.
        if (hoverBoost > 0.01f && ! involvesH)
        {
            const int maxOrder = molecule.maxOrderForBond ((int) bi);
            if (maxOrder > 1)
            {
                const auto mid = (p1 + p2) * 0.5f;
                g.setColour (juce::Colour (0xFF2A6FB0).withAlpha (0.16f * hoverBoost));
                g.fillEllipse (juce::Rectangle<float> (13.0f * uiScale * 2.0f,
                                                       13.0f * uiScale * 2.0f).withCentre (mid));
            }
        }
    }
}

void MoleculeCanvas::paintAtoms (juce::Graphics& g) const
{
    const auto& atoms = molecule.atoms();
    const float s = drawScale();

    // Hydrogens first so heavy atoms sit on top.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool drawHydrogen = (pass == 0);

        for (size_t i = 0; i < atoms.size(); ++i)
        {
            const auto& a = atoms[i];
            if (a.isHydrogen != drawHydrogen)
                continue;
            if (a.spawnAnim < 0.02f)
                continue;

            const auto& info = elementInfo (a.element);
            const auto colour = juce::Colour (info.colour);

            const float scale = 0.55f + 0.45f * a.spawnAnim;
            const float alpha = a.spawnAnim;

            const bool isHovered = ((int) i == hoveredAtom);
            const float hoverBoost = isHovered ? hoverAnim : 0.0f;

            const float r = info.radius * s * scale * (1.0f + hoverBoost * 0.10f);
            const auto centre = modelToScreen (a.pos);
            const auto rect = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

            if (hoverBoost > 0.01f)
            {
                g.setColour (colour.withAlpha (0.14f * hoverBoost));
                g.fillEllipse (rect.expanded ((6.0f + hoverBoost * 3.0f) * uiScale));
            }

            const float fillAlpha = (a.isHydrogen ? 0.42f : 0.88f) * alpha;
            g.setColour (colour.withAlpha (fillAlpha));
            g.fillEllipse (rect);

            g.setColour (colour.withAlpha ((a.isHydrogen ? 0.35f : 0.65f) * alpha));
            g.drawEllipse (rect, 1.0f * uiScale);

            // Only label atoms that are large enough to read.
            if (r > 6.0f)
            {
                const float fontSize = a.isHydrogen ? r * 1.05f : r * 1.0f;
                g.setColour (juce::Colours::white.withAlpha ((a.isHydrogen ? 0.85f : 0.95f) * alpha));
                g.setFont (juce::Font (juce::FontOptions (fontSize).withStyle ("Bold")));
                g.drawText (info.symbol, rect, juce::Justification::centred, false);
            }
        }
    }
}

void MoleculeCanvas::paintEmptyHint (juce::Graphics& g) const
{
    const auto area = getLocalBounds().toFloat();
    const auto& info = elementInfo (currentElement);

    g.setColour (kHintText);
    g.setFont (juce::Font (juce::FontOptions (13.5f * uiScale)));
    g.drawText (juce::String ("Click anywhere to place ") + info.symbol,
                area.withHeight (24.0f * uiScale)
                    .withCentre ({ area.getCentreX(), area.getCentreY() - 6.0f * uiScale }),
                juce::Justification::centred, false);

    g.setColour (kHintText.withAlpha (0.65f));
    g.setFont (juce::Font (juce::FontOptions (11.5f * uiScale)));
    g.drawText ("Hydrogens fill in automatically  -  right click removes an atom",
                area.withHeight (20.0f * uiScale)
                    .withCentre ({ area.getCentreX(), area.getCentreY() + 16.0f * uiScale }),
                juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
//  Structural formula overlay  (problem 6)
// ---------------------------------------------------------------------------

void MoleculeCanvas::paintStructuralFormula (juce::Graphics& g) const
{
    if (molecule.heavyAtomCount() == 0)
        return;

    const auto text = molecule.structuralFormula();
    const auto name = molecule.commonName();
    const auto area = getLocalBounds().toFloat();

    const float pad = 16.0f * uiScale;
    const float labelSize = 9.5f * uiScale;
    const float valueSize = 14.0f * uiScale;
    const float nameSize = 16.0f * uiScale;

    // Small caption above the formula, kept very light so it never competes
    // with the molecule itself.
    g.setColour (kFormulaText.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (labelSize)));
    g.drawText ("STRUCTURE",
                juce::Rectangle<float> (pad,
                                        area.getBottom() - pad - valueSize - labelSize - 4.0f * uiScale,
                                        area.getWidth() * 0.8f,
                                        labelSize + 2.0f * uiScale),
                juce::Justification::bottomLeft, false);

    g.setColour (kFormulaText);
    g.setFont (juce::Font (juce::FontOptions (valueSize)));
    g.drawText (text,
                juce::Rectangle<float> (pad,
                                        area.getBottom() - pad - valueSize,
                                        area.getWidth() - pad * 2.0f,
                                        valueSize + 2.0f * uiScale),
                juce::Justification::bottomLeft, false);

    // Common name above the structure when the molecule is recognised.
    if (name.isNotEmpty())
    {
        const float nameBottom = area.getBottom() - pad - valueSize - labelSize - 10.0f * uiScale;

        g.setColour (kFormulaText.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (labelSize)));
        g.drawText ("COMMON NAME",
                    juce::Rectangle<float> (pad,
                                            nameBottom - nameSize - labelSize - 2.0f * uiScale,
                                            area.getWidth() * 0.8f,
                                            labelSize + 2.0f * uiScale),
                    juce::Justification::bottomLeft, false);

        g.setColour (juce::Colour (0xFF2A2A28));
        g.setFont (juce::Font (juce::FontOptions (nameSize).withStyle ("Bold")));
        g.drawText (name,
                    juce::Rectangle<float> (pad,
                                            nameBottom - nameSize,
                                            area.getWidth() - pad * 2.0f,
                                            nameSize + 2.0f * uiScale),
                    juce::Justification::bottomLeft, false);
    }
}

// ---------------------------------------------------------------------------
//  Drag ghost  (drag-to-place / drag-to-connect preview)
// ---------------------------------------------------------------------------

void MoleculeCanvas::paintGhost (juce::Graphics& g) const
{
    if (! dragActive)
        return;

    const auto& info = elementInfo (currentElement);
    const float s = drawScale();

    const auto centre = modelToScreen (dragCurrentModel);
    const float r = info.radius * s;

    // Grey the ghost out when the release position is not placeable / bondable.
    const juce::Colour base = dragValid ? juce::Colour (info.colour)
                                        : juce::Colour (0xFFB0B0B0);

    // Preview the bond that will form when dragging outwards from an atom.
    if (dragSourceAtom >= 0)
    {
        const auto src = modelToScreen (molecule.atoms()[(size_t) dragSourceAtom].pos);
        g.setColour (base.withAlpha (0.45f));
        g.drawLine (src.x, src.y, centre.x, centre.y, 1.4f * uiScale);
    }

    // Highlight the bondable target atom sitting under the pointer.
    if (dragTargetAtom >= 0 && dragValid)
    {
        const auto tp = modelToScreen (molecule.atoms()[(size_t) dragTargetAtom].pos);
        const float tr = elementInfo (molecule.atoms()[(size_t) dragTargetAtom].element).radius * s;

        g.setColour (juce::Colour (0xFF2A6FB0).withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (tr * 2.0f + 8.0f * uiScale,
                                               tr * 2.0f + 8.0f * uiScale).withCentre (tp));
    }

    // The ghost atom itself.
    g.setColour (base.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));

    g.setColour (base.withAlpha (0.85f));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre),
                   1.2f * uiScale);

    if (r > 6.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (r).withStyle ("Bold")));
        g.drawText (info.symbol,
                    juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre),
                    juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------
//  Wave preview  (top-right waveform window)
// ---------------------------------------------------------------------------

void MoleculeCanvas::paintWavePreview (juce::Graphics& g) const
{
    const float s = uiScale;

    // 设计尺寸 180×50，随 uiScale 等比缩放。
    const float w = 180.0f * s;
    const float h = 50.0f * s;
    const float margin = 12.0f * s;

    const auto bounds = juce::Rectangle<float> (
        (float) getWidth() - w - margin, margin, w, h);

    // 背景 + 细边框。
    g.setColour (juce::Colour (0xFFF7F7F3));
    g.fillRect (bounds);
    g.setColour (juce::Colour (0xFFDCDCD5));
    g.drawRect (bounds, 1.0f * s);

    // 水平零线。
    g.setColour (juce::Colour (0xFFE8E8E2));
    g.drawLine (bounds.getX(), bounds.getCentreY(),
                bounds.getRight(), bounds.getCentreY(), 1.0f * s);

    // 波形曲线。
    if (previewWaveProvider == nullptr)
        return;

    const auto wave = previewWaveProvider();
    const int count = (int) wave.size();
    if (count < 2)
        return;

    const float midY = bounds.getCentreY();
    const float amp = h * 0.40f;
    const float x0 = bounds.getX() + 3.0f * s;
    const float x1 = bounds.getRight() - 3.0f * s;

    juce::Path path;
    path.startNewSubPath (x0, midY - wave[0] * amp);

    for (int i = 1; i < count; ++i)
    {
        const float x = x0 + (float) i * (x1 - x0) / (float) (count - 1);
        const float y = midY - wave[(size_t) i] * amp;
        path.lineTo (x, y);
    }

    g.setColour (juce::Colour (0xFF2A2A28));
    g.strokePath (path, juce::PathStrokeType (1.3f * s));
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------

void MoleculeCanvas::mouseDown (const juce::MouseEvent& e)
{
    const auto screenPos = e.position;
    const auto modelPos = screenToModel (screenPos);
    const float s = drawScale();

    const int atomHit = molecule.hitTestAtom (modelPos, 1.0f);

    // --- Right click: break a bond, or remove a heavy atom ---
    if (e.mods.isRightButtonDown())
    {
        bool changed = false;

        if (atomHit >= 0)
        {
            changed = molecule.removeAtom (atomHit);
        }
        else
        {
            // Tolerance is expressed in model space, so widen it when zoomed out.
            const float tol = (s > 0.0001f) ? (8.0f * uiScale / s) : 8.0f;
            const int bondHit = molecule.hitTestBond (modelPos, tol);
            changed = (bondHit >= 0) && molecule.removeBond (bondHit);
        }

        if (changed)
        {
            hoveredAtom = -1;
            hoveredBond = -1;

            if (onMoleculeChanged != nullptr)
                onMoleculeChanged();
            repaint();
        }
        return;
    }

    if (! e.mods.isLeftButtonDown())
        return;

    // --- Left click on a bond: cycle its order ---
    if (atomHit < 0)
    {
        // Tolerance is expressed in model space, so widen it when zoomed out.
        const float tol = (s > 0.0001f) ? (8.0f * uiScale / s) : 8.0f;
        const int bondHit = molecule.hitTestBond (modelPos, tol);

        if (bondHit >= 0)
        {
            if (molecule.cycleBondOrder (bondHit))
            {
                if (onMoleculeChanged != nullptr)
                    onMoleculeChanged();
                repaint();
            }
            return;
        }
    }

    // --- Left press on empty space or a heavy atom: begin a drag ---
    // The ghost follows the pointer; the atom / bond is committed on mouse-up.
    const bool hitHeavy = atomHit >= 0 && ! molecule.atoms()[(size_t) atomHit].isHydrogen;

    dragActive       = true;
    dragSourceAtom   = hitHeavy ? atomHit : -1;
    dragTargetAtom   = -1;
    dragCurrentModel = modelPos;

    hoveredAtom = -1;
    hoveredBond = -1;

    updateDragValidity();

    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    repaint();
}

void MoleculeCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragActive)
        return;

    dragCurrentModel = screenToModel (e.position);
    updateDragValidity();
    repaint();
}

void MoleculeCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (! dragActive)
        return;

    dragActive = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);

    const auto modelPos = screenToModel (e.position);

    bool changed = false;

    if (dragSourceAtom < 0)
    {
        // Pressed on empty space: place the element where the pointer lands.
        changed = (molecule.addAtom (currentElement, modelPos) >= 0);
    }
    else
    {
        const int atomHit = molecule.hitTestAtom (modelPos, 1.0f);

        if (atomHit >= 0 && ! molecule.atoms()[(size_t) atomHit].isHydrogen
            && atomHit != dragSourceAtom)
        {
            // Released on another heavy atom: bond them together.
            changed = molecule.connectAtoms (dragSourceAtom, atomHit);
        }
        else if (molecule.canGrowFrom (dragSourceAtom))
        {
            // Released on empty space: grow a new atom outwards from the source.
            const auto anchor = molecule.atoms()[(size_t) dragSourceAtom].pos;
            auto outward = modelPos - anchor;

            if (outward.getDistanceFromOrigin() < 1.0f)
                outward = { 1.0f, 0.0f };
            else
                outward /= outward.getDistanceFromOrigin();

            changed = (molecule.addAtom (currentElement, anchor + outward * 60.0f) >= 0);
        }
    }

    if (changed)
    {
        ripplePos  = e.position;
        rippleAnim = 0.0f;
    }

    dragSourceAtom = -1;
    dragTargetAtom = -1;

    if (changed && onMoleculeChanged != nullptr)
        onMoleculeChanged();

    repaint();
}

void MoleculeCanvas::updateDragValidity()
{
    dragTargetAtom = -1;

    if (dragSourceAtom < 0)
    {
        // Placing a fresh atom: valid while the molecule can still accept one.
        dragValid = molecule.canAddAtom();
        return;
    }

    // Dragging from an existing atom: either bonding to another atom, or
    // growing outwards when the pointer is over empty space.
    const int atomHit = molecule.hitTestAtom (dragCurrentModel, 1.0f);

    if (atomHit >= 0 && ! molecule.atoms()[(size_t) atomHit].isHydrogen
        && atomHit != dragSourceAtom)
    {
        dragTargetAtom = atomHit;
        dragValid = molecule.canConnect (dragSourceAtom, atomHit);
    }
    else
    {
        dragValid = molecule.canGrowFrom (dragSourceAtom);
    }
}

void MoleculeCanvas::mouseMove (const juce::MouseEvent& e)
{
    if (dragActive)
        return;

    const auto modelPos = screenToModel (e.position);
    const float s = drawScale();

    const int atomHit = molecule.hitTestAtom (modelPos, 1.0f);
    int bondHit = -1;

    if (atomHit < 0)
    {
        const float tol = (s > 0.0001f) ? (8.0f * uiScale / s) : 8.0f;
        bondHit = molecule.hitTestBond (modelPos, tol);
    }

    if (atomHit != hoveredAtom || bondHit != hoveredBond)
    {
        hoveredAtom = atomHit;
        hoveredBond = bondHit;
        hoverAnim = 0.0f;

        const bool atomInteractive = atomHit >= 0
                                     && ! molecule.atoms()[(size_t) atomHit].isHydrogen;
        const bool bondInteractive = bondHit >= 0
                                     && molecule.maxOrderForBond (bondHit) > 1;

        setMouseCursor ((atomInteractive || bondInteractive)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void MoleculeCanvas::mouseExit (const juce::MouseEvent&)
{
    if (hoveredAtom >= 0 || hoveredBond >= 0)
    {
        hoveredAtom = -1;
        hoveredBond = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void MoleculeCanvas::clearMolecule()
{
    molecule.clear();
    hoveredAtom = -1;
    hoveredBond = -1;
    viewScale = 1.0f;
    viewScaleTarget = 1.0f;
    modelCentre = { 0.0f, 0.0f };

    if (onMoleculeChanged != nullptr)
        onMoleculeChanged();
    repaint();
}

void MoleculeCanvas::restoreMolecule (const juce::ValueTree& tree)
{
    // Ignore invalid input; leave the current molecule untouched.
    if (! tree.isValid() || tree.getType() != juce::Identifier ("Molecule"))
        return;

    molecule.fromValueTree (tree);

    hoveredAtom = -1;
    hoveredBond = -1;
    viewScale = 1.0f;
    viewScaleTarget = 1.0f;
    modelCentre = { 0.0f, 0.0f };

    if (onMoleculeChanged != nullptr)
        onMoleculeChanged();
    repaint();
}

// ---------------------------------------------------------------------------
//  Animation
// ---------------------------------------------------------------------------

void MoleculeCanvas::advanceAnimation (float dt)
{
    bool needsRepaint = true;

    if (! molecule.atoms().empty())
    {
        // Relaxation runs in model space and gently re-centres towards the
        // origin, independent of the view transform.
        molecule.relaxStep (dt);
        molecule.advanceAnimations (dt);
    }

    updateViewScale (dt);

    if (hoveredAtom >= 0 || hoveredBond >= 0)
    {
        if (hoverAnim < 1.0f)
        {
            hoverAnim = juce::jmin (1.0f, hoverAnim + dt * 0.18f);
            needsRepaint = true;
        }
    }

    if (rippleAnim < 1.0f)
    {
        rippleAnim = juce::jmin (1.0f, rippleAnim + dt * 0.05f);
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

} // namespace organic
