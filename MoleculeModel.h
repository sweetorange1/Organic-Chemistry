#pragma once

#include <JuceHeader.h>
#include <vector>

// ============================================================
//  Molecule model
//
//  Responsibilities:
//    - Maintain topology of heavy atoms (C/O/N/S/P) and bonds
//    - Auto-complete hydrogen atoms from remaining valence
//    - 2D force-directed layout (bond springs + atom repulsion + centring)
//    - Keep exactly one connected molecule on the canvas
//    - Derive condensed structural formula
//
//  Scope note:
//    This is an interaction-prototype level geometric approximation, not a
//    quantum-chemical optimisation. The goal is a molecule that visibly
//    relaxes, breathes and feels organic. See PROJECT_OVERVIEW.md section
//    "Chemistry knowledge sources" for the rules this file encodes.
// ============================================================

namespace organic
{

// ---------------------------------------------------------------------------
//  Elements
// ---------------------------------------------------------------------------

enum class Element
{
    Carbon = 0,
    Oxygen,
    Nitrogen,
    Sulfur,
    Phosphorus,
    Hydrogen,   // auto-generated, not offered in the element bar
    Count
};

struct ElementInfo
{
    const char*   symbol;      // element symbol
    const char*   name;        // display name (English)
    juce::uint32  colour;      // draw colour (desaturated, for a light theme)
    int           valence;     // typical bonding capacity
    float         radius;      // draw radius in px at scale 1.0
    float         weight;      // standard atomic weight
    int           valenceElectrons;   // group electrons, for lone-pair counting
    float         electronegativity;  // Pauling scale
};

/** Element property table. Order must match the Element enum.

    Colours follow the CPK convention loosely, shifted towards lower
    saturation so they sit calmly on a white background.
    Valences are the common (neutral, uncharged) bonding capacities.

    valenceElectrons is the group's outer-shell electron count, used to derive
    lone pairs:  lonePairs = (valenceElectrons - bondOrderSum) / 2.
    Together with the sigma-bond count this yields the VSEPR steric number,
    which drives the ideal bond angle used by the layout solver.

    electronegativity is the Pauling scale value, used for bond-polarity
    descriptors in the audio mapping layer.
*/
inline const ElementInfo& elementInfo (Element e)
{
    static const ElementInfo table[] = {
        // symbol, name,         colour,      val, radius, weight,  e-, chi
        {  "C",    "Carbon",     0xFF3A3A3A,  4,   15.0f,  12.011f, 4,  2.55f },
        {  "O",    "Oxygen",     0xFFD9544D,  2,   13.5f,  15.999f, 6,  3.44f },
        {  "N",    "Nitrogen",   0xFF4A7BC4,  3,   14.0f,  14.007f, 5,  3.04f },
        {  "S",    "Sulfur",     0xFFD9A63A,  2,   16.5f,  32.060f, 6,  2.58f },
        {  "P",    "Phosphorus", 0xFFCE7A3C,  3,   16.0f,  30.974f, 5,  2.19f },
        {  "H",    "Hydrogen",   0xFFB8B8B8,  1,    7.5f,   1.008f, 1,  2.20f },
    };
    return table[(int) e];
}

/** Experimental valence angle at an atom, in degrees.

    The coarse VSEPR buckets (109.47 / 120 / 180) are only accurate for
    carbon. Real bond angles depend strongly on the central element, because
    lone pairs on the heavier elements compress the bonding angle far below
    the tetrahedral value:

      centre  species                 angle     species (hydride)   angle
      ------  ----------------------  --------  ------------------  -------
      C sp3   propane   C-C-C          112.4    methane  H-C-H      109.47
      N sp3   trimethylamine C-N-C     110.9    ammonia  H-N-H      107.8
      O sp3   dimethyl ether C-O-C     111.7    water    H-O-H      104.5
      S sp3   dimethyl sulfide C-S-C    99.1    H2S      H-S-H       92.1
      P sp3   trimethylphosphine C-P-C  98.6    PH3      H-P-H       93.5

    Values are gas-phase microwave / electron-diffraction determinations as
    tabulated in the CRC Handbook of Chemistry and Physics and in Allen et
    al., "Tables of bond lengths and angles", J. Chem. Soc. Perkin Trans. II
    (1987). Note that C-O-C is *not* linear: the two lone pairs on oxygen
    occupy electron domains, so an ether is bent at almost exactly the same
    angle as a carbon chain.

    sp2 centres are trigonal planar (120 deg) and sp centres linear (180 deg)
    regardless of the element, so only the sp3 row varies.

    @param e          central element
    @param stericNum  VSEPR steric number (sigma bonds + lone pairs)
    @param heavyPair  true when both substituents are heavy atoms
*/
inline float experimentalBondAngleDegrees (Element e, int stericNum, bool heavyPair)
{
    if (stericNum <= 2)
        return 180.0f;    // sp   - linear
    if (stericNum == 3)
        return 120.0f;    // sp2  - trigonal planar

    switch (e)            // sp3 and above: element dependent
    {
        case Element::Carbon:     return heavyPair ? 112.4f : 109.5f;
        case Element::Nitrogen:   return heavyPair ? 110.9f : 107.8f;
        case Element::Oxygen:     return heavyPair ? 111.7f : 104.5f;
        case Element::Sulfur:     return heavyPair ?  99.1f :  92.1f;
        case Element::Phosphorus: return heavyPair ?  98.6f :  93.5f;
        default:                  return 109.5f;
    }
}

/** Elements offered in the bottom selection bar (hydrogen excluded). */
inline const std::vector<Element>& selectableElements()
{
    static const std::vector<Element> list {
        Element::Carbon, Element::Oxygen, Element::Nitrogen,
        Element::Sulfur, Element::Phosphorus
    };
    return list;
}

// ---------------------------------------------------------------------------
//  Atom / Bond
// ---------------------------------------------------------------------------

struct Atom
{
    Element            element = Element::Carbon;
    juce::Point<float> pos;          // model-space position
    juce::Point<float> velocity;     // force-directed velocity
    bool               isHydrogen = false;
    int                parentIndex = -1;  // owning heavy atom, for hydrogens
    float              spawnAnim = 0.0f;  // 0 -> 1 birth animation

