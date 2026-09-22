#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

#include "MoleculeModel.h"

// ============================================================
//  Molecule -> audio parameter mapping layer
//
//  Turns a molecule's chemical descriptors into two things:
//
//    1. A single-cycle wavetable (an array of samples) whose shape is derived
//       from the molecule's canonical SMILES. This wavetable IS the timbre:
//       the voice reads it directly instead of a fixed sine, so the waveform
//       is unrestricted and changes dramatically with the molecule.
//
//    2. The effect-chain parameters (low-cut, reverb, delay) that wrap the
//       wavetable in a space. Their ranges are deliberately wide so topology
//       changes are clearly audible.
//
//  This layer is pure and thread-agnostic: it computes plain values from a
//  plain struct. The audio thread never touches Molecule directly; the editor
//  calls Molecule::computeDescriptors() / canonicalSmiles() on the UI thread
//  and hands the results to the processor as immutable values.
// ============================================================

namespace organic
{

// Size of the single-cycle wavetable, shared by the mapper and the processor.
constexpr int kWaveTableSize = 2048;

// ---------------------------------------------------------------------------
//  Synthesis parameter categories
//
//  The test panel groups parameters by processing stage so related controls
//  sit together. The order here also defines the display order.
// ---------------------------------------------------------------------------

enum class ParamCategory : int
{
    Oscillator = 0,   // sound source: level, detune, sub, warp, noise, glide
    Envelope,         // per-note ADSR amplitude envelope + velocity response
    Filter,           // low-cut, high-cut and the per-voice morphing filter
    ModEnv,           // second envelope, freely assignable (ENV 2)
    Lfo,              // global LFO and its destinations (LFO 1)
    Lfo2,             // per-voice LFO and its destinations (LFO 2)
    Organic,          // per-note randomisation and slow analogue drift
    Resonator,        // tuned comb resonator (body / string character)
    Formant,          // vowel filter bank
    Distortion,       // drive / saturator, selectable curve
    Chorus,           // modulated delay doubling
    Reverb,           // space
    Delay,            // echo
    Master,           // output trim / width
    Count
};

constexpr int kNumCategories = static_cast<int> (ParamCategory::Count);

// ---------------------------------------------------------------------------
//  Synthesis parameter set
// ---------------------------------------------------------------------------

/** Index into the flat parameter array. Order groups parameters by category
    so the test panel can render them in contiguous, labelled sections. */
enum class ParamId : int
{
    // --- Oscillator ---
    OscLevel = 0,      // voice gain
    OscDetune,         // per-voice pitch spread
    OscSpread,         // stereo placement of the unison voices
    SubLevel,          // sine sub-oscillator, one octave down
    WaveWarp,          // phase distortion applied to the wavetable read
    NoiseLevel,        // filtered noise layer mixed in with the oscillator
    NoiseColour,       // 0 dark rumble .. 1 bright hiss
    GlideSec,          // portamento time between successive notes

    // --- Envelope (ADSR) ---
    AttackSec,         // attack time
    DecaySec,          // decay time towards the sustain level
    SustainLevel,      // sustain plateau, 0..1
    ReleaseSec,        // release time
    EnvCurve,          // 0 linear .. 1 strongly exponential
    VelSens,           // how much MIDI velocity scales the amplitude

    // --- Filter (per voice) ---
    LowCutHz,          // global high-pass cutoff
    LowCutRes,         // global high-pass resonance
    HighCutHz,         // global low-pass cutoff
    HighCutRes,        // global low-pass resonance
    FilterType,        // morph: 0 LP -> 1 BP -> 2 HP -> 3 Notch
    FilterFreq,        // base cutoff
    FilterRes,         // resonance / Q
    FilterEnvAmt,      // amp envelope -> cutoff, in octaves
    FilterKeyTrk,      // MIDI note -> cutoff (0 none, 1 full 1:1 tracking)
    FilterVelTrk,      // MIDI velocity -> cutoff
    FilterDrive,       // saturation inside the filter path

    // --- Mod envelope (ENV 2, per voice, freely assigned) ---
    ModEnvAttack,
    ModEnvDecay,
    ModEnvSustain,
    ModEnvRelease,
    ModEnvToFilter,    // bipolar, in octaves
    ModEnvToPitch,     // bipolar, in semitones
    ModEnvToWarp,      // unipolar, into the wavetable phase distortion

