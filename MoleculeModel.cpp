#include "MoleculeModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <tuple>
#include <utility>

namespace organic
{

namespace
{
/** Serialise a preset-style topology description into a ValueTree.
    Defined further down next to the preset tables; forward-declared here
    because Molecule::commonName() builds its lookup table with it. */
juce::ValueTree topologyToValueTree (const MoleculePreset& preset);
}

// ---------------------------------------------------------------------------
//  Valence bookkeeping
// ---------------------------------------------------------------------------

int Molecule::heavyBondOrderSum (int index) const
{
    int n = 0;
    for (const auto& b : bondList)
    {
        const int other = (b.a == index) ? b.b : (b.b == index ? b.a : -1);
        if (other >= 0 && ! atomList[(size_t) other].isHydrogen)
            n += b.order;
    }
    return n;
}

int Molecule::totalBondOrderSum (int index) const
{
    int n = 0;
    for (const auto& b : bondList)
        if (b.a == index || b.b == index)
            n += b.order;
    return n;
}

int Molecule::heavyAtomCount() const
{
    int n = 0;
    for (const auto& a : atomList)
        if (! a.isHydrogen)
            ++n;
    return n;
}

// ---------------------------------------------------------------------------
//  Add atom
// ---------------------------------------------------------------------------

int Molecule::addAtom (Element element, juce::Point<float> hint)
{
    // Seed atom: drop it straight at the hint position.
    if (heavyAtomCount() == 0)
    {
        atomList.clear();
        bondList.clear();

        atomList.emplace_back (element, hint);

        rebuildHydrogens();
        settling = true;
        return 0;
    }

    // Pick an attachment point: a heavy atom with free valence, nearest to hint.
    int   bestIdx  = -1;
    float bestDist = std::numeric_limits<float>::max();

    for (size_t i = 0; i < atomList.size(); ++i)
    {
        const auto& a = atomList[i];
        if (a.isHydrogen)
            continue;

        const int freeValence = elementInfo (a.element).valence - heavyBondOrderSum ((int) i);
        if (freeValence <= 0)
            continue;

        const float d = a.pos.getDistanceFrom (hint);
        if (d < bestDist)
        {
            bestDist = d;
            bestIdx  = (int) i;
        }
    }

    if (bestIdx < 0)
        return -1;   // fully saturated, nowhere to attach

    // Place the new atom one bond length away from the anchor.
    //
    // Rather than dropping it straight along (anchor -> pointer), the direction
    // is snapped to a chemically sensible one so the bond starts at its VSEPR
    // angle instead of collinear with an existing bond. The user still chooses
    // *which side* the atom lands on; the solver only fixes the angle.
    const auto anchor = atomList[(size_t) bestIdx].pos;

    auto pointerDir = hint - anchor;
    if (pointerDir.getDistanceFromOrigin() < 1.0f)
        pointerDir = { 1.0f, 0.0f };
    pointerDir /= pointerDir.getDistanceFromOrigin();

    // Existing heavy neighbours define the directions already taken.
    std::vector<float> takenAngles;
    for (const auto& b : bondList)
    {
        const int other = (b.a == bestIdx) ? b.b : (b.b == bestIdx ? b.a : -1);
        if (other < 0 || atomList[(size_t) other].isHydrogen)
            continue;

        const auto d = atomList[(size_t) other].pos - anchor;
        if (d.getDistanceFromOrigin() > 0.01f)
            takenAngles.push_back (std::atan2 (d.y, d.x));
    }

    auto dir = pointerDir;

    if (takenAngles.size() == 1)
    {
        // One existing bond: the new bond sits at the ideal angle on whichever
        // side the pointer is on, which is exactly how a chain zig-zags.
        const float ideal = idealBondAngle (bestIdx);
        const float pointerAngle = std::atan2 (pointerDir.y, pointerDir.x);

        float best = 0.0f;
        float bestDelta = std::numeric_limits<float>::max();

        for (const float sign : { +1.0f, -1.0f })
        {
            const float candidate = takenAngles[0] + sign * ideal;

            // Shortest angular distance to the pointer.
            float delta = std::abs (std::remainder (candidate - pointerAngle,
                                                    juce::MathConstants<float>::twoPi));
            if (delta < bestDelta)
            {
                bestDelta = delta;
                best = candidate;
            }
        }

        dir = { std::cos (best), std::sin (best) };
    }
    else if (takenAngles.size() >= 2)
    {
        // Several bonds already: drop the new one into the widest free gap,
        // which is what electron-pair repulsion would do anyway.
        std::sort (takenAngles.begin(), takenAngles.end());

        float bestBisector = std::atan2 (pointerDir.y, pointerDir.x);
        float widest = -1.0f;

        for (size_t i = 0; i < takenAngles.size(); ++i)
        {
            const float a = takenAngles[i];
            const float b = (i + 1 < takenAngles.size())
                                ? takenAngles[i + 1]
                                : takenAngles[0] + juce::MathConstants<float>::twoPi;

            const float gap = b - a;
            if (gap > widest)
            {
                widest = gap;
                bestBisector = a + gap * 0.5f;
            }
        }

        dir = { std::cos (bestBisector), std::sin (bestBisector) };
    }

    atomList.emplace_back (element, anchor + dir * kHeavyBondLength);
    const int newIdx = (int) atomList.size() - 1;
    bondList.emplace_back (bestIdx, newIdx, 1);

    rebuildHydrogens();
    settling = true;
    return newIdx;
}

// ---------------------------------------------------------------------------
//  Placement / connection queries (drag-to-place preview)
// ---------------------------------------------------------------------------

bool Molecule::canAddAtom() const
{
    if (atomList.empty())
        return true;

    for (size_t i = 0; i < atomList.size(); ++i)
        if (! atomList[i].isHydrogen && canGrowFrom ((int) i))
            return true;
    return false;
}

bool Molecule::canGrowFrom (int index) const
{
    if (index < 0 || index >= (int) atomList.size())
        return false;
    if (atomList[(size_t) index].isHydrogen)
        return false;

    return heavyBondOrderSum (index) < elementInfo (atomList[(size_t) index].element).valence;
}

bool Molecule::canConnect (int a, int b) const
{
    if (a < 0 || b < 0 || a >= (int) atomList.size() || b >= (int) atomList.size())
        return false;
    if (a == b)
        return false;
    if (atomList[(size_t) a].isHydrogen || atomList[(size_t) b].isHydrogen)
        return false;

    // A bond already exists between them; raising its order goes through
    // cycleBondOrder() instead of creating a duplicate bond.
    for (const auto& bond : bondList)
        if ((bond.a == a && bond.b == b) || (bond.a == b && bond.b == a))
            return false;

    return canGrowFrom (a) && canGrowFrom (b);
}

bool Molecule::connectAtoms (int a, int b)
{
    if (! canConnect (a, b))
        return false;

    bondList.emplace_back (a, b, 1);
    rebuildHydrogens();
    settling = true;
    return true;
}

// ---------------------------------------------------------------------------
//  Remove atom  (problem 4: always keep a single molecule)
// ---------------------------------------------------------------------------

bool Molecule::removeAtom (int index)
{
    if (index < 0 || index >= (int) atomList.size())
        return false;

    if (atomList[(size_t) index].isHydrogen)
        return false;   // hydrogens are system-managed

    // Rebuild the heavy-atom graph without the removed atom. Hydrogens are
    // dropped wholesale here and regenerated at the end.
    std::vector<Atom> heavy;
    std::vector<Bond> heavyBonds;
    std::vector<int>  remap (atomList.size(), -1);

    for (size_t i = 0; i < atomList.size(); ++i)
    {
        if (atomList[i].isHydrogen || (int) i == index)
            continue;

        remap[i] = (int) heavy.size();
        heavy.push_back (atomList[i]);
    }

    for (const auto& b : bondList)
    {
        if (b.a == index || b.b == index)
            continue;
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;

        const int na = remap[(size_t) b.a];
        const int nb = remap[(size_t) b.b];
        if (na >= 0 && nb >= 0)
        {
            Bond nb2 (na, nb, b.order);
            nb2.orderAnim = 1.0f;
            heavyBonds.push_back (nb2);
        }
    }

    atomList = std::move (heavy);
    bondList = std::move (heavyBonds);

    // Removing a mid-chain atom splits the skeleton: keep only the largest part.
    keepLargestFragment();

    rebuildHydrogens();
    settling = true;
    return true;
}

// ---------------------------------------------------------------------------
//  Remove bond  (break a bond; keep a single molecule)
// ---------------------------------------------------------------------------

bool Molecule::removeBond (int bondIndex)
{
    if (bondIndex < 0 || bondIndex >= (int) bondList.size())
        return false;

    const auto& b = bondList[(size_t) bondIndex];
    if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
        return false;   // hydrogen bonds are system-managed

    // Drop the target heavy bond.
    bondList.erase (bondList.begin() + (size_t) bondIndex);

    // Strip hydrogens so the prune below works on the heavy skeleton alone,
    // exactly as removeAtom() does.
    std::vector<Atom> heavy;
    std::vector<Bond> heavyBonds;
    std::vector<int>  remap (atomList.size(), -1);

    for (size_t i = 0; i < atomList.size(); ++i)
    {
        if (atomList[i].isHydrogen)
            continue;
        remap[i] = (int) heavy.size();
        heavy.push_back (atomList[i]);
    }

    for (const auto& bond : bondList)
    {
        const int na = remap[(size_t) bond.a];
        const int nb = remap[(size_t) bond.b];
        if (na >= 0 && nb >= 0)
        {
            Bond nb2 (na, nb, bond.order);
            nb2.orderAnim = bond.orderAnim;
            heavyBonds.push_back (nb2);
        }
    }

    atomList = std::move (heavy);
    bondList = std::move (heavyBonds);

    // Opening a ring or breaking a bridge may split the skeleton: keep the
    // largest remaining fragment.
    keepLargestFragment();

    rebuildHydrogens();
    settling = true;
    return true;
}

// ---------------------------------------------------------------------------
//  Connected-component pruning
// ---------------------------------------------------------------------------

void Molecule::keepLargestFragment()
{
    const size_t n = atomList.size();
    if (n == 0)
        return;

    // Adjacency over the current (heavy-only) graph.
    std::vector<std::vector<int>> adjacency (n);
    for (const auto& b : bondList)
    {
        adjacency[(size_t) b.a].push_back (b.b);
        adjacency[(size_t) b.b].push_back (b.a);
    }

    // Label components with BFS.
    std::vector<int> component (n, -1);
    int componentCount = 0;

    for (size_t start = 0; start < n; ++start)
    {
        if (component[start] >= 0)
            continue;

        std::queue<int> q;
        q.push ((int) start);
        component[start] = componentCount;

        while (! q.empty())
        {
            const int cur = q.front();
            q.pop();

            for (int nb : adjacency[(size_t) cur])
            {
                if (component[(size_t) nb] < 0)
                {
                    component[(size_t) nb] = componentCount;
                    q.push (nb);
                }
            }
        }
        ++componentCount;
    }

    if (componentCount <= 1)
        return;   // still a single molecule, nothing to prune

    // Pick the largest component; ties break towards the lower index so the
    // result is deterministic.
    std::vector<int> sizes ((size_t) componentCount, 0);
    for (size_t i = 0; i < n; ++i)
        ++sizes[(size_t) component[i]];

    const int keep = (int) std::distance (sizes.begin(),
                                         std::max_element (sizes.begin(), sizes.end()));

    // Compact atoms and bonds down to the surviving component.
    std::vector<Atom> kept;
    std::vector<Bond> keptBonds;
    std::vector<int>  remap (n, -1);

    for (size_t i = 0; i < n; ++i)
    {
        if (component[i] != keep)
            continue;
        remap[i] = (int) kept.size();
        kept.push_back (atomList[i]);
    }

    for (const auto& b : bondList)
    {
        const int na = remap[(size_t) b.a];
        const int nb = remap[(size_t) b.b];
        if (na >= 0 && nb >= 0)
        {
            Bond nb2 (na, nb, b.order);
            nb2.orderAnim = 1.0f;
            keptBonds.push_back (nb2);
        }
    }

    atomList = std::move (kept);
    bondList = std::move (keptBonds);
}

void Molecule::clear()
{
    atomList.clear();
    bondList.clear();
    settling = false;
    invalidateTopology();
}

// ---------------------------------------------------------------------------
//  Bond order cycling  (problem 5)
// ---------------------------------------------------------------------------

int Molecule::maxOrderForBond (int bondIndex) const
{
    if (bondIndex < 0 || bondIndex >= (int) bondList.size())
        return 1;

    const auto& b = bondList[(size_t) bondIndex];
    const auto& A = atomList[(size_t) b.a];
    const auto& B = atomList[(size_t) b.b];

    // Bonds to hydrogen are always single.
    if (A.isHydrogen || B.isHydrogen)
        return 1;

    // Free valence on each side, counting hydrogens as replaceable: raising the
    // order consumes hydrogens first, so the ceiling is
    //   valence - (heavy bond order excluding this bond)
    const int freeA = elementInfo (A.element).valence - (heavyBondOrderSum (b.a) - b.order);
    const int freeB = elementInfo (B.element).valence - (heavyBondOrderSum (b.b) - b.order);

    return juce::jlimit (1, 3, juce::jmin (freeA, freeB));
}

bool Molecule::cycleBondOrder (int bondIndex)
{
    if (bondIndex < 0 || bondIndex >= (int) bondList.size())
        return false;

    const int maxOrder = maxOrderForBond (bondIndex);
    if (maxOrder <= 1)
        return false;   // no room for a higher order

    auto& b = bondList[(size_t) bondIndex];

    // single -> double -> triple -> back to single
    const int next = (b.order >= maxOrder) ? 1 : b.order + 1;
    if (next == b.order)
        return false;

    b.order = next;
    b.orderAnim = 0.0f;

    // Raising the order consumes hydrogens; lowering it frees them up.
    rebuildHydrogens();
    settling = true;
    return true;
}

// ---------------------------------------------------------------------------
//  Hydrogen auto-completion
// ---------------------------------------------------------------------------

void Molecule::rebuildHydrogens()
{
    // Every mutating operation funnels through here, so this is the single
    // point where the ring cache has to be marked stale. Heavy atoms keep
    // indices 0..heavyCount-1 below and hydrogens are appended afterwards,
    // which is what lets the cached rings store plain atom indices.
    invalidateTopology();

    // 1) Strip existing hydrogens and any bond touching one.
    std::vector<Atom> heavy;
    std::vector<Bond> heavyBonds;
    std::vector<int>  remap (atomList.size(), -1);

    for (size_t i = 0; i < atomList.size(); ++i)
    {
        if (atomList[i].isHydrogen)
            continue;
        remap[i] = (int) heavy.size();
        heavy.push_back (atomList[i]);
    }

    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;

        const int na = remap[(size_t) b.a];
        const int nb = remap[(size_t) b.b];
        if (na >= 0 && nb >= 0)
        {
            Bond nb2 (na, nb, b.order);
            nb2.orderAnim = b.orderAnim;
            heavyBonds.push_back (nb2);
        }
    }