    Atom() = default;
    Atom (Element e, juce::Point<float> p) : element (e), pos (p) {}
};

struct Bond
{
    int   a = 0;
    int   b = 0;
    int   order = 1;          // 1 = single, 2 = double, 3 = triple
    float orderAnim = 1.0f;   // 0 -> 1 animation when the order changes

    Bond() = default;
    Bond (int i, int j, int o = 1) : a (i), b (j), order (o) {}
};

// ---------------------------------------------------------------------------
//  Chemical descriptors
//
//  Molecular quantities derivable purely from topology, used by the audio
//  mapping layer to turn a drawn molecule into synthesis parameters. These
//  mirror well-known cheminformatics descriptors (ring count, degree of
//  unsaturation, etc.) computed the way RDKit / Avogadro would for a neutral
//  covalent molecule, but restricted to the C/O/N/S/P palette this tool offers.
// ---------------------------------------------------------------------------

struct ChemicalDescriptors
{
    // --- Composition / topology (v0.6) ---
    int   heavyAtomCount       = 0;   // heavy atoms, hydrogen excluded
    int   heteroAtomCount      = 0;   // non-carbon heavy atoms (O/N/S/P)
    int   carbonCount          = 0;   // carbon atoms
    int   ringCount            = 0;   // number of independent rings (cyclomatic)
    int   doubleBondCount      = 0;   // heavy-atom double bonds
    int   tripleBondCount      = 0;   // heavy-atom triple bonds
    int   longestChainLength   = 0;   // longest heavy-atom chain (atoms)
    int   branchCount          = 0;   // heavy atoms with degree >= 3
    float molecularWeight      = 0.0f; // sum of standard atomic weights
    int   degreeOfUnsaturation = 0;   // rings + pi bonds (DoU)

    // --- Ring / aromaticity (v0.11) ---
    int   aromaticRingCount    = 0;   // rings satisfying Huckel's 4n+2 rule
    int   maxRingSize          = 0;   // atoms in the largest perceived ring