    // --- LFO 1 (global, shared by all voices) ---
    LfoRate,           // rate in Hz
    LfoShape,          // 0 sine -> 1 triangle -> 2 square -> 3 sample & hold
    LfoFadeSec,        // fade-in time from note start (delayed vibrato)
    LfoToFilter,       // depth into the voice cutoff
    LfoToPitch,        // vibrato depth, cents
    LfoToAmp,          // depth into output amplitude (tremolo)
    LfoToPan,          // depth into stereo position (auto-pan)

    // --- LFO 2 (per voice, free-running from note start) ---
    Lfo2Rate,
    Lfo2Shape,
    Lfo2ToWarp,        // into the wavetable phase distortion
    Lfo2ToFilter,      // into the voice cutoff

    // --- Organic: per-note randomisation and slow drift ---
    RandPitch,         // per-note random detune, cents
    RandFilter,        // per-note random cutoff offset, octaves
    RandPan,           // per-note random stereo placement
    DriftRate,         // slow per-voice drift rate, Hz
    DriftDepth,        // drift depth into pitch and cutoff

    // --- Resonator (tuned comb, tracks the played note) ---
    CombMix,
    CombTune,          // ratio against the played note (0.25 .. 4)
    CombFeedback,
    CombDamp,          // high-frequency loss inside the feedback loop

    // --- Formant filter ---
    FormantMorph,      // 0 A -> 1 E -> 2 I -> 3 O -> 4 U
    FormantMix,

    // --- Distortion ---
    Drive,             // saturator drive amount
    DistMix,           // wet/dry blend
    DistType,          // curve: 0 tanh, 1 hard clip, 2 wavefolder, 3 bitcrush

    // --- Chorus ---
    ChorusRate,        // LFO rate, Hz
    ChorusDepth,       // modulation depth, seconds
    ChorusMix,         // wet/dry blend

    // --- Reverb ---
    ReverbRoomSize,
    ReverbDamping,
    ReverbWet,
    ReverbWidth,

    // --- Delay ---
    DelayTimeSec,
    DelayFeedback,
    DelayMix,
    DelayPingPong,     // 0 = parallel echoes, 1 = fully cross-fed ping-pong

    // --- Master ---
    MasterLevel,
    StereoWidth,