    atomList = std::move (heavy);
    bondList = std::move (heavyBonds);

    // 2) Fill each heavy atom up to its valence with hydrogens.
    const int heavyCount = (int) atomList.size();

    for (int i = 0; i < heavyCount; ++i)
    {
        const auto& info = elementInfo (atomList[(size_t) i].element);
        const int nH = juce::jmax (0, info.valence - heavyBondOrderSum (i));

        if (nH == 0)
            continue;

        // Spread hydrogens away from the directions already taken by neighbours.
        juce::Point<float> occupied { 0.0f, 0.0f };
        int neighbours = 0;

        for (const auto& b : bondList)
        {
            const int other = (b.a == i) ? b.b : (b.b == i ? b.a : -1);
            if (other >= 0)
            {
                auto d = atomList[(size_t) other].pos - atomList[(size_t) i].pos;
                const float len = d.getDistanceFromOrigin();
                if (len > 0.01f)
                {
                    occupied += d / len;
                    ++neighbours;
                }
            }
        }

        float baseAngle = -juce::MathConstants<float>::halfPi;
        if (neighbours > 0 && occupied.getDistanceFromOrigin() > 0.01f)
            baseAngle = std::atan2 (-occupied.y, -occupied.x);

        const float spread = (nH == 1)
                                 ? 0.0f
                                 : juce::MathConstants<float>::twoPi / (float) (nH + neighbours + 1);

        for (int k = 0; k < nH; ++k)
        {
            const float offset = (nH == 1)
                                     ? 0.0f
                                     : ((float) k - (float) (nH - 1) * 0.5f) * spread;
            const float ang = baseAngle + offset;

            Atom h (Element::Hydrogen,
                    atomList[(size_t) i].pos
                        + juce::Point<float> (std::cos (ang), std::sin (ang)) * kHydrogenBondLength);
            h.isHydrogen  = true;
            h.parentIndex = i;
            h.spawnAnim   = atomList[(size_t) i].spawnAnim;

            atomList.push_back (h);
            bondList.emplace_back (i, (int) atomList.size() - 1, 1);
        }
    }
}

// ---------------------------------------------------------------------------
//  Hit testing
// ---------------------------------------------------------------------------

int Molecule::hitTestAtom (juce::Point<float> p, float scale) const
{
    int   best  = -1;
    float bestD = std::numeric_limits<float>::max();

    for (size_t i = 0; i < atomList.size(); ++i)
    {
        const auto& a = atomList[i];
        const float r = elementInfo (a.element).radius * scale + 4.0f;
        const float d = a.pos.getDistanceFrom (p);

        if (d <= r && d < bestD)
        {
            bestD = d;
            best  = (int) i;
        }
    }
    return best;
}