    // --- Drug-likeness style descriptors (v0.11) ---
    int   rotatableBondCount   = 0;   // Veber-style rotatable single bonds
    int   hBondDonorCount      = 0;   // Lipinski donors (N-H / O-H)
    int   hBondAcceptorCount   = 0;   // Lipinski acceptors (N and O atoms)
    float tpsa                 = 0.0f; // topological polar surface area, A^2
    float clogP                = 0.0f; // Crippen-style lipophilicity estimate
    float fractionSp3          = 0.0f; // sp3 carbons / all carbons, 0..1

    // --- Topological indices (v0.11) ---
    int   wienerIndex          = 0;   // sum of all pairwise path distances
    float randicIndex          = 0.0f; // sum over bonds of 1/sqrt(di*dj)
    float bondPolarity         = 0.0f; // sum of |delta electronegativity|
};

// ---------------------------------------------------------------------------
//  Presets
//
//  A small library of common organic molecules, stored as raw topology
//  (element list + bond list) rather than SMILES, so no parser is needed:
//  the same data feeds Molecule::fromValueTree(), which already knows how to
//  lay out a skeleton and regenerate hydrogens.
//
//  Every entry stays inside the neutral valences this editor enforces
//  (C 4, N 3, O 2, S 2, P 3), and rings are written in Kekule form.
// ---------------------------------------------------------------------------

struct PresetBond
{
    int a;
    int b;
    int order;
};

struct MoleculePreset
{
    const char*                    name;     // English display name
    std::vector<Element>           atoms;    // heavy atoms, index = bond ref
    std::vector<PresetBond>        bonds;    // heavy-atom bonds
};

/** The preset library, ordered roughly from simplest to most elaborate.
    These are the entries offered in the header drop-down. */
const std::vector<MoleculePreset>& moleculePresets();

/** Every molecule this editor can put a name to.

    A superset of moleculePresets(): the presets plus a further set of common
    organic species that are worth recognising but not worth a slot in the
    picker. Molecule::commonName() derives its lookup table from this list at
    runtime by building each entry and asking for its canonical SMILES, so a
    molecule that can be loaded as a preset is guaranteed to be named. */
const std::vector<MoleculePreset>& namedMolecules();

/** Serialise preset @p index into the same ValueTree shape that
    Molecule::toValueTree() produces, so it can be restored through the
    normal persistence path. Out-of-range indices yield an empty tree. */
juce::ValueTree presetValueTree (int index);

/** Hill-order molecular formula of preset @p index, e.g. "C6H6".

    Computed once on first call by building every preset, then cached. The
    picker shows it next to the name so the list doubles as a reference. */
const juce::String& presetFormula (int index);

// ---------------------------------------------------------------------------
//  Molecule
// ---------------------------------------------------------------------------

class Molecule
{
public:
    Molecule() = default;

    // --- Topology ---

    /** Add one heavy atom.

        Attachment rule (simplified): the new atom bonds to the heavy atom
        that still has free valence and lies closest to @p hint. If the
        molecule is empty the atom is placed at @p hint as the seed.

        @return index of the new atom, or -1 when the molecule is saturated.
    */
    int addAtom (Element element, juce::Point<float> hint);

    /** Connect two heavy atoms with a new single bond.

        Succeeds only when both atoms still have free valence and no bond
        exists between them yet, so the drag-to-connect interaction never
        violates the valence rules.
        @return true when a bond was created.
    */
    bool connectAtoms (int a, int b);

    /** Whether a new atom can still be placed: the molecule is empty, or at
        least one heavy atom still has free valence. */
    bool canAddAtom() const;

    /** Whether heavy atom @p index still has free valence to grow from. */
    bool canGrowFrom (int index) const;

    /** Whether a new single bond may be formed between two heavy atoms. */
    bool canConnect (int a, int b) const;

    /** Remove a heavy atom.

        After removal the graph may split. Only the largest remaining
        fragment is kept, so the canvas always holds exactly one molecule.
        Hydrogens cannot be removed directly.
    */
    bool removeAtom (int index);

    /** Break the bond at @p bondIndex.

        Removing a bridge bond may split the skeleton into disconnected
        pieces (e.g. opening a ring). Only the largest remaining fragment is
        kept, matching removeAtom(). Hydrogen bonds cannot be broken directly;
        they are regenerated automatically.
    */
    bool removeBond (int bondIndex);