    Count
};

constexpr int kNumParams = static_cast<int> (ParamId::Count);

// ---------------------------------------------------------------------------
//  Parameter metadata
// ---------------------------------------------------------------------------

/** Everything the UI needs to render one parameter: display name, category,
    value range, step and the default (molecule-independent) value.

    `mapped` records whether mapMoleculeToAudio() currently drives this
    parameter. Parameters with mapped == false are *bypassed*: the mapper
    writes their neutral value and leaves the sound untouched, so they sit in
    the test panel waiting to be dialled in by hand. The panel greys their
    labels out, which is the only place this flag is consumed — keeping it
    here means the panel and the mapper can never disagree about which
    parameters are live.

 IMPORTANT: for a parameter to be truly bypassed its `defaultValue` must be
    the value that makes its DSP stage a no-op, not simply zero. `VelSens` is
    the obvious trap: 0 does not mean "no velocity processing", it means "ignore
    velocity entirely and play every note at full level". The neutral value
  there is 1.0. */
struct ParamDef
{
    const char*   name;
    ParamCategory category;
    float         minValue;
    float         maxValue;
    float         step;
    float         defaultValue;
    bool          mapped;
};

/** Metadata table for every parameter, in ParamId order. */
inline const ParamDef& paramDef (int index)
{
    // The last column is `mapped`. `true` marks the 37 parameters that were
    // already driven by the molecule in v0.12 and still are. `false` marks the
    // 33 capabilities added in v0.13: their default value is deliberately the
    // *neutral* setting for that DSP stage, so with a fresh instance the synth
    // sounds exactly like v0.12 and every new module is silent until dialled
    // in by hand from the test panel.
    static const ParamDef table[] = {
        // Oscillator
        { "Osc level",       ParamCategory::Oscillator, 0.0f,  1.5f,  0.01f, 0.50f,  true  },
        { "Osc detune",      ParamCategory::Oscillator, 0.0f,  0.30f, 0.01f, 0.05f,  true  },
        { "Osc spread",      ParamCategory::Oscillator, 0.0f,  1.0f,  0.01f, 0.35f,  true  },
        { "Sub level",       ParamCategory::Oscillator, 0.0f,  1.0f,  0.01f, 0.15f,  true  },
        { "Wave warp",       ParamCategory::Oscillator, 0.0f,  1.0f,  0.01f, 0.0f,   true  },
        // Neutral: no noise layer at all.
        { "Noise level",     ParamCategory::Oscillator, 0.0f,  0.60f, 0.01f, 0.0f,   false },
        { "Noise colour",    ParamCategory::Oscillator, 0.0f,  1.0f,  0.01f, 0.50f,  false },
        // Neutral: 0 s glide means the pitch jumps immediately, as before.
        { "Glide (s)",       ParamCategory::Oscillator, 0.0f,  0.50f, 0.005f, 0.0f,  false },

        // Envelope (ADSR)
        { "Attack (s)",      ParamCategory::Envelope,   0.001f, 2.0f,  0.001f, 0.005f, true  },
        { "Decay (s)",       ParamCategory::Envelope,   0.005f, 3.0f,  0.005f, 0.25f,  true  },
        { "Sustain",         ParamCategory::Envelope,   0.0f,   1.0f,  0.01f,  0.80f,  true  },
        { "Release (s)",     ParamCategory::Envelope,   0.01f,  5.0f,  0.01f,  0.10f,  true  },
        // Neutral: 0 = linear envelope, which is what v0.12 did. Anything above
        // 0 raises the envelope to a power and audibly drops the level.
        { "Env curve",       ParamCategory::Envelope,   0.0f,   1.0f,  0.01f,  0.0f,   false },
        // Neutral: 1.0, NOT 0. v0.12 used `level = velocity * oscLevel`, i.e.
        // full velocity response. 0 would mean "ignore velocity, every note at
        // full level" — the opposite of a bypass.
        { "Velocity sens",   ParamCategory::Envelope,   0.0f,   1.0f,  0.01f,  1.0f,   false },

        // Filter
        { "Low-cut (Hz)",    ParamCategory::Filter,     20.0f,  2000.0f, 1.0f,  60.0f,  true  },
        { "Low-cut res",     ParamCategory::Filter,     0.5f,   3.0f,   0.01f,  0.707f, true  },
        { "High-cut (Hz)",   ParamCategory::Filter,     200.0f, 20000.0f, 1.0f,  18000.0f, true },
        { "High-cut res",    ParamCategory::Filter,     0.5f,   3.0f,   0.01f,  0.707f, true  },
        { "Filter type",     ParamCategory::Filter,     0.0f,   3.0f,   0.01f,  0.0f,   true  },
        { "Filter freq (Hz)",ParamCategory::Filter,     80.0f,  16000.0f, 1.0f, 12000.0f, true },
        { "Filter res",      ParamCategory::Filter,     0.5f,   8.0f,   0.01f,  0.80f,  true  },
        { "Filter env amt",  ParamCategory::Filter,     0.0f,   4.0f,   0.01f,  0.0f,   true  },
        // Neutral: 0 keeps the cutoff identical on every voice, which makes the
        // per-voice filters collectively equivalent to the single global filter
        // v0.12 had (an SVF is linear, so summing filtered voices equals
        // filtering the sum). Any non-zero value detunes them per note.
        { "Filter key trk",  ParamCategory::Filter,     0.0f,   1.0f,   0.01f,  0.0f,   false },
        { "Filter vel trk",  ParamCategory::Filter,     0.0f,   1.0f,   0.01f,  0.0f,   false },
        // Neutral: 0 skips the pre-filter saturator, keeping the path linear.
        { "Filter drive",    ParamCategory::Filter,     0.0f,   1.0f,   0.01f,  0.0f,   false },

        // Mod envelope (ENV 2) — runs, but reaches nothing until a depth is set.
        { "ENV2 attack (s)", ParamCategory::ModEnv,     0.001f, 3.0f,   0.001f, 0.02f,  false },
        { "ENV2 decay (s)",  ParamCategory::ModEnv,     0.005f, 4.0f,   0.005f, 0.40f,  false },
        { "ENV2 sustain",    ParamCategory::ModEnv,     0.0f,   1.0f,   0.01f,  0.30f,  false },
        { "ENV2 release (s)",ParamCategory::ModEnv,     0.01f,  6.0f,   0.01f,  0.50f,  false },
        { "ENV2 -> filter",  ParamCategory::ModEnv,    -4.0f,   4.0f,   0.01f,  0.0f,   false },
        { "ENV2 -> pitch",   ParamCategory::ModEnv,   -12.0f,  12.0f,   0.10f,  0.0f,   false },
        { "ENV2 -> warp",    ParamCategory::ModEnv,     0.0f,   1.0f,   0.01f,  0.0f,   false },

        // LFO 1 (global)
        { "LFO1 rate (Hz)",  ParamCategory::Lfo,        0.02f,  12.0f,  0.01f,  0.60f,  true  },
        // Neutral: 0 = sine, the only shape v0.12 had.
        { "LFO1 shape",      ParamCategory::Lfo,        0.0f,   3.0f,   0.01f,  0.0f,   false },
        // Neutral: 0 s fade means full depth from the first sample, as before.
        { "LFO1 fade (s)",   ParamCategory::Lfo,        0.0f,   5.0f,   0.01f,  0.0f,   false },
        { "LFO1 -> filter",  ParamCategory::Lfo,        0.0f,   1.0f,   0.01f,  0.0f,   true  },
        { "LFO1 -> pitch",   ParamCategory::Lfo,        0.0f,   100.0f, 0.50f,  0.0f,   false },
        { "LFO1 -> amp",     ParamCategory::Lfo,        0.0f,   1.0f,   0.01f,  0.0f,   true  },
        { "LFO1 -> pan",     ParamCategory::Lfo,        0.0f,   1.0f,   0.01f,  0.0f,   true  },

        // LFO 2 (per voice) — free-running, but routed nowhere by default.
        { "LFO2 rate (Hz)",  ParamCategory::Lfo2,       0.02f,  20.0f,  0.01f,  3.0f,   false },
        { "LFO2 shape",      ParamCategory::Lfo2,       0.0f,   3.0f,   0.01f,  0.0f,   false },
        { "LFO2 -> warp",    ParamCategory::Lfo2,       0.0f,   1.0f,   0.01f,  0.0f,   false },
        { "LFO2 -> filter",  ParamCategory::Lfo2,       0.0f,   1.0f,   0.01f,  0.0f,   false },

        // Organic (per-note randomisation and slow drift) — all depths at 0,
        // so every note is bit-identical, exactly like v0.12.
        { "Rand pitch (ct)",  ParamCategory::Organic,   0.0f,   60.0f,  0.5f,   0.0f,   false },
        { "Rand filter (oct)",ParamCategory::Organic,   0.0f,   2.0f,   0.01f,  0.0f,   false },
        { "Rand pan",         ParamCategory::Organic,   0.0f,   1.0f,   0.01f,  0.0f,   false },
        { "Drift rate (Hz)",  ParamCategory::Organic,   0.01f,  2.0f,   0.01f,  0.15f,  false },
        { "Drift depth",      ParamCategory::Organic,   0.0f,   1.0f,   0.01f,  0.0f,   false },

        // Resonator — mix 0 passes the dry signal through untouched.
        { "Comb mix",        ParamCategory::Resonator,  0.0f,   1.0f,   0.01f,  0.0f,   false },
        { "Comb tune",       ParamCategory::Resonator,  0.25f,  4.0f,   0.01f,  1.0f,   false },
        { "Comb feedback",   ParamCategory::Resonator,  0.0f,   0.97f,  0.01f,  0.60f,  false },
        { "Comb damp",       ParamCategory::Resonator,  0.0f,   1.0f,   0.01f,  0.40f,  false },

        // Formant — mix 0 skips the whole stage.
        { "Formant morph",   ParamCategory::Formant,    0.0f,   4.0f,   0.01f,  0.0f,   false },
        { "Formant mix",     ParamCategory::Formant,    0.0f,   1.0f,   0.01f,  0.0f,   false },

        // Distortion
        { "Drive",           ParamCategory::Distortion, 0.0f,   1.0f,   0.01f,  0.10f,  true  },
        { "Dist mix",        ParamCategory::Distortion, 0.0f,   1.0f,   0.01f,  0.10f,  true  },
        { "Dist type",       ParamCategory::Distortion, 0.0f,   3.0f,   1.0f,   0.0f,   true  },

        // Chorus
        { "Chorus rate (Hz)",ParamCategory::Chorus,     0.05f,  5.0f,   0.05f,  0.80f,  true  },
        { "Chorus depth",    ParamCategory::Chorus,     0.0f,   0.010f, 0.0001f, 0.004f, true },
        { "Chorus mix",      ParamCategory::Chorus,     0.0f,   1.0f,   0.01f,  0.15f,  true  },

        // Reverb
        { "Reverb room",     ParamCategory::Reverb,     0.0f,   1.0f,   0.01f,  0.45f,  true  },
        { "Reverb damping",  ParamCategory::Reverb,     0.0f,   1.0f,   0.01f,  0.55f,  true  },
        { "Reverb wet",      ParamCategory::Reverb,     0.0f,   1.0f,   0.01f,  0.30f,  true  },
        { "Reverb width",    ParamCategory::Reverb,     0.0f,   1.0f,   0.01f,  0.90f,  true  },

        // Delay
        { "Delay time (s)",  ParamCategory::Delay,      0.01f,  1.5f,   0.01f,  0.24f,  true  },
        { "Delay feedback",  ParamCategory::Delay,      0.0f,   0.95f,  0.01f,  0.28f,  true  },
        { "Delay mix",       ParamCategory::Delay,      0.0f,   1.0f,   0.01f,  0.22f,  true  },
        { "Delay ping-pong", ParamCategory::Delay,      0.0f,   1.0f,   0.01f,  0.0f,   true  },

        // Master
        { "Master level",    ParamCategory::Master,     0.0f,   1.5f,   0.01f,  0.90f,  true  },
        { "Stereo width",    ParamCategory::Master,     0.0f,   1.0f,   0.01f,  1.0f,   true  },
    };
    return table[index];
}

/** True when mapMoleculeToAudio() currently drives this parameter. */
inline bool paramIsMapped (int index)
{
    return paramDef (index).mapped;
}

/** Human-readable title for a category (used as a section header). */
inline const char* categoryName (ParamCategory c)
{
    switch (c)
    {
        case ParamCategory::Oscillator:  return "OSCILLATOR";
        case ParamCategory::Envelope:    return "AMP ENVELOPE";
        case ParamCategory::Filter:      return "FILTER";
        case ParamCategory::ModEnv:      return "MOD ENVELOPE (ENV 2)";
        case ParamCategory::Lfo:         return "LFO 1  (global)";
        case ParamCategory::Lfo2:        return "LFO 2  (per voice)";
        case ParamCategory::Organic:     return "ORGANIC  (per-note random / drift)";
        case ParamCategory::Resonator:   return "RESONATOR";
        case ParamCategory::Formant:     return "FORMANT";
        case ParamCategory::Distortion:  return "DISTORTION";
        case ParamCategory::Chorus:      return "CHORUS";
        case ParamCategory::Reverb:      return "REVERB";
        case ParamCategory::Delay:       return "DELAY";
        case ParamCategory::Master:      return "MASTER";
        default:                         return "";
    }
}

/** Every audio parameter the effect chain consumes, in a flat array indexed
    by ParamId. Plain floats, ready to be smoothed by the audio thread. */
using SynthesisParameters = std::array<float, kNumParams>;

// ---------------------------------------------------------------------------
//  Wavetable (the timbre itself)
// ---------------------------------------------------------------------------

/** A single-cycle wavetable, normalised so its peak magnitude is <= 1.0.

    The samples array always has kWaveTableSize entries; each entry is one
    point of one full 0..2pi cycle. The voice does linear-interpolated
    table lookup to play it back at any pitch. */
struct WaveTable
{
    std::vector<float> samples;   // length == kWaveTableSize
};

// ---------------------------------------------------------------------------
//  Mapping
// ---------------------------------------------------------------------------

/** Map chemical descriptors to synthesis parameters.

    The rules are documented in PROJECT_OVERVIEW.md §9 (molecule -> sound).
    Returns a fully clamped, ready-to-use parameter set. */
SynthesisParameters mapMoleculeToAudio (const ChemicalDescriptors& d);

/** Build a single-cycle wavetable from a molecule's canonical SMILES.

    The SMILES string seeds a deterministic pseudo-random generator; the
    molecular descriptors decide how many harmonics and how fast they decay,
    so the resulting waveform is unrestricted in shape yet smooth, and always
    reproducible for the same molecule.

    @param d       chemical descriptors driving harmonic count and decay
    @param smiles  canonical SMILES (deterministic seed)
    @param size    number of samples per cycle (default kWaveTableSize)
    @return a normalised single-cycle wavetable */
WaveTable buildWaveTable (const ChemicalDescriptors& d,
                          const juce::String& smiles,
                          int size = kWaveTableSize);

} // namespace organic