int Molecule::hitTestBond (juce::Point<float> p, float tolerance) const
{
    int   best  = -1;
    float bestD = tolerance;

    for (size_t i = 0; i < bondList.size(); ++i)
    {
        const auto& b = bondList[i];
        const auto& A = atomList[(size_t) b.a];
        const auto& B = atomList[(size_t) b.b];

        // Hydrogen bonds have a fixed order, so they are not clickable.
        if (A.isHydrogen || B.isHydrogen)
            continue;

        // Point-to-segment distance.
        const auto ab = B.pos - A.pos;
        const float lenSq = ab.x * ab.x + ab.y * ab.y;
        if (lenSq < 1.0f)
            continue;

        const auto ap = p - A.pos;
        float t = (ap.x * ab.x + ap.y * ab.y) / lenSq;
        t = juce::jlimit (0.0f, 1.0f, t);

        const auto closest = A.pos + ab * t;
        const float d = closest.getDistanceFrom (p);

        // Ignore hits that sit inside either endpoint's circle; those belong
        // to the atoms, not the bond.
        const float rA = elementInfo (A.element).radius;
        const float rB = elementInfo (B.element).radius;
        if (p.getDistanceFrom (A.pos) < rA || p.getDistanceFrom (B.pos) < rB)
            continue;

        if (d < bestD)
        {
            bestD = d;
            best  = (int) i;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
//  VSEPR geometry
//
//  Valence Shell Electron Pair Repulsion: the electron domains around an atom
//  (sigma bonds plus lone pairs) arrange themselves as far apart as possible.
//  The count of those domains is the steric number, and it fixes the ideal
//  angle between substituents.
//
//    SN 2 -> linear,          180 deg   (sp,  e.g. HC#CH, O=C=O)
//    SN 3 -> trigonal planar, 120 deg   (sp2, e.g. H2C=CH2, benzene)
//    SN 4 -> tetrahedral,     109.47 deg (sp3, e.g. CH4, H2O, NH3)
//
//  Lone pairs count towards the steric number even though they are not drawn,
//  which is why water comes out bent rather than linear.
// ---------------------------------------------------------------------------

int Molecule::stericNumber (int index) const
{
    if (index < 0 || index >= (int) atomList.size())
        return 4;

    // Sigma bonds: one per bonded neighbour, regardless of bond order. The pi
    // bonds of a double / triple bond share the same electron domain.
    int sigmaBonds = 0;
    for (const auto& b : bondList)
        if (b.a == index || b.b == index)
            ++sigmaBonds;

    const auto& info = elementInfo (atomList[(size_t) index].element);
    const int bondingElectrons = totalBondOrderSum (index);
    const int lonePairs = juce::jmax (0, (info.valenceElectrons - bondingElectrons) / 2);

    return sigmaBonds + lonePairs;
}

float Molecule::idealBondAngle (int index, bool heavyPair) const
{
    if (index < 0 || index >= (int) atomList.size())
        return juce::degreesToRadians (109.5f);

    const int sn = stericNumber (index);
    const auto e = atomList[(size_t) index].element;

    return juce::degreesToRadians (experimentalBondAngleDegrees (e, sn, heavyPair));
}

// ---------------------------------------------------------------------------
//  Ring cache
//
//  perceiveRings() runs a BFS per bond, which is fine on demand but far too
//  expensive for a 60 Hz solver. The result only changes when the topology
//  does, so it is memoised against a version counter bumped by every
//  mutating operation.
// ---------------------------------------------------------------------------

const std::vector<std::vector<int>>& Molecule::rings() const
{
    if (cachedRingVersion != topologyVersion)
    {
        cachedRings = perceiveRings();
        cachedRingVersion = topologyVersion;
    }
    return cachedRings;
}

float Molecule::ringAngleTarget (int centre, int n1, int n2) const
{
    for (const auto& ring : rings())
    {
        const int n = (int) ring.size();
        if (n < 3)
            continue;

        // Locate the centre inside this ring.
        int at = -1;
        for (int i = 0; i < n; ++i)
            if (ring[(size_t) i] == centre)
            {
                at = i;
                break;
            }

        if (at < 0)
            continue;

        // Its two ring neighbours are the cycle predecessor and successor.
        const int prev = ring[(size_t) ((at + n - 1) % n)];
        const int next = ring[(size_t) ((at + 1) % n)];

        const bool matches = (n1 == prev && n2 == next)
                          || (n1 == next && n2 == prev);

        if (matches)
            return juce::MathConstants<float>::pi
                 - juce::MathConstants<float>::twoPi / (float) n;
    }

    return -1.0f;   // not a ring pair
}

// ---------------------------------------------------------------------------
//  Ring perception (SSSR approximation)
//
//  For every heavy-atom bond that lies on a cycle, the smallest ring through
//  it is found by deleting the bond and taking the shortest remaining path
//  between its endpoints. Collecting those rings and removing duplicates gives
//  a set equivalent to the Smallest Set of Smallest Rings for the small, mostly
//  monocyclic molecules this editor produces.
// ---------------------------------------------------------------------------

std::vector<std::vector<int>> Molecule::perceiveRings() const
{
    std::vector<std::vector<int>> rings;

    const int n = (int) atomList.size();
    if (n == 0)
        return rings;

    // Heavy-atom adjacency.
    std::vector<std::vector<int>> adjacency ((size_t) n);
    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;
        adjacency[(size_t) b.a].push_back (b.b);
        adjacency[(size_t) b.b].push_back (b.a);
    }

    std::vector<std::vector<int>> seen;   // sorted atom sets, for de-duplication

    for (const auto& bond : bondList)
    {
        if (atomList[(size_t) bond.a].isHydrogen || atomList[(size_t) bond.b].isHydrogen)
            continue;

        const int start = bond.a;
        const int goal  = bond.b;

        // BFS from start to goal without traversing this bond directly.
        std::vector<int> parent ((size_t) n, -2);
        std::queue<int> q;
        q.push (start);
        parent[(size_t) start] = -1;

        bool found = false;
        while (! q.empty() && ! found)
        {
            const int cur = q.front();
            q.pop();

            for (int nb : adjacency[(size_t) cur])
            {
                // Skip the bond under test itself.
                if ((cur == start && nb == goal) || (cur == goal && nb == start))
                    continue;

                if (parent[(size_t) nb] != -2)
                    continue;

                parent[(size_t) nb] = cur;

                if (nb == goal)
                {
                    found = true;
                    break;
                }
                q.push (nb);
            }
        }

        if (! found)
            continue;   // bridge bond, not part of any ring

        std::vector<int> ring;
        for (int cur = goal; cur != -1; cur = parent[(size_t) cur])
            ring.push_back (cur);

        if (ring.size() < 3)
            continue;

        std::vector<int> key = ring;
        std::sort (key.begin(), key.end());

        if (std::find (seen.begin(), seen.end(), key) == seen.end())
        {
            seen.push_back (key);
            rings.push_back (ring);
        }
    }

    return rings;
}

// ---------------------------------------------------------------------------
//  Force-directed layout
// ---------------------------------------------------------------------------

float Molecule::relaxStep (float dt)
{
    const size_t n = atomList.size();
    if (n == 0)
    {
        settling = false;
        return 0.0f;
    }

    std::vector<juce::Point<float>> force (n, { 0.0f, 0.0f });

    // Adjacency over every bond (hydrogens included) — needed by both the
    // angle term and the 1-2 / 1-3 exclusion mask below.
    std::vector<std::vector<int>> adjacency (n);
    for (const auto& b : bondList)
    {
        adjacency[(size_t) b.a].push_back (b.b);
        adjacency[(size_t) b.b].push_back (b.a);
    }

    // 1) Bond springs pull towards the ideal length. Higher bond orders are
    //    slightly shorter, mirroring real bond-length trends
    //    (C-C 154 pm, C=C 134 pm, C#C 120 pm).
    for (const auto& b : bondList)
    {
        auto& A = atomList[(size_t) b.a];
        auto& B = atomList[(size_t) b.b];

        const bool involvesH = A.isHydrogen || B.isHydrogen;
        float ideal = involvesH ? kHydrogenBondLength : kHeavyBondLength;

        if (! involvesH)
            ideal *= (b.order == 2 ? 0.88f : (b.order >= 3 ? 0.79f : 1.0f));

        auto d = B.pos - A.pos;
        float len = d.getDistanceFromOrigin();
        if (len < 0.01f)
        {
            d = { 0.7f, 0.3f };
            len = d.getDistanceFromOrigin();
        }

        const auto dir = d / len;
        const float f = (len - ideal) * kSpringK;

        force[(size_t) b.a] += dir * f;
        force[(size_t) b.b] -= dir * f;
    }

    // 2) Angle bending — the term that gives the molecule real geometry.
    //
    //    A harmonic restraint E = 1/2 k (theta - theta0)^2 is applied to every
    //    neighbour pair around a shared centre, with theta0 taken from the
    //    centre's VSEPR steric number. This is what turns a butane chain from
    //    a straight line into the correct 109.5 deg zig-zag.
    //
    //    Two passes run with different stiffness:
    //      A. heavy-heavy pairs (the skeleton) — stiff, uses the pure
    //         hybridisation angle when the centre has exactly two heavy
    //         neighbours, otherwise spreads them evenly.
    //      B. pairs involving hydrogen — softer, fills the remaining space.
    {
        auto applyAngleForce = [&] (int centre, int p1, int p2,
                                    float targetAngle, float k)
        {
            const auto r1 = atomList[(size_t) p1].pos - atomList[(size_t) centre].pos;
            const auto r2 = atomList[(size_t) p2].pos - atomList[(size_t) centre].pos;

            const float l1 = r1.getDistanceFromOrigin();
            const float l2 = r2.getDistanceFromOrigin();
            if (l1 < 1.0e-3f || l2 < 1.0e-3f)
                return;

            const auto u1 = r1 / l1;
            const auto u2 = r2 / l2;

            const float dot = juce::jlimit (-1.0f, 1.0f, u1.x * u2.x + u1.y * u2.y);
            const float theta = std::acos (dot);
            const float diff = theta - targetAngle;

            if (std::abs (diff) < 1.0e-4f)
                return;

            // Perpendicular components: the directions in which each neighbour
            // must move to change the angle.
            auto t1 = u2 - u1 * dot;
            auto t2 = u1 - u2 * dot;

            const float n1 = t1.getDistanceFromOrigin();
            const float n2 = t2.getDistanceFromOrigin();

            if (n1 < 1.0e-4f || n2 < 1.0e-4f)
            {
                // Collinear (0 or 180 deg): the gradient vanishes, so nudge both
                // neighbours the *same* way perpendicular to the axis. Moving
                // them in opposite directions would keep them anti-parallel and
                // the configuration would never leave this saddle point.
                const juce::Point<float> perp { -u1.y, u1.x };
                const float kick = k * diff * 0.5f / juce::jmax (l1, l2);

                force[(size_t) p1] += perp * kick;
                force[(size_t) p2] += perp * kick;
                force[(size_t) centre] -= perp * (kick * 2.0f);
                return;
            }

            t1 /= n1;
            t2 /= n2;

            // F = -dE/dp = k * (theta - theta0) * t / r
            const auto f1 = t1 * (k * diff / l1);
            const auto f2 = t2 * (k * diff / l2);

            force[(size_t) p1] += f1;
            force[(size_t) p2] += f2;
            force[(size_t) centre] -= (f1 + f2);
        };

        // Order a neighbour list by polar angle around the centre.
        auto sortByAngle = [&] (int centre, std::vector<int>& list)
        {
            const auto origin = atomList[(size_t) centre].pos;
            std::sort (list.begin(), list.end(),
                       [&] (int lhs, int rhs)
                       {
                           const auto a = atomList[(size_t) lhs].pos - origin;
                           const auto b = atomList[(size_t) rhs].pos - origin;
                           return std::atan2 (a.y, a.x) < std::atan2 (b.y, b.x);
                       });
        };

        for (size_t c = 0; c < n; ++c)
        {
            if (atomList[c].isHydrogen)
                continue;   // a hydrogen has a single bond, no angle to keep

            const auto& all = adjacency[c];
            if (all.size() < 2)
                continue;

            // Two variants of the ideal angle: between two heavy substituents
            // (methylated reference values) and towards a hydrogen (hydride
            // reference values). Oxygen and sulfur differ noticeably here.
            const float heavyIdeal = idealBondAngle ((int) c, true);
            const float lightIdeal = idealBondAngle ((int) c, false);

            // --- Pass A: the heavy-atom skeleton ---
            std::vector<int> heavy;
            for (int j : all)
                if (! atomList[(size_t) j].isHydrogen)
                    heavy.push_back (j);

            if (heavy.size() >= 2)
            {
                sortByAngle ((int) c, heavy);

                const int count = (int) heavy.size();

                // Two heavy neighbours: one angle, straight from hybridisation.
                // Three or more: they must share 360 deg, so cap the target.
                const float generic = (count == 2)
                    ? heavyIdeal
                    : juce::jmin (heavyIdeal,
                                  juce::MathConstants<float>::twoPi / (float) count);

                const int pairCount = (count == 2) ? 1 : count;
                for (int k = 0; k < pairCount; ++k)
                {
                    const int i1 = heavy[(size_t) k];
                    const int i2 = heavy[(size_t) ((k + 1) % count)];

                    // A pair that closes a ring must use the polygon interior
                    // angle instead, otherwise the ring cannot close and the
                    // solver folds one vertex inwards.
                    const float ringTarget = ringAngleTarget ((int) c, i1, i2);
                    const float target = (ringTarget > 0.0f) ? ringTarget : generic;

                    applyAngleForce ((int) c, i1, i2, target, kAngleHeavyK);
                }
            }

            // --- Pass B: everything, but only the pairs that involve a hydrogen ---
            std::vector<int> ordered = all;
            sortByAngle ((int) c, ordered);

            const int total = (int) ordered.size();
            const float hTarget = juce::jmin (lightIdeal,
                                              juce::MathConstants<float>::twoPi
                                                  / (float) total);

            const int pairCount = (total == 2) ? 1 : total;
            for (int k = 0; k < pairCount; ++k)
            {
                const int i1 = ordered[(size_t) k];
                const int i2 = ordered[(size_t) ((k + 1) % total)];

                const bool involvesH = atomList[(size_t) i1].isHydrogen
                                    || atomList[(size_t) i2].isHydrogen;
                if (! involvesH)
                    continue;   // already handled by pass A

                applyAngleForce ((int) c, i1, i2, hTarget, kAngleHydrogenK);
            }
        }
    }

    // 2b) Ring templates.
    //
    //     Angles alone do not pin down a ring: acos() is unsigned, so a vertex
    //     bent the "wrong" way scores exactly the same energy as the correct
    //     one. That degeneracy is what lets a hexagon relax into a heart or a
    //     dented polygon, especially when the solver starts from a restored
    //     state rather than from an incrementally drawn one.
    //
    //     Constraining every ring member to the circumcircle of the regular
    //     polygon with the same side length removes the mirrored solution
    //     entirely. This is the standard 2D-depiction approach: structure
    //     drawing packages (RDKit's coordinate generator among them) place
    //     rings from templates first and only then relax the substituents.
    {
        auto bondOrderBetween = [&] (int i, int j) -> int
        {
            for (const auto& b : bondList)
                if ((b.a == i && b.b == j) || (b.a == j && b.b == i))
                    return b.order;
            return 1;
        };

        for (const auto& ring : rings())
        {
            const int m = (int) ring.size();
            if (m < 3)
                continue;

            juce::Point<float> centre { 0.0f, 0.0f };
            for (int idx : ring)
                centre += atomList[(size_t) idx].pos;
            centre /= (float) m;

            // Mean ideal side length, honouring the shorter multiple bonds.
            float sideSum = 0.0f;
            for (int k = 0; k < m; ++k)
            {
                const int order = bondOrderBetween (ring[(size_t) k],
                                                    ring[(size_t) ((k + 1) % m)]);
                sideSum += kHeavyBondLength
                         * (order == 2 ? 0.88f : (order >= 3 ? 0.79f : 1.0f));
            }

            const float side = sideSum / (float) m;
            const float radius = side
                / (2.0f * std::sin (juce::MathConstants<float>::pi / (float) m));

            for (int idx : ring)
            {
                auto d = atomList[(size_t) idx].pos - centre;
                float len = d.getDistanceFromOrigin();

                if (len < 1.0e-3f)
                {
                    d = { 1.0f, 0.0f };
                    len = 1.0f;
                }

                force[(size_t) idx] += (d / len) * ((radius - len) * kRingTemplateK);
            }
        }
    }

    // 3) Non-bonded repulsion, with 1-2 and 1-3 pairs excluded.
    //
    //    Real force fields skip van der Waals between directly bonded atoms
    //    and between the two ends of a bond angle, because those distances are
    //    already governed by the bond and angle terms. Doing the same here is
    //    what stops repulsion from flattening every angle out to 180 deg.
    //    1-4 pairs are kept, and that residual repulsion is exactly what makes
    //    an extended chain prefer the anti (trans zig-zag) conformation.
    {
        std::vector<std::uint8_t> excluded (n * n, 0u);

        auto exclude = [&] (int i, int j)
        {
            excluded[(size_t) i * n + (size_t) j] = 1u;
            excluded[(size_t) j * n + (size_t) i] = 1u;
        };

        for (const auto& b : bondList)          // 1-2
            exclude (b.a, b.b);

        for (size_t c = 0; c < n; ++c)          // 1-3
        {
            const auto& nb = adjacency[c];
            for (size_t x = 0; x < nb.size(); ++x)
                for (size_t y = x + 1; y < nb.size(); ++y)
                    exclude (nb[x], nb[y]);
        }

        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = i + 1; j < n; ++j)
            {
                if (excluded[i * n + j] != 0u)
                    continue;

                auto d = atomList[j].pos - atomList[i].pos;
                float len2 = d.x * d.x + d.y * d.y;

                if (len2 < 0.01f)
                {
                    d = { 0.9f, 0.4f };
                    len2 = d.x * d.x + d.y * d.y;
                }

                const float len = std::sqrt (len2);
                const auto dir = d / len;

                const bool involvesH = atomList[i].isHydrogen || atomList[j].isHydrogen;
                const float k = involvesH ? kRepulsionK * 0.35f : kRepulsionK;
                const float f = k / juce::jmax (len2, 36.0f);

                force[i] -= dir * f;
                force[j] += dir * f;
            }
        }
    }

    // 4) Gentle re-centring: pull the whole molecule back towards the origin
    //    without distorting its shape. Every heavy atom shares one force based
    //    on the centroid, so internal springs and repulsion are free to spread
    //    the structure naturally while the molecule as a whole stays centred.
    juce::Point<float> centroid { 0.0f, 0.0f };
    int heavyCount = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (atomList[i].isHydrogen)
            continue;
        centroid += atomList[i].pos;
        ++heavyCount;
    }

    if (heavyCount > 0)
    {
        const juce::Point<float> recentre = (centroid / (float) heavyCount) * -kCentringK;
        for (size_t i = 0; i < n; ++i)
            if (! atomList[i].isHydrogen)
                force[i] += recentre;
    }

    // 5) Integrate with damping and a speed clamp.
    float totalMotion = 0.0f;

    for (size_t i = 0; i < n; ++i)
    {
        auto& a = atomList[i];
        a.velocity = (a.velocity + force[i] * dt) * kDamping;

        const float speed = a.velocity.getDistanceFromOrigin();
        constexpr float maxSpeed = 9.0f;
        if (speed > maxSpeed)
            a.velocity *= maxSpeed / speed;

        a.pos += a.velocity * dt;
        totalMotion += std::abs (a.velocity.x) + std::abs (a.velocity.y);
    }

    const float avgMotion = totalMotion / (float) n;
    settling = avgMotion > kSettleThreshold;

    return avgMotion;
}