    /** Cycle a bond order: single -> double -> triple -> single.

        The next order is clamped by the free valence still available on
        both endpoints, so the result never exceeds what the elements allow.
        Bonds to hydrogen stay single.

        @return true when the order actually changed.
    */
    bool cycleBondOrder (int bondIndex);

    /** Highest order this bond may take given both endpoints' free valence. */
    int maxOrderForBond (int bondIndex) const;

    /** Hit test atoms. Returns atom index or -1. */
    int hitTestAtom (juce::Point<float> p, float scale = 1.0f) const;

    /** Hit test bonds by point-to-segment distance. Returns bond index or -1.
        Hydrogen bonds are ignored since their order is fixed.
    */
    int hitTestBond (juce::Point<float> p, float tolerance = 7.0f) const;

    void clear();

    // --- Queries ---

    const std::vector<Atom>& atoms() const { return atomList; }
    const std::vector<Bond>& bonds() const { return bondList; }

    int heavyAtomCount() const;

    /** Molecular formula in Hill order (C, then H, then alphabetical). */
    juce::String formula() const;

    /** Condensed structural formula, e.g. "CH3-CH2-OH", "CH3-CH=CH2".
        Walks the heavy-atom skeleton along its longest chain and renders
        each heavy atom with its implicit hydrogen count.
    */
    juce::String structuralFormula() const;

    /** Canonical SMILES (Kekulé, implicit hydrogens) used for common-name
        matching. Deterministic and graph-isomorphism invariant. */
    juce::String canonicalSmiles() const;

    /** English common name for a recognised molecule, or an empty string. */
    juce::String commonName() const;

    /** Approximate molecular weight from standard atomic weights. */
    float molecularWeight() const;

    /** Compute the molecular descriptors used by the audio mapping layer.

        Pure topology-derived quantities (ring count, degree of unsaturation,
        branching, chain length, etc.). Cheap to call and safe from the UI
        thread; the resulting plain struct is copied into the audio engine.
    */
    ChemicalDescriptors computeDescriptors() const;

    /** Axis-aligned bounding box over all atoms (model space). */
    juce::Rectangle<float> boundingBox() const;

    // --- VSEPR geometry ---

    /** VSEPR steric number of an atom: sigma bonds + lone pairs.

        Lone pairs come from (valenceElectrons - totalBondOrder) / 2, so a
        neutral oxygen with two single bonds reports 2 + 2 = 4, a carbonyl
        carbon reports 3, and an sp carbon reports 2. */
    int stericNumber (int index) const;

    /** Ideal bond angle in radians at @p index.

        Derived from the steric number, then refined with the experimental
        value for the central element (see experimentalBondAngleDegrees).
        @param heavyPair true when both substituents are heavy atoms, which
                         selects the methylated rather than the hydride value.
    */
    float idealBondAngle (int index, bool heavyPair = true) const;

    /** Smallest rings through each bond (an SSSR approximation).
        Each entry lists the heavy-atom indices of one ring, in cycle order. */
    std::vector<std::vector<int>> perceiveRings() const;

    /** Cached perceiveRings(), recomputed only when the topology changes.
        relaxStep() runs at 60 Hz, so the BFS must not run every frame. */
    const std::vector<std::vector<int>>& rings() const;

    // --- Persistence ---

    /** Serialise the heavy-atom topology (elements + bonds + bond orders)
        into a ValueTree. Hydrogens are omitted; they are regenerated on
        restore. Used for DAW project persistence. */
    juce::ValueTree toValueTree() const;

    /** Rebuild the molecule from a ValueTree produced by toValueTree().

        The molecule is cleared first, heavy atoms are placed on a ring, then
        bonds and hydrogens are rebuilt. A valid but empty tree simply clears
        the molecule. Invalid / wrong-typed trees are ignored. */
    void fromValueTree (const juce::ValueTree& tree);

    // --- Geometry ---