void Molecule::advanceAnimations (float dt)
{
    for (auto& a : atomList)
        if (a.spawnAnim < 1.0f)
            a.spawnAnim = juce::jmin (1.0f, a.spawnAnim + dt * 0.09f);

    for (auto& b : bondList)
        if (b.orderAnim < 1.0f)
            b.orderAnim = juce::jmin (1.0f, b.orderAnim + dt * 0.12f);
}

juce::Rectangle<float> Molecule::boundingBox() const
{
    if (atomList.empty())
        return {};

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (const auto& a : atomList)
    {
        const float r = elementInfo (a.element).radius;
        minX = juce::jmin (minX, a.pos.x - r);
        minY = juce::jmin (minY, a.pos.y - r);
        maxX = juce::jmax (maxX, a.pos.x + r);
        maxY = juce::jmax (maxY, a.pos.y + r);
    }

    return { minX, minY, maxX - minX, maxY - minY };
}

// ---------------------------------------------------------------------------
//  Formula
// ---------------------------------------------------------------------------

juce::String Molecule::formula() const
{
    std::map<juce::String, int> counts;

    for (const auto& a : atomList)
        counts[elementInfo (a.element).symbol]++;

    juce::String out;

    // Hill system: carbon first, hydrogen second, remaining elements alphabetical.
    auto append = [&out, &counts] (const juce::String& sym)
    {
        auto it = counts.find (sym);
        if (it == counts.end())
            return;

        out << sym;
        if (it->second > 1)
            out << it->second;
        counts.erase (it);
    };

    append ("C");
    append ("H");

    for (const auto& kv : counts)
    {
        out << kv.first;
        if (kv.second > 1)
            out << kv.second;
    }

    return out.isEmpty() ? juce::String ("-") : out;
}

// ---------------------------------------------------------------------------
//  Longest heavy-atom chain (double BFS, the classic tree-diameter trick)
// ---------------------------------------------------------------------------

std::vector<int> Molecule::longestChain() const
{
    std::vector<int> heavy;
    for (size_t i = 0; i < atomList.size(); ++i)
        if (! atomList[i].isHydrogen)
            heavy.push_back ((int) i);

    if (heavy.empty())
        return {};
    if (heavy.size() == 1)
        return heavy;

    // Adjacency restricted to heavy atoms.
    std::map<int, std::vector<int>> adjacency;
    for (int h : heavy)
        adjacency[h] = {};

    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;
        adjacency[b.a].push_back (b.b);
        adjacency[b.b].push_back (b.a);
    }

    // BFS returning the farthest node plus the parent map, so the path can be
    // reconstructed afterwards.
    auto bfs = [&adjacency] (int source, std::map<int, int>& parent) -> int
    {
        std::map<int, int> dist;
        std::queue<int> q;

        q.push (source);
        dist[source] = 0;
        parent[source] = -1;

        int farthest = source;
        int bestDist = 0;

        while (! q.empty())
        {
            const int cur = q.front();
            q.pop();

            for (int nb : adjacency[cur])
            {
                if (dist.find (nb) == dist.end())
                {
                    dist[nb] = dist[cur] + 1;
                    parent[nb] = cur;
                    q.push (nb);

                    if (dist[nb] > bestDist)
                    {
                        bestDist = dist[nb];
                        farthest = nb;
                    }
                }
            }
        }
        return farthest;
    };

    std::map<int, int> parentA;
    const int endA = bfs (heavy.front(), parentA);

    std::map<int, int> parentB;
    const int endB = bfs (endA, parentB);

    // Walk back from endB to endA to recover the chain.
    std::vector<int> chain;
    for (int cur = endB; cur != -1; cur = parentB[cur])
        chain.push_back (cur);

    std::reverse (chain.begin(), chain.end());
    return chain;
}

// ---------------------------------------------------------------------------
//  Ring detection
// ---------------------------------------------------------------------------

bool Molecule::hasRing() const
{
    int heavyCount = 0;
    int heavyBondCount = 0;

    for (const auto& a : atomList)
        if (! a.isHydrogen)
            ++heavyCount;

    for (const auto& b : bondList)
        if (! atomList[(size_t) b.a].isHydrogen && ! atomList[(size_t) b.b].isHydrogen)
            ++heavyBondCount;

    // The heavy-atom graph is always kept connected, so it is a tree exactly
    // when it has one bond fewer than it has atoms. Any extra bond closes a
    // ring (double/triple bonds still count as a single edge here).
    return heavyCount >= 2 && heavyBondCount >= heavyCount;
}

// ---------------------------------------------------------------------------
//  Ring structural formula (SMILES-style, ring-closure numbers)
// ---------------------------------------------------------------------------

juce::String Molecule::ringStructuralFormula() const
{
    // Map every heavy atom to a compact 0..n-1 index.
    const size_t totalAtoms = atomList.size();
    std::vector<int> local (totalAtoms, -1);
    std::vector<int> heavy;   // heavy[local] = original atom index

    for (size_t i = 0; i < totalAtoms; ++i)
    {
        if (atomList[i].isHydrogen)
            continue;
        local[i] = (int) heavy.size();
        heavy.push_back ((int) i);
    }

    const int n = (int) heavy.size();
    if (n == 0)
        return "-";
    if (n == 1)
        return elementInfo (atomList[(size_t) heavy[0]].element).symbol;

    // Heavy-atom bonds in compact indices.
    struct HeavyBond { int a; int b; int order; };
    std::vector<HeavyBond> hBonds;

    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;
        hBonds.push_back ({ local[(size_t) b.a], local[(size_t) b.b], b.order });
    }

    // Adjacency list: adjacency[u] = { (neighbour, heavyBondIndex) }.
    std::vector<std::vector<std::pair<int, int>>> adjacency ((size_t) n);
    for (size_t i = 0; i < hBonds.size(); ++i)
    {
        adjacency[(size_t) hBonds[i].a].emplace_back (hBonds[i].b, (int) i);
        adjacency[(size_t) hBonds[i].b].emplace_back (hBonds[i].a, (int) i);
    }

    // --- Pass 1: find rings (back edges) with a depth-first search ---
    struct Ring { int open; int close; int bondIndex; int number; };
    std::vector<Ring> rings;

    std::vector<int> colour ((size_t) n, 0);   // 0 white, 1 grey, 2 black
    std::vector<int> parentBond ((size_t) n, -1);

    std::function<void (int)> detect = [&] (int u)
    {
        colour[(size_t) u] = 1;
        for (const auto& edge : adjacency[(size_t) u])
        {
            const int v = edge.first;
            const int bi = edge.second;

            if (bi == parentBond[(size_t) u])
                continue;

            if (colour[(size_t) v] == 0)
            {
                parentBond[(size_t) v] = bi;
                detect (v);
            }
            else if (colour[(size_t) v] == 1)
            {
                // Back edge u -> v: v (the ancestor) opens the ring, u closes it.
                rings.push_back ({ v, u, bi, 0 });
            }
        }
        colour[(size_t) u] = 2;
    };

    for (int u = 0; u < n; ++u)
        if (colour[(size_t) u] == 0)
            detect (u);

    for (size_t i = 0; i < rings.size(); ++i)
        rings[i].number = (int) i + 1;

    // Ring-closure numbers attached to each end of every ring.
    std::vector<std::vector<int>> openRings ((size_t) n);
    std::vector<std::vector<int>> closeRings ((size_t) n);

    for (const auto& r : rings)
    {
        openRings[(size_t) r.open].push_back (r.number);
        closeRings[(size_t) r.close].push_back (r.number);
    }

    // Back edges are already encoded as ring numbers, so skip them while walking.
    std::vector<bool> isBackEdge (hBonds.size(), false);
    for (const auto& r : rings)
        isBackEdge[(size_t) r.bondIndex] = true;

    // --- Pass 2: emit the SMILES-style string ---
    auto bondSymbol = [] (int order) -> juce::String
    {
        if (order == 2)
            return "=";
        if (order >= 3)
            return "#";
        return {};   // single bonds are implicit
    };

    std::vector<bool> visited ((size_t) n, false);
    juce::String out;

    std::function<void (int, int)> emit = [&] (int u, int from)
    {
        visited[(size_t) u] = true;

        out << elementInfo (atomList[(size_t) heavy[(size_t) u]].element).symbol;

        for (int rn : openRings[(size_t) u])
            out << rn;
        for (int rn : closeRings[(size_t) u])
            out << rn;

        // Gather the tree children (skip parent, back edges and visited nodes).
        std::vector<std::pair<int, int>> children;
        for (const auto& edge : adjacency[(size_t) u])
        {
            const int v = edge.first;
            const int bi = edge.second;
            if (v == from || isBackEdge[(size_t) bi] || visited[(size_t) v])
                continue;
            children.emplace_back (v, bi);
        }

        for (size_t i = 0; i < children.size(); ++i)
        {
            const int v = children[i].first;
            const int bi = children[i].second;

            if (i > 0)
                out << "(";
            out << bondSymbol (hBonds[(size_t) bi].order);
            emit (v, u);
            if (i > 0)
                out << ")";
        }
    };

    emit (0, -1);

    return out;
}

// ---------------------------------------------------------------------------
//  Condensed structural formula  (problem 6)
// ---------------------------------------------------------------------------

juce::String Molecule::structuralFormula() const
{
    if (hasRing())
        return ringStructuralFormula();

    const auto chain = longestChain();
    if (chain.empty())
        return "-";

    // Hydrogen count attached to a given heavy atom.
    auto hydrogenCount = [this] (int atomIdx) -> int
    {
        int n = 0;
        for (const auto& b : bondList)
        {
            const int other = (b.a == atomIdx) ? b.b : (b.b == atomIdx ? b.a : -1);
            if (other >= 0 && atomList[(size_t) other].isHydrogen)
                ++n;
        }
        return n;
    };

    // Heavy neighbours that are not part of the main chain, rendered as
    // parenthesised substituents.
    auto branchesOf = [this, &chain] (int atomIdx) -> std::vector<int>
    {
        std::vector<int> out;
        for (const auto& b : bondList)
        {
            const int other = (b.a == atomIdx) ? b.b : (b.b == atomIdx ? b.a : -1);
            if (other < 0 || atomList[(size_t) other].isHydrogen)
                continue;
            if (std::find (chain.begin(), chain.end(), other) == chain.end())
                out.push_back (other);
        }
        return out;
    };

    // Bond order between two adjacent heavy atoms.
    auto orderBetween = [this] (int i, int j) -> int
    {
        for (const auto& b : bondList)
            if ((b.a == i && b.b == j) || (b.a == j && b.b == i))
                return b.order;
        return 1;
    };

    // Render one heavy atom as e.g. "CH3", "NH2", "O".
    auto renderAtom = [this, &hydrogenCount] (int atomIdx) -> juce::String
    {
        juce::String s (elementInfo (atomList[(size_t) atomIdx].element).symbol);
        const int nH = hydrogenCount (atomIdx);

        if (nH == 1)
            s << "H";
        else if (nH > 1)
            s << "H" << nH;

        return s;
    };

    juce::String out;

    for (size_t k = 0; k < chain.size(); ++k)
    {
        const int idx = chain[k];
        out << renderAtom (idx);

        // Substituents hanging off this chain position.
        for (int branch : branchesOf (idx))
        {
            const int order = orderBetween (idx, branch);
            const juce::String link = (order == 2) ? "=" : (order >= 3 ? "#" : "");
            out << "(" << link << renderAtom (branch) << ")";
        }

        // Bond symbol towards the next chain atom: - single, = double, # triple.
        if (k + 1 < chain.size())
        {
            const int order = orderBetween (idx, chain[k + 1]);
            out << (order == 2 ? "=" : (order >= 3 ? "#" : "-"));
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
//  Canonical SMILES + common-name lookup
// ---------------------------------------------------------------------------

namespace
{

/** Atomic number used to order elements in the canonical form (C < N < O <
    P < S, matching IUPAC atomic numbers). */
int atomicNumber (Element e)
{
    switch (e)
    {
        case Element::Carbon:     return 6;
        case Element::Nitrogen:   return 7;
        case Element::Oxygen:     return 8;
        case Element::Phosphorus: return 15;
        case Element::Sulfur:     return 16;
        default:                  return 0;   // Hydrogen
    }
}

/** Common substance entries: canonical SMILES -> English common name.

    The SMILES strings must match the exact output of canonicalSmiles()
    below (Kekulé, implicit hydrogens, endpoint-first walk). Keep the list
    small and focused on substances representable with the C / O / N / S / P
    palette. Multi-ring / heteroatom-bridged aromatics (e.g. diphenyl ether)
    are deliberately omitted until a proper canonicaliser (RDKit) is
    integrated.
*/
struct CommonSubstance
{
    const char* smiles;
    const char* name;
};

const CommonSubstance kCommonSubstances[] = {
    { "O",            "Water" },
    { "C",            "Methane" },
    { "N",            "Ammonia" },
    { "S",            "Hydrogen sulfide" },
    { "P",            "Phosphine" },
    { "O=O",          "Oxygen" },
    { "N#N",          "Nitrogen" },
    { "OO",           "Hydrogen peroxide" },
    { "O=C=O",        "Carbon dioxide" },
    { "S=C=S",        "Carbon disulfide" },
    { "O=S=O",        "Sulfur dioxide" },
    { "CO",           "Methanol" },
    { "C=O",          "Formaldehyde" },
    { "CC",           "Ethane" },
    { "C=C",          "Ethylene" },
    { "C#C",          "Acetylene" },
    { "CCO",          "Ethanol" },
    { "CCC",          "Propane" },
    { "COC",          "Dimethyl ether" },
    { "CN",           "Methylamine" },
    { "CC=O(O)",      "Acetic acid" },
    { "C1=CC=CC=C1",  "Benzene" },
    { "C1CC1",        "Cyclopropane" },
    { "C1CCC1",       "Cyclobutane" },
    { "C1CCCC1",      "Cyclopentane" },
    { "C1CCCCC1",     "Cyclohexane" },
};

}  // namespace

juce::String Molecule::canonicalSmiles() const
{
    const size_t total = atomList.size();

    std::vector<int> local (total, -1);
    std::vector<int> heavy;   // heavy[local] = original atom index

    for (size_t i = 0; i < total; ++i)
    {
        if (atomList[i].isHydrogen)
            continue;
        local[i] = (int) heavy.size();
        heavy.push_back ((int) i);
    }

    const int n = (int) heavy.size();
    if (n == 0)
        return {};
    if (n == 1)
        return elementInfo (atomList[(size_t) heavy[0]].element).symbol;

    // Heavy-atom bonds in compact indices.
    struct HeavyBond { int a; int b; int order; };
    std::vector<HeavyBond> hBonds;

    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;
        hBonds.push_back ({ local[(size_t) b.a], local[(size_t) b.b], b.order });
    }

    // Adjacency: adjacency[u] = { (neighbour, heavyBondIndex) }.
    std::vector<std::vector<std::pair<int, int>>> adjacency ((size_t) n);
    for (size_t i = 0; i < hBonds.size(); ++i)
    {
        adjacency[(size_t) hBonds[i].a].emplace_back (hBonds[i].b, (int) i);
        adjacency[(size_t) hBonds[i].b].emplace_back (hBonds[i].a, (int) i);
    }

    // Hydrogen count per heavy atom, used as part of the invariant.
    auto hydrogenCount = [this] (int atomIdx) -> int
    {
        int cnt = 0;
        for (const auto& b : bondList)
        {
            const int other = (b.a == atomIdx) ? b.b : (b.b == atomIdx ? b.a : -1);
            if (other >= 0 && atomList[(size_t) other].isHydrogen)
                ++cnt;
        }
        return cnt;
    };

    // Invariant: (degree, atomic number, bond orders desc, hydrogen count).
    // Endpoint atoms (degree 1) sort first, so chains start at their end.
    auto invariant = [&] (int u)
    {
        std::vector<int> orders;
        for (const auto& e : adjacency[(size_t) u])
            orders.push_back (hBonds[(size_t) e.second].order);
        std::sort (orders.begin(), orders.end(), std::greater<int>());

        return std::make_tuple ((int) adjacency[(size_t) u].size(),
                                atomicNumber (atomList[(size_t) heavy[(size_t) u]].element),
                                orders,
                                hydrogenCount (heavy[(size_t) u]));
    };

    int start = 0;
    auto bestInvariant = invariant (0);
    for (int u = 1; u < n; ++u)
    {
        const auto inv = invariant (u);
        if (inv < bestInvariant)
        {
            bestInvariant = inv;
            start = u;
        }
    }

    // Deterministic neighbour order: higher bond order first, then smaller
    // atomic number, then lower index (keeps the walk canonical).
    auto neighbourLess = [&] (const std::pair<int, int>& x, const std::pair<int, int>& y)
    {
        const int ox = hBonds[(size_t) x.second].order;
        const int oy = hBonds[(size_t) y.second].order;
        if (ox != oy)
            return ox > oy;

        const int ex = atomicNumber (atomList[(size_t) heavy[(size_t) x.first]].element);
        const int ey = atomicNumber (atomList[(size_t) heavy[(size_t) y.first]].element);
        if (ex != ey)
            return ex < ey;

        return x.first < y.first;
    };

    for (int u = 0; u < n; ++u)
        std::sort (adjacency[(size_t) u].begin(), adjacency[(size_t) u].end(), neighbourLess);

    // --- Pass 1: find rings as DFS back edges ---
    struct Ring { int open; int close; int bondIndex; int number; };
    std::vector<Ring> rings;

    std::vector<int> colour ((size_t) n, 0);   // 0 white, 1 grey, 2 black
    std::vector<int> parentBond ((size_t) n, -1);

    std::function<void (int)> detect = [&] (int u)
    {
        colour[(size_t) u] = 1;
        for (const auto& edge : adjacency[(size_t) u])
        {
            const int v = edge.first;
            const int bi = edge.second;

            if (bi == parentBond[(size_t) u])
                continue;

            if (colour[(size_t) v] == 0)
            {
                parentBond[(size_t) v] = bi;
                detect (v);
            }
            else if (colour[(size_t) v] == 1)
            {
                rings.push_back ({ v, u, bi, 0 });
            }
        }
        colour[(size_t) u] = 2;
    };

    detect (start);
    for (int u = 0; u < n; ++u)
        if (colour[(size_t) u] == 0)
            detect (u);

    for (size_t i = 0; i < rings.size(); ++i)
        rings[i].number = (int) i + 1;

    std::vector<std::vector<int>> openRings ((size_t) n);
    std::vector<std::vector<int>> closeRings ((size_t) n);

    for (const auto& r : rings)
    {
        openRings[(size_t) r.open].push_back (r.number);
        closeRings[(size_t) r.close].push_back (r.number);
    }

    std::vector<bool> isBackEdge (hBonds.size(), false);
    for (const auto& r : rings)
        isBackEdge[(size_t) r.bondIndex] = true;

    // --- Pass 2: emit ---
    auto bondSymbol = [] (int order) -> juce::String
    {
        if (order == 2)
            return "=";
        if (order >= 3)
            return "#";
        return {};
    };

    std::vector<bool> visited ((size_t) n, false);
    juce::String out;

    std::function<void (int, int)> emit = [&] (int u, int from)
    {
        visited[(size_t) u] = true;

        out << elementInfo (atomList[(size_t) heavy[(size_t) u]].element).symbol;

        for (int rn : openRings[(size_t) u])
            out << rn;
        for (int rn : closeRings[(size_t) u])
            out << rn;

        std::vector<std::pair<int, int>> children;
        for (const auto& edge : adjacency[(size_t) u])
        {
            const int v = edge.first;
            const int bi = edge.second;
            if (v == from || isBackEdge[(size_t) bi] || visited[(size_t) v])
                continue;
            children.emplace_back (v, bi);
        }

        for (size_t i = 0; i < children.size(); ++i)
        {
            const int v = children[i].first;
            const int bi = children[i].second;

            if (i > 0)
                out << "(";
            out << bondSymbol (hBonds[(size_t) bi].order);
            emit (v, u);
            if (i > 0)
                out << ")";
        }
    };

    emit (start, -1);

    return out;
}

juce::String Molecule::commonName() const
{
    const juce::String smiles = canonicalSmiles();
    if (smiles.isEmpty())
        return {};

    // The organic library is keyed by canonical SMILES, but those strings are
    // *derived*, not hand-written: every entry in namedMolecules() is actually
    // built here once and asked for its own canonical SMILES. Hand-writing the
    // strings was the old approach and it silently rotted — a preset the user
    // could load would come back unnamed because the hand-written SMILES did
    // not match what the writer produced for a ring or a branch. Deriving them
    // makes that class of bug impossible.
    static const std::map<juce::String, juce::String> derived = []
    {
        std::map<juce::String, juce::String> table;

        for (const auto& entry : namedMolecules())
        {
            Molecule m;
            m.fromValueTree (topologyToValueTree (entry));

            const juce::String key = m.canonicalSmiles();
            if (key.isNotEmpty())
                table.emplace (key, juce::String (entry.name));
        }

        return table;
    }();

    const auto it = derived.find (smiles);
    if (it != derived.end())
        return it->second;

    // Small inorganic species that are not worth a topology entry.
    for (const auto& entry : kCommonSubstances)
        if (smiles == entry.smiles)
            return entry.name;

    return {};
}

float Molecule::molecularWeight() const
{
    float m = 0.0f;
    for (const auto& a : atomList)
        m += elementInfo (a.element).weight;
    return m;
}

// ---------------------------------------------------------------------------
//  Chemical descriptors (for the audio mapping layer)
// ---------------------------------------------------------------------------

ChemicalDescriptors Molecule::computeDescriptors() const
{
    ChemicalDescriptors d;

    const int n = (int) atomList.size();

    // Per-heavy-atom degree over the heavy skeleton (hydrogen excluded).
    std::vector<int> degree ((size_t) n, 0);

    int heavyEdgeCount = 0;   // heavy-atom bonds, one per Bond (order ignored)

    for (size_t i = 0; i < atomList.size(); ++i)
    {
        const auto& a = atomList[i];
        if (a.isHydrogen)
            continue;

        ++d.heavyAtomCount;

        if (a.element == Element::Carbon)
            ++d.carbonCount;
        else
            ++d.heteroAtomCount;
    }

    for (const auto& b : bondList)
    {
        const auto& A = atomList[(size_t) b.a];
        const auto& B = atomList[(size_t) b.b];

        // Hydrogen bonds do not count towards the heavy skeleton's ring
        // structure, branching or multiple-bond statistics.
        if (A.isHydrogen || B.isHydrogen)
            continue;

        ++heavyEdgeCount;
        ++degree[(size_t) b.a];
        ++degree[(size_t) b.b];

        if (b.order == 2)
            ++d.doubleBondCount;
        else if (b.order >= 3)
            ++d.tripleBondCount;
    }

    // Ring count from the cyclomatic number: for a connected graph,
    //   rings = edges - vertices + 1
    // The molecule is always kept connected (single fragment), so this holds.
    if (d.heavyAtomCount > 0)
        d.ringCount = heavyEdgeCount - d.heavyAtomCount + 1;

    // Branch points: heavy atoms bonded to three or more other heavy atoms.
    for (int i = 0; i < n; ++i)
        if (! atomList[(size_t) i].isHydrogen && degree[(size_t) i] >= 3)
            ++d.branchCount;

    // Longest heavy-atom chain (reuses the tree-diameter walker).
    d.longestChainLength = (int) longestChain().size();

    d.molecularWeight = molecularWeight();

    // Degree of unsaturation: rings + pi bonds (a triple bond contributes 2).
    d.degreeOfUnsaturation = d.ringCount + d.doubleBondCount + 2 * d.tripleBondCount;

    if (d.heavyAtomCount == 0)
        return d;

    // -----------------------------------------------------------------------
    //  Helper lambdas shared by the descriptors below
    // -----------------------------------------------------------------------

    // Hydrogens attached to a heavy atom.
    auto hydrogenCountOn = [this] (int idx)
    {
        int h = 0;
        for (const auto& b : bondList)
        {
            const int other = (b.a == idx) ? b.b : (b.b == idx ? b.a : -1);
            if (other >= 0 && atomList[(size_t) other].isHydrogen)
                ++h;
        }
        return h;
    };

    // Highest bond order this heavy atom participates in (heavy bonds only).
    auto maxBondOrderOn = [this] (int idx)
    {
        int m = 1;
        for (const auto& b : bondList)
        {
            if (b.a != idx && b.b != idx)
                continue;
            const int other = (b.a == idx) ? b.b : b.a;
            if (atomList[(size_t) other].isHydrogen)
                continue;
            m = juce::jmax (m, b.order);
        }
        return m;
    };

    // -----------------------------------------------------------------------
    //  Ring perception, ring size and Huckel aromaticity
    // -----------------------------------------------------------------------

    const auto rings = perceiveRings();

    std::vector<bool> inAromaticRing ((size_t) n, false);

    for (const auto& ring : rings)
    {
        d.maxRingSize = juce::jmax (d.maxRingSize, (int) ring.size());

        // Huckel's rule: a planar, fully conjugated ring is aromatic when its
        // pi-electron count is 4n + 2.
        //
        // Contributions counted here:
        //   - each double bond whose *both* ends lie in the ring: 2 electrons
        //   - each ring heteroatom with no ring double bond: 2 electrons from
        //     its lone pair (this is what makes furan / pyrrole aromatic)
        //
        // Every ring atom must also be able to go planar sp2, i.e. it either
        // carries a ring pi bond or contributes a lone pair.
        if (ring.size() < 3 || ring.size() > 8)
            continue;

        int piElectrons = 0;
        bool conjugated = true;

        std::vector<bool> hasRingPiBond (ring.size(), false);

        for (size_t i = 0; i < ring.size(); ++i)
        {
            const int a = ring[i];
            const int b = ring[(i + 1) % ring.size()];

            for (const auto& bond : bondList)
            {
                const bool matches = (bond.a == a && bond.b == b)
                                  || (bond.a == b && bond.b == a);
                if (! matches)
                    continue;

                if (bond.order == 2)
                {
                    piElectrons += 2;
                    hasRingPiBond[i] = true;
                    hasRingPiBond[(i + 1) % ring.size()] = true;
                }
                else if (bond.order >= 3)
                {
                    // A triple bond cannot sit in a small planar ring.
                    conjugated = false;
                }
                break;
            }
        }

        for (size_t i = 0; i < ring.size() && conjugated; ++i)
        {
            if (hasRingPiBond[i])
                continue;

            const auto element = atomList[(size_t) ring[i]].element;
            const bool heteroWithLonePair = (element == Element::Oxygen
                                          || element == Element::Nitrogen
                                          || element == Element::Sulfur);

            if (heteroWithLonePair)
                piElectrons += 2;      // lone pair joins the pi system
            else
                conjugated = false;    // an sp3 carbon breaks conjugation
        }

        if (conjugated && piElectrons > 0 && (piElectrons - 2) % 4 == 0)
        {
            ++d.aromaticRingCount;
            for (int idx : ring)
                inAromaticRing[(size_t) idx] = true;
        }
    }

    // -----------------------------------------------------------------------
    //  Rotatable bonds (Veber-style)
    //
    //  A single, acyclic bond between two heavy atoms that each carry at least
    //  one further heavy neighbour. Terminal bonds (methyl groups) do not
    //  create a new conformation, so they are excluded.
    // -----------------------------------------------------------------------

    std::vector<bool> bondInRing ((size_t) bondList.size(), false);
    for (const auto& ring : rings)
    {
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const int a = ring[i];
            const int b = ring[(i + 1) % ring.size()];

            for (size_t bi = 0; bi < bondList.size(); ++bi)
            {
                const auto& bond = bondList[bi];
                if ((bond.a == a && bond.b == b) || (bond.a == b && bond.b == a))
                    bondInRing[bi] = true;
            }
        }
    }

    for (size_t bi = 0; bi < bondList.size(); ++bi)
    {
        const auto& bond = bondList[bi];
        if (atomList[(size_t) bond.a].isHydrogen || atomList[(size_t) bond.b].isHydrogen)
            continue;
        if (bond.order != 1 || bondInRing[bi])
            continue;
        if (degree[(size_t) bond.a] < 2 || degree[(size_t) bond.b] < 2)
            continue;

        ++d.rotatableBondCount;
    }

    // -----------------------------------------------------------------------
    //  Lipinski hydrogen-bond donors / acceptors, TPSA, cLogP, Fsp3
    // -----------------------------------------------------------------------

    float tpsa = 0.0f;
    float logP = 0.0f;
    int   sp3Carbons = 0;

    for (int i = 0; i < n; ++i)
    {
        const auto& atom = atomList[(size_t) i];
        if (atom.isHydrogen)
            continue;

        const int nH = hydrogenCountOn (i);
        const int maxOrder = maxBondOrderOn (i);
        const bool aromatic = inAromaticRing[(size_t) i];

        switch (atom.element)
        {
            case Element::Nitrogen:
            {
                ++d.hBondAcceptorCount;
                if (nH > 0)
                    d.hBondDonorCount += nH;

                // TPSA fragment contributions (Ertl et al. 2000, Table 1).
                if (aromatic)             tpsa += (nH > 0 ? 15.79f : 12.89f);
                else if (maxOrder >= 3)   tpsa += 23.79f;
                else if (maxOrder == 2)   tpsa += 12.36f;
                else if (nH == 0)         tpsa += 3.24f;
                else if (nH == 1)         tpsa += 12.03f;
                else                      tpsa += 26.02f;

                // Crippen-style contribution (representative of the N types).
                logP += aromatic ? -0.3187f : (nH > 0 ? -1.0190f : -0.7096f);
                break;
            }

            case Element::Oxygen:
            {
                ++d.hBondAcceptorCount;
                if (nH > 0)
                    d.hBondDonorCount += nH;

                if (aromatic)             tpsa += 13.14f;
                else if (maxOrder == 2)   tpsa += 17.07f;   // carbonyl =O
                else if (nH > 0)          tpsa += 20.23f;   // hydroxyl -OH
                else                      tpsa += 9.23f;    // ether -O-

                logP += (maxOrder == 2) ? -0.1526f : (nH > 0 ? -0.2893f : 0.1129f);
                break;
            }

            case Element::Sulfur:
            {
                if (maxOrder == 2)        tpsa += 32.09f;   // =S
                else if (nH > 0)          tpsa += 38.80f;   // thiol -SH
                else                      tpsa += 25.30f;   // thioether -S-

                logP += 0.6482f;
                break;
            }

            case Element::Phosphorus:
            {
                if (nH > 0)               tpsa += 23.47f;
                else if (maxOrder == 2)   tpsa += 9.81f;
                else                      tpsa += 13.59f;

                logP += -0.3260f;
                break;
            }

            case Element::Carbon:
            {
                // Fsp3: a carbon is sp3 when its steric number is 4, i.e. it
                // has four sigma domains and no pi bond.
                if (stericNumber (i) >= 4)
                    ++sp3Carbons;

                // Crippen-style carbon types: aromatic / bonded to hetero /
                // unsaturated / plain aliphatic.
                bool bondedToHetero = false;
                for (const auto& bond : bondList)
                {
                    const int other = (bond.a == i) ? bond.b : (bond.b == i ? bond.a : -1);
                    if (other < 0)
                        continue;
                    const auto oe = atomList[(size_t) other].element;
                    if (oe != Element::Carbon && oe != Element::Hydrogen)
                        bondedToHetero = true;
                }

                if (aromatic)             logP += 0.1581f;
                else if (bondedToHetero)  logP += -0.2035f;
                else if (maxOrder >= 2)   logP += 0.0000f;
                else                      logP += 0.1441f;
                break;
            }

            default:
                break;
        }

        // Hydrogen contributions: non-polar H on carbon vs polar H on hetero.
        if (nH > 0)
            logP += (atom.element == Element::Carbon ? 0.1230f : -0.2677f)
                        * (float) nH;
    }

    d.tpsa  = tpsa;
    d.clogP = logP;
    d.fractionSp3 = (d.carbonCount > 0)
                        ? (float) sp3Carbons / (float) d.carbonCount
                        : 0.0f;

    // -----------------------------------------------------------------------
    //  Topological indices
    // -----------------------------------------------------------------------

    // Heavy-atom adjacency for the shortest-path work below.
    std::vector<int> heavyIndices;
    for (int i = 0; i < n; ++i)
        if (! atomList[(size_t) i].isHydrogen)
            heavyIndices.push_back (i);

    std::vector<std::vector<int>> adjacency ((size_t) n);
    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;
        adjacency[(size_t) b.a].push_back (b.b);
        adjacency[(size_t) b.b].push_back (b.a);
    }

    // Wiener index: the sum of topological distances over all heavy-atom pairs.
    // Each unordered pair is counted once.
    long long wiener = 0;
    for (int src : heavyIndices)
    {
        std::vector<int> dist ((size_t) n, -1);
        std::queue<int> q;
        q.push (src);
        dist[(size_t) src] = 0;

        while (! q.empty())
        {
            const int cur = q.front();
            q.pop();

            for (int nb : adjacency[(size_t) cur])
                if (dist[(size_t) nb] < 0)
                {
                    dist[(size_t) nb] = dist[(size_t) cur] + 1;
                    q.push (nb);
                }
        }

        for (int dst : heavyIndices)
            if (dst > src && dist[(size_t) dst] > 0)
                wiener += dist[(size_t) dst];
    }
    d.wienerIndex = (int) juce::jmin<long long> (wiener, 100000);

    // Randic branching index: sum over heavy bonds of 1 / sqrt(di * dj).
    float randic = 0.0f;
    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;

        const int di = juce::jmax (1, degree[(size_t) b.a]);
        const int dj = juce::jmax (1, degree[(size_t) b.b]);
        randic += 1.0f / std::sqrt ((float) di * (float) dj);
    }
    d.randicIndex = randic;

    // Overall bond polarity: the summed electronegativity difference across
    // every bond, hydrogens included. A rough stand-in for molecular polarity.
    float polarity = 0.0f;
    for (const auto& b : bondList)
    {
        const float chiA = elementInfo (atomList[(size_t) b.a].element).electronegativity;
        const float chiB = elementInfo (atomList[(size_t) b.b].element).electronegativity;
        polarity += std::abs (chiA - chiB);
    }
    d.bondPolarity = polarity;

    return d;
}