    /** Advance one force-directed relaxation step.

        The molecule is gently re-centred towards the origin each step (a
        centroid-level force shared by all heavy atoms), so it stays centred
        in the canvas without a hard boundary constraint or per-atom pull.
        @return mean per-atom motion this step (used for convergence checks).
    */
    float relaxStep (float dt);

    /** Advance birth / bond-order animations. */
    void advanceAnimations (float dt);

    bool isSettling() const { return settling; }

private:
    /** Rebuild every hydrogen from the heavy atoms' free valence. */
    void rebuildHydrogens();

    /** Bond order sum towards heavy atoms only (hydrogens excluded). */
    int heavyBondOrderSum (int index) const;

    /** Total bond order sum, hydrogens included. */
    int totalBondOrderSum (int index) const;

    /** Drop every fragment except the largest connected one (heavy atoms). */
    void keepLargestFragment();

    /** Longest heavy-atom chain, as an ordered index list. */
    std::vector<int> longestChain() const;

    /** Whether the heavy-atom graph contains at least one ring. */
    bool hasRing() const;

    /** SMILES-style structural formula with ring-closure numbers, used for
        cyclic molecules (where the longest-chain walker does not apply). */
    juce::String ringStructuralFormula() const;

    /** Mark the ring cache stale. Called from every topology mutation. */
    void invalidateTopology() { ++topologyVersion; }

    /** Ideal angle in radians between two ring neighbours of @p centre.

        A closed planar n-gon forces its interior angles to sum to
        (n - 2) * 180 deg, i.e. 180 - 360/n each. Cyclohexane therefore needs
        120 deg per vertex, not the 109.47 deg an isolated sp3 carbon wants.
        Insisting on the hybridisation value makes the ring unclosable and the
        solver resolves the conflict by flipping one vertex concave, which is
        the dented-polygon artefact this function removes.

        @return the polygon angle, or a negative value when the two
                neighbours are not consecutive with @p centre in any ring.
    */
    float ringAngleTarget (int centre, int n1, int n2) const;

    /** Deterministic ring-aware starting geometry: rings are laid out as
        regular polygons and the remaining atoms are grown outwards by BFS.
        Used after fromValueTree(), where no coordinates were persisted. */
    void seedLayout();

    std::vector<Atom> atomList;
    std::vector<Bond> bondList;
    bool settling = false;

    // Ring cache. Mutable so the const accessors can refresh it lazily.
    int                                   topologyVersion   = 0;
    mutable int                           cachedRingVersion = -1;
    mutable std::vector<std::vector<int>> cachedRings;

    // --- Force-field parameters ---
    //
    // The layout is a miniature molecular-mechanics force field: bond
    // stretching + angle bending + non-bonded repulsion, with 1-2 and 1-3
    // pairs excluded from the repulsion term exactly as MMFF94 / UFF do.
    // Angles are therefore set by VSEPR, not by a repulsion tug-of-war, which
    // is what gives alkane chains their proper zig-zag.

    static constexpr float kHeavyBondLength    = 62.0f;
    static constexpr float kHydrogenBondLength = 34.0f;
    static constexpr float kSpringK            = 0.055f;

    // Angle bending stiffness. The force scales as k * (theta - theta0) / r,
    // so k lives on a much larger numeric scale than the spring constant.
    // The heavy-atom backbone is stiffer than the hydrogens hanging off it.
    static constexpr float kAngleHeavyK        = 150.0f;
    static constexpr float kAngleHydrogenK     = 55.0f;

    // Non-bonded repulsion now only has to keep 1-4 and more distant pairs
    // apart, so it is softer than in the angle-free versions of this solver.
    static constexpr float kRepulsionK         = 3000.0f;

    // Ring template force. Each perceived ring pulls its members onto the
    // circumcircle of the regular polygon with the same side length. This is
    // the 2D-depiction trick every structure drawing package uses (RDKit's
    // coordinate generator ships explicit ring templates for the same
    // reason): it guarantees a convex, evenly spaced ring and stops the angle
    // term from settling into a mirrored, concave solution.
    static constexpr float kRingTemplateK      = 0.10f;

    static constexpr float kCentringK          = 0.004f;
    static constexpr float kDamping            = 0.86f;
    static constexpr float kSettleThreshold    = 0.06f;
};

} // namespace organic