// ---------------------------------------------------------------------------
//  Persistence
//
//  Serialises the heavy-atom skeleton only: element list + bonds + orders.
//  Hydrogens are derived data, regenerated from valence on restore, so they
//  are deliberately left out. Positions are also omitted — the force-directed
//  layout settles into a sane shape by itself after restore.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Deterministic starting geometry
//
//  Only the topology is persisted, so a restored molecule has no coordinates.
//  Dropping every atom onto one big circle (the previous behaviour) is a poor
//  seed: for anything with a ring the solver has to unfold the circle while
//  simultaneously closing the ring, and it can get trapped in a mirrored,
//  concave arrangement - the "benzene comes back as a heart" symptom.
//
//  Instead the rings are laid down first as regular polygons (fused rings are
//  reflected across their shared edge) and everything else is grown outwards
//  by breadth-first search at the ideal bond angle. The relaxation then only
//  has to polish a structure that is already essentially correct.
//
//  Call this with hydrogens absent; they are regenerated afterwards.
// ---------------------------------------------------------------------------

void Molecule::seedLayout()
{
    const int n = (int) atomList.size();
    if (n == 0)
        return;

    if (n == 1)
    {
        atomList[0].pos = { 0.0f, 0.0f };
        return;
    }

    std::vector<std::vector<int>> adjacency ((size_t) n);
    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;
        adjacency[(size_t) b.a].push_back (b.b);
        adjacency[(size_t) b.b].push_back (b.a);
    }

    std::vector<bool> placed ((size_t) n, false);
    int placedCount = 0;

    auto markPlaced = [&] (int idx, juce::Point<float> p)
    {
        atomList[(size_t) idx].pos = p;
        atomList[(size_t) idx].velocity = { 0.0f, 0.0f };
        if (! placed[(size_t) idx])
        {
            placed[(size_t) idx] = true;
            ++placedCount;
        }
    };

    // --- 1) Rings as regular polygons ---
    float scatterX = 0.0f;

    for (const auto& ring : rings())
    {
        const int m = (int) ring.size();
        if (m < 3)
            continue;

        const float radius = kHeavyBondLength
            / (2.0f * std::sin (juce::MathConstants<float>::pi / (float) m));
        const float step = juce::MathConstants<float>::twoPi / (float) m;

        // Look for an edge whose endpoints are already positioned: that means
        // this ring is fused onto something previously laid out.
        int base = -1;
        for (int k = 0; k < m; ++k)
            if (placed[(size_t) ring[(size_t) k]]
                && placed[(size_t) ring[(size_t) ((k + 1) % m)]])
            {
                base = k;
                break;
            }

        if (base < 0)
        {
            // Free-standing ring: drop a regular polygon at the next slot.
            for (int k = 0; k < m; ++k)
            {
                const int idx = ring[(size_t) k];
                if (placed[(size_t) idx])
                    continue;

                const float a = step * (float) k;
                markPlaced (idx, { scatterX + std::cos (a) * radius,
                                   std::sin (a) * radius });
            }
            scatterX += radius * 2.4f;
            continue;
        }

        // Fused ring: mirror the new polygon across the shared edge.
        const auto A = atomList[(size_t) ring[(size_t) base]].pos;
        const auto B = atomList[(size_t) ring[(size_t) ((base + 1) % m)]].pos;

        auto edge = B - A;
        float edgeLen = edge.getDistanceFromOrigin();
        if (edgeLen < 1.0e-3f)
        {
            edge = { kHeavyBondLength, 0.0f };
            edgeLen = kHeavyBondLength;
        }

        const auto dir  = edge / edgeLen;
        const juce::Point<float> perp { -dir.y, dir.x };
        const auto mid = (A + B) * 0.5f;

        const float apothem = radius * std::cos (juce::MathConstants<float>::pi / (float) m);

        // Existing structure's centre of mass, so the new ring goes the other way.
        juce::Point<float> existing { 0.0f, 0.0f };
        int existingCount = 0;
        for (int i = 0; i < n; ++i)
            if (placed[(size_t) i])
            {
                existing += atomList[(size_t) i].pos;
                ++existingCount;
            }
        if (existingCount > 0)
            existing /= (float) existingCount;

        const auto c1 = mid + perp * apothem;
        const auto c2 = mid - perp * apothem;
        const auto centre = (c1.getDistanceFrom (existing) >= c2.getDistanceFrom (existing))
                                ? c1 : c2;

        // Walk the polygon from A, in whichever rotational sense reaches B.
        const float angleA = std::atan2 (A.y - centre.y, A.x - centre.x);
        const float angleB = std::atan2 (B.y - centre.y, B.x - centre.x);

        float delta = angleB - angleA;
        while (delta >  juce::MathConstants<float>::pi) delta -= juce::MathConstants<float>::twoPi;
        while (delta < -juce::MathConstants<float>::pi) delta += juce::MathConstants<float>::twoPi;

        const float signedStep = (delta >= 0.0f) ? step : -step;

        for (int k = 2; k < m; ++k)
        {
            const int idx = ring[(size_t) ((base + k) % m)];
            if (placed[(size_t) idx])
                continue;

            const float a = angleA + signedStep * (float) k;
            markPlaced (idx, { centre.x + std::cos (a) * radius,
                               centre.y + std::sin (a) * radius });
        }
    }

    // --- 2) Everything else, grown outwards by BFS ---
    if (placedCount == 0)
        markPlaced (0, { 0.0f, 0.0f });

    std::queue<int> q;
    for (int i = 0; i < n; ++i)
        if (placed[(size_t) i])
            q.push (i);

    while (! q.empty())
    {
        const int cur = q.front();
        q.pop();

        // Directions already occupied around this atom.
        juce::Point<float> occupied { 0.0f, 0.0f };
        std::vector<int> pending;

        for (int nb : adjacency[(size_t) cur])
        {
            if (placed[(size_t) nb])
            {
                auto d = atomList[(size_t) nb].pos - atomList[(size_t) cur].pos;
                const float len = d.getDistanceFromOrigin();
                if (len > 1.0e-3f)
                    occupied += d / len;
            }
            else
            {
                pending.push_back (nb);
            }
        }

        if (pending.empty())
            continue;

        float baseAngle = 0.0f;
        if (occupied.getDistanceFromOrigin() > 1.0e-3f)
            baseAngle = std::atan2 (-occupied.y, -occupied.x);

        const float spread = idealBondAngle (cur, true);
        const int   count  = (int) pending.size();

        for (int k = 0; k < count; ++k)
        {
            const float offset = (count == 1)
                ? 0.0f
                : ((float) k - (float) (count - 1) * 0.5f) * spread;
            const float a = baseAngle + offset;

            const int idx = pending[(size_t) k];
            markPlaced (idx, atomList[(size_t) cur].pos
                             + juce::Point<float> (std::cos (a), std::sin (a))
                                   * kHeavyBondLength);
            q.push (idx);
        }
    }

    // --- 3) Safety net for anything the graph walk could not reach ---
    for (int i = 0; i < n; ++i)
        if (! placed[(size_t) i])
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
            markPlaced (i, { std::cos (a) * kHeavyBondLength * 2.0f,
                             std::sin (a) * kHeavyBondLength * 2.0f });
        }

    // Re-centre on the origin so the canvas auto-fit starts from a sane place.
    juce::Point<float> centroid { 0.0f, 0.0f };
    for (const auto& a : atomList)
        centroid += a.pos;
    centroid /= (float) atomList.size();

    for (auto& a : atomList)
        a.pos -= centroid;
}

juce::ValueTree Molecule::toValueTree() const
{
    juce::ValueTree root ("Molecule");

    // Heavy atoms in their canonical index order.
    for (const auto& a : atomList)
    {
        if (a.isHydrogen)
            continue;

        juce::ValueTree atom ("Atom");
        atom.setProperty ("element", (int) a.element, nullptr);
        root.addChild (atom, -1, nullptr);
    }

    // Bonds between heavy atoms, referencing heavy-atom ordinal indices.
    // Build the remap from raw atom index -> heavy ordinal first.
    std::vector<int> heavyOrdinal (atomList.size(), -1);
    {
        int ord = 0;
        for (size_t i = 0; i < atomList.size(); ++i)
            if (! atomList[i].isHydrogen)
                heavyOrdinal[i] = ord++;
    }

    for (const auto& b : bondList)
    {
        if (atomList[(size_t) b.a].isHydrogen || atomList[(size_t) b.b].isHydrogen)
            continue;

        juce::ValueTree bond ("Bond");
        bond.setProperty ("a", heavyOrdinal[(size_t) b.a], nullptr);
        bond.setProperty ("b", heavyOrdinal[(size_t) b.b], nullptr);
        bond.setProperty ("order", b.order, nullptr);
        root.addChild (bond, -1, nullptr);
    }

    return root;
}

void Molecule::fromValueTree (const juce::ValueTree& tree)
{
    // Reject malformed input: a non-"Molecule" tree is not our state.
    if (! tree.isValid() || tree.getType() != juce::Identifier ("Molecule"))
        return;

    clear();

    // Pass 1: heavy atoms (their order defines the bond indices).
    std::vector<Element> heavyElements;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (child.getType() == juce::Identifier ("Atom"))
        {
            const int e = juce::jlimit ((int) Element::Carbon,
                                        (int) Element::Phosphorus,
                                        (int) child.getProperty ("element", (int) Element::Carbon));
            heavyElements.push_back ((Element) e);
        }
    }

    const int n = (int) heavyElements.size();

    // Positions are filled in by seedLayout() once the bonds are known; a
    // tiny deterministic offset just avoids exactly coincident atoms in case
    // the tree turns out to have no bonds at all.
    for (int i = 0; i < n; ++i)
    {
        const float angle = juce::MathConstants<float>::twoPi
                          * (float) i / (float) juce::jmax (1, n);
        atomList.emplace_back (heavyElements[(size_t) i],
                               juce::Point<float> (std::cos (angle) * kHeavyBondLength,
                                                   std::sin (angle) * kHeavyBondLength));
    }

    // Pass 2: bonds.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (child.getType() != juce::Identifier ("Bond"))
            continue;

        const int a = (int) child.getProperty ("a", -1);
        const int b = (int) child.getProperty ("b", -1);
        const int order = juce::jlimit (1, 3, (int) child.getProperty ("order", 1));

        if (a < 0 || b < 0 || a >= n || b >= n || a == b)
            continue;

        bondList.emplace_back (a, b, order);
    }

    // The ring cache must see the new bonds before seedLayout() asks for rings.
    invalidateTopology();

    // Ring-aware starting geometry, then hydrogens on the finished skeleton.
    seedLayout();
    rebuildHydrogens();
    settling = true;
}

// ---------------------------------------------------------------------------
//  Preset library
//
//  Topology is written out longhand rather than parsed from SMILES: the
//  editor has a SMILES *writer* but no reader, and an explicit table is
//  easier to audit against the valence rules.
//
//  Rings are given in Kekule form (alternating single / double), which is how
//  this editor represents aromatics internally - there is no aromatic bond
//  type. Every entry has been checked against the neutral valences the model
//  enforces: C 4, N 3, O 2, S 2, P 3, so no atom ends up over-bonded and the
//  implicit hydrogen count comes out right.
// ---------------------------------------------------------------------------

const std::vector<MoleculePreset>& moleculePresets()
{
    constexpr Element C = Element::Carbon;
    constexpr Element O = Element::Oxygen;
    constexpr Element N = Element::Nitrogen;
    constexpr Element S = Element::Sulfur;
    constexpr Element P = Element::Phosphorus;

    static const std::vector<MoleculePreset> list = {
        // --- one and two heavy atoms ---
        { "Methane",            { C },              {} },
        { "Methanol",           { C, O },           { {0,1,1} } },
        { "Formaldehyde",       { C, O },           { {0,1,2} } },
        { "Ethane",             { C, C },           { {0,1,1} } },
        { "Ethylene",           { C, C },           { {0,1,2} } },
        { "Acetylene",          { C, C },           { {0,1,3} } },

        // --- small chains and functional groups ---
        { "Ethanol",            { C, C, O },        { {0,1,1}, {1,2,1} } },
        { "Acetaldehyde",       { C, C, O },        { {0,1,1}, {1,2,2} } },
        { "Dimethyl ether",     { C, O, C },        { {0,1,1}, {1,2,1} } },
        { "Dimethyl sulfide",   { C, S, C },        { {0,1,1}, {1,2,1} } },
        { "Trimethylphosphine", { P, C, C, C },     { {0,1,1}, {0,2,1}, {0,3,1} } },
        { "Propane",            { C, C, C },        { {0,1,1}, {1,2,1} } },
        { "Acetonitrile",       { C, C, N },        { {0,1,1}, {1,2,3} } },
        { "Acetone",            { C, C, O, C },     { {0,1,1}, {1,2,2}, {1,3,1} } },
        { "Acetic acid",        { C, C, O, O },     { {0,1,1}, {1,2,2}, {1,3,1} } },
        { "Urea",               { N, C, O, N },     { {0,1,1}, {1,2,2}, {1,3,1} } },
        { "Butane",             { C, C, C, C },     { {0,1,1}, {1,2,1}, {2,3,1} } },
        { "Isobutane",          { C, C, C, C },     { {0,1,1}, {0,2,1}, {0,3,1} } },
        { "Ethylene glycol",    { O, C, C, O },     { {0,1,1}, {1,2,1}, {2,3,1} } },
        { "Glycine",            { N, C, C, O, O },  { {0,1,1}, {1,2,1}, {2,3,2}, {2,4,1} } },

        // --- saturated rings ---
        { "Cyclopropane",       { C, C, C },
          { {0,1,1}, {1,2,1}, {2,0,1} } },
        { "Cyclopentane",       { C, C, C, C, C },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,0,1} } },
        { "Cyclohexane",        { C, C, C, C, C, C },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1} } },

        // --- aromatics (Kekule) ---
        { "Benzene",            { C, C, C, C, C, C },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1} } },
        { "Toluene",            { C, C, C, C, C, C, C },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1} } },
        { "Phenol",             { C, C, C, C, C, C, O },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1} } },
        { "Aniline",            { C, C, C, C, C, C, N },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1} } },
        { "Styrene",            { C, C, C, C, C, C, C, C },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1},
            {0,6,1}, {6,7,2} } },
        { "Pyridine",           { N, C, C, C, C, C },
          { {0,1,1}, {1,2,2}, {2,3,1}, {3,4,2}, {4,5,1}, {5,0,2} } },
        { "Furan",              { O, C, C, C, C },
          { {0,1,1}, {1,2,2}, {2,3,1}, {3,4,2}, {4,0,1} } },
        { "Thiophene",          { S, C, C, C, C },
          { {0,1,1}, {1,2,2}, {2,3,1}, {3,4,2}, {4,0,1} } },

        // --- fused and larger showcases ---
        { "Naphthalene",        { C, C, C, C, C, C, C, C, C, C },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,9,2}, {9,0,1},
            {4,5,1}, {5,6,2}, {6,7,1}, {7,8,2}, {8,9,1} } },
        { "Glucose (open)",     { C, O, C, O, C, O, C, O, C, O, C, O },
          { {0,1,2}, {0,2,1}, {2,3,1}, {2,4,1}, {4,5,1}, {4,6,1},
            {6,7,1}, {6,8,1}, {8,9,1}, {8,10,1}, {10,11,1} } },
        { "Caffeine",           { C, N, C, N, C, C, C, O, N, C, C, O, N, C },
          { {0,1,1}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,1,1},
            {5,6,1}, {6,7,2}, {6,8,1}, {8,9,1}, {8,10,1}, {10,11,2},
            {10,12,1}, {12,4,1}, {12,13,1} } },
    };

    return list;
}

// ---------------------------------------------------------------------------
//  Extended name library
//
//  Molecules that deserve a common name but not a slot in the preset picker.
//  Same authoring rules as the presets: Kekule rings, valences C 4 / N 3 /
//  O 2 / S 2 / P 3.
// ---------------------------------------------------------------------------

const std::vector<MoleculePreset>& namedMolecules()
{
    constexpr Element C = Element::Carbon;
    constexpr Element O = Element::Oxygen;
    constexpr Element N = Element::Nitrogen;
    constexpr Element S = Element::Sulfur;

    static const std::vector<MoleculePreset> extras = {
        // --- alkenes / alkynes ---
        { "Propene",           { C, C, C },       { {0,1,2}, {1,2,1} } },
        { "Propyne",           { C, C, C },       { {0,1,3}, {1,2,1} } },
        { "1-Butene",          { C, C, C, C },    { {0,1,2}, {1,2,1}, {2,3,1} } },
        { "1,3-Butadiene",     { C, C, C, C },    { {0,1,2}, {1,2,1}, {2,3,2} } },

        // --- alcohols ---
        { "1-Propanol",        { C, C, C, O },    { {0,1,1}, {1,2,1}, {2,3,1} } },
        { "Isopropanol",       { C, C, C, O },    { {0,1,1}, {1,2,1}, {1,3,1} } },
        { "1-Butanol",         { C, C, C, C, O }, { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1} } },
        { "tert-Butanol",      { C, C, C, C, O }, { {0,1,1}, {0,2,1}, {0,3,1}, {0,4,1} } },
        { "Glycerol",          { C, C, C, O, O, O },
          { {0,1,1}, {1,2,1}, {0,3,1}, {1,4,1}, {2,5,1} } },

        // --- carbonyls and acids ---
        { "Formic acid",       { C, O, O },       { {0,1,2}, {0,2,1} } },
        { "Propionic acid",    { C, C, C, O, O }, { {0,1,1}, {1,2,1}, {2,3,2}, {2,4,1} } },
        { "Butyric acid",      { C, C, C, C, O, O },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,2}, {3,5,1} } },
        { "Acrylic acid",      { C, C, C, O, O }, { {0,1,2}, {1,2,1}, {2,3,2}, {2,4,1} } },
        { "Oxalic acid",       { C, C, O, O, O, O },
          { {0,1,1}, {0,2,2}, {0,3,1}, {1,4,2}, {1,5,1} } },
        { "Malonic acid",      { C, C, C, O, O, O, O },
          { {0,1,1}, {1,2,1}, {0,3,2}, {0,4,1}, {2,5,2}, {2,6,1} } },
        { "Succinic acid",     { C, C, C, C, O, O, O, O },
          { {0,1,1}, {1,2,1}, {2,3,1}, {0,4,2}, {0,5,1}, {3,6,2}, {3,7,1} } },
        { "Fumaric acid",      { C, C, C, C, O, O, O, O },
          { {0,1,1}, {1,2,2}, {2,3,1}, {0,4,2}, {0,5,1}, {3,6,2}, {3,7,1} } },
        { "Lactic acid",       { C, C, C, O, O, O },
          { {0,1,1}, {1,2,1}, {1,3,1}, {2,4,2}, {2,5,1} } },
        { "Pyruvic acid",      { C, C, C, O, O, O },
          { {0,1,1}, {1,2,1}, {1,3,2}, {2,4,2}, {2,5,1} } },
        { "Methyl acetate",    { C, C, O, O, C }, { {0,1,1}, {1,2,2}, {1,3,1}, {3,4,1} } },
        { "Ethyl acetate",     { C, C, O, O, C, C },
          { {0,1,1}, {1,2,2}, {1,3,1}, {3,4,1}, {4,5,1} } },
        { "Acetic anhydride",  { C, C, O, O, C, O, C },
          { {0,1,1}, {1,2,2}, {1,3,1}, {3,4,1}, {4,5,2}, {4,6,1} } },

        // --- nitrogen ---
        { "Formamide",         { C, O, N },       { {0,1,2}, {0,2,1} } },
        { "Acetamide",         { C, C, O, N },    { {0,1,1}, {1,2,2}, {1,3,1} } },
        { "Hydrogen cyanide",  { C, N },          { {0,1,3} } },
        { "Ethylamine",        { C, C, N },       { {0,1,1}, {1,2,1} } },
        { "Dimethylamine",     { C, N, C },       { {0,1,1}, {1,2,1} } },
        { "Alanine",           { C, C, N, C, O, O },
          { {0,1,1}, {1,2,1}, {1,3,1}, {3,4,2}, {3,5,1} } },

        // --- sulfur ---
        { "Methanethiol",      { C, S },          { {0,1,1} } },
        { "Ethanethiol",       { C, C, S },       { {0,1,1}, {1,2,1} } },

        // --- ethers and saturated heterocycles ---
        { "Diethyl ether",     { C, C, O, C, C }, { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1} } },
        { "Ethylene oxide",    { C, C, O },       { {0,1,1}, {1,2,1}, {2,0,1} } },
        { "Tetrahydrofuran",   { O, C, C, C, C }, { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,0,1} } },
        { "1,4-Dioxane",       { O, C, C, O, C, C },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1} } },
        { "Piperidine",        { N, C, C, C, C, C },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1} } },
        { "Morpholine",        { O, C, C, N, C, C },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1} } },

        // --- carbocycles ---
        { "Cyclobutane",       { C, C, C, C },    { {0,1,1}, {1,2,1}, {2,3,1}, {3,0,1} } },
        { "Cycloheptane",      { C, C, C, C, C, C, C },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,6,1}, {6,0,1} } },
        { "Cyclohexene",       { C, C, C, C, C, C },
          { {0,1,2}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1} } },
        { "Cyclohexanol",      { C, C, C, C, C, C, O },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1}, {0,6,1} } },
        { "Cyclohexanone",     { C, C, C, C, C, C, O },
          { {0,1,1}, {1,2,1}, {2,3,1}, {3,4,1}, {4,5,1}, {5,0,1}, {0,6,2} } },

        // --- aromatic heterocycles and substituted arenes ---
        { "Pyrrole",           { N, C, C, C, C },
          { {0,1,1}, {1,2,2}, {2,3,1}, {3,4,2}, {4,0,1} } },
        { "Imidazole",         { N, C, N, C, C },
          { {0,1,1}, {1,2,2}, {2,3,1}, {3,4,2}, {4,0,1} } },
        { "Pyrimidine",        { N, C, N, C, C, C },
          { {0,1,1}, {1,2,2}, {2,3,1}, {3,4,2}, {4,5,1}, {5,0,2} } },
        { "Benzaldehyde",      { C, C, C, C, C, C, C, O },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1}, {6,7,2} } },
        { "Benzoic acid",      { C, C, C, C, C, C, C, O, O },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1},
            {0,6,1}, {6,7,2}, {6,8,1} } },
        { "Anisole",           { C, C, C, C, C, C, O, C },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1}, {6,7,1} } },
        { "p-Xylene",          { C, C, C, C, C, C, C, C },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1}, {3,7,1} } },
        { "p-Cresol",          { C, C, C, C, C, C, C, O },
          { {0,1,2}, {1,2,1}, {2,3,2}, {3,4,1}, {4,5,2}, {5,0,1}, {0,6,1}, {3,7,1} } },
    };

    // presets + extras, built once.
    static const std::vector<MoleculePreset> all = []
    {
        std::vector<MoleculePreset> v = moleculePresets();
        v.insert (v.end(), extras.begin(), extras.end());
        return v;
    }();

    return all;
}

namespace
{
/** Turn a preset-style topology description into a ValueTree. */
juce::ValueTree topologyToValueTree (const MoleculePreset& preset)
{
    juce::ValueTree root ("Molecule");

    for (auto e : preset.atoms)
    {
        juce::ValueTree atom ("Atom");
        atom.setProperty ("element", (int) e, nullptr);
        root.addChild (atom, -1, nullptr);
    }

    for (const auto& b : preset.bonds)
    {
        juce::ValueTree bond ("Bond");
        bond.setProperty ("a", b.a, nullptr);
        bond.setProperty ("b", b.b, nullptr);
        bond.setProperty ("order", b.order, nullptr);
        root.addChild (bond, -1, nullptr);
    }

    return root;
}
}  // namespace

juce::ValueTree presetValueTree (int index)
{
    const auto& presets = moleculePresets();

    if (index < 0 || index >= (int) presets.size())
        return juce::ValueTree ("Molecule");

    return topologyToValueTree (presets[(size_t) index]);
}

const juce::String& presetFormula (int index)
{
    static const std::vector<juce::String> formulas = []
    {
        std::vector<juce::String> v;

        for (const auto& preset : moleculePresets())
        {
            Molecule m;
            m.fromValueTree (topologyToValueTree (preset));
            v.push_back (m.formula());
        }

        return v;
    }();

    static const juce::String empty;

    if (index < 0 || index >= (int) formulas.size())
        return empty;

    return formulas[(size_t) index];
}

} // namespace organic
