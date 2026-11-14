# HYPERCOLOR --- Product Vision

**Status:** Product direction agreed; implementation details remain
subject to technical validation.\
**Product:** HYPERCOLOR\
**Type:** Creative audio effect plug-in\
**Primary format:** VST3\
**Platforms:** macOS and Windows\
**Primary DAW validation target:** FL Studio

## 1. Product statement

HYPERCOLOR is a granular-first creative multi-effect processor for
Hyperpop, Digicore, Synthpop, Electropop, and modern electronic music.

Its identity is built around a powerful granular engine that processes
incoming audio, surrounded by a flexible collection of creative effects,
modulation tools, macros, and sound-design workflows.

HYPERCOLOR is an audio effect, not a synthesizer or instrument. It
receives audio from the host and transforms it.

## 2. Product principles

1.  **Sound first.** The quality, musicality, and creative range of the
    Granular Engine matter more than feature count.
2.  **Creative freedom.** Users should be able to build expressive
    effect chains, modulate parameters, and explore unusual sounds
    without fighting the interface.
3.  **Performance first.** The standard version must be stable and
    efficient in real music projects. Expensive experimental features
    must not compromise ordinary sessions.
4.  **Approachable depth.** Useful sounds should be reachable quickly,
    while advanced controls remain available to sound designers.
5.  **A distinct identity.** HYPERCOLOR should feel like its own
    product, not a visual or functional copy of Portal, RC-20, Serum, or
    another plug-in.

## 3. Inspiration and differentiation

Reference products are inspiration for quality and interaction patterns
only:

-   Output Portal: granular processing as a central creative instrument
    and clear granular visualization.
-   XLN Audio RC-20 Retro Color: cohesive multi-effect workflow,
    texture, coloration, and approachable sound shaping.
-   Serum: intuitive modulation assignment and visible modulation
    relationships.

HYPERCOLOR must have original DSP, interaction design, naming, visual
assets, and product identity. Do not copy proprietary UI layouts,
graphics, presets, algorithms, or branding.

## 4. Intended users and use cases

Primary users: - Producers working in Hyperpop, Digicore, Synthpop,
Electropop, and adjacent electronic genres. - Producers processing
vocals, drums, leads, plucks, basses, pads, keys, and samples. - Sound
designers who want granular transformation and interconnected modulation
in one plug-in.

Core use cases: - Turn a vocal phrase into a rhythmic, pitched, frozen,
or glitchy texture. - Transform drums while retaining useful transient
character through parallel blending. - Create evolving pads and
atmospheres from ordinary audio. - Add movement, width, distortion,
filtering, and space to synths. - Generate creative starting points with
genre/character/intensity-aware randomization. - Save complete chains as
reusable presets.

## 5. Core product architecture

### Granular Engine

The central module. It processes live incoming audio and offers multiple
operating modes, including live granulation and a switchable Freeze
mode. Musical and Experimental behavior should coexist.

### Effect families

The initial product direction includes eight families: 1. Drive 2.
Dynamics 3. Space 4. Texture 5. Digital / Crush 6. Modulation 7. Filter
8. Width

The MVP should implement a useful, coherent subset of algorithms within
these families. A family name must not be treated as a requirement to
ship every listed algorithm immediately.

### Modulation

A modulation system with up to four independent LFOs, an Envelope
Follower, random modulation sources, and macro sources. Users can assign
sources to multiple compatible parameters and set modulation amount and
range.

### Presets and macros

Factory presets are organized primarily by instrument/use case. Users
can save their own presets in a dedicated user category. Eight global
macros are the initial target, with user-assignable macro mappings
desirable.

## 6. Visual direction

The visual priority is: 1. Experimental and expressive. 2. Minimal and
premium. 3. Cyber/neon accents.

The interface should feel creative and distinctive, but remain readable
and usable during production. Use animation where it communicates audio
or modulation behavior; keep decorative animation restrained.

The Granular Engine should have a prominent, attractive visualization
inspired by the clarity and appeal of granular visual interfaces, while
remaining visually original.

## 7. MVP success definition

The MVP is successful when a user can:

-   Insert HYPERCOLOR as a VST3 effect in FL Studio on supported macOS
    and Windows builds.
-   Process live incoming mono or stereo audio in real time.
-   Use the Granular Engine and toggle Freeze on and off.
-   Build and reorder an effect chain, with no more than two instances
    of a given module type.
-   Use modulation assignments, up to four LFOs, and the Envelope
    Follower.
-   Use global Dry/Wet and a practical parallel dry/wet path.
-   Save and restore plug-in state inside a DAW project.
-   Save, load, import, and export presets.
-   Access at least 50 curated factory presets.
-   Use a responsive, resizable, HiDPI-aware interface that feels
    coherent and user-friendly.
-   Work without unacceptable clicks, dropouts, crashes, or CPU use in
    representative sessions.

## 8. Explicitly deferred

The following are not MVP requirements: - Arbitrary user-defined audio
routing graphs. - A high-CPU multi-layer/ multi-engine "everything at
once" mode. - A standalone synthesizer or sound generator. - MIDI-learn
and deep MIDI controller workflows. - A huge preset library at launch. -
A full spectrum-analysis suite if it meaningfully increases complexity
or CPU use. - A separate high-CPU fork. The architecture should avoid
blocking future experimentation, but the fork itself is a later product
decision.

## 9. Product scope discipline

When implementation trade-offs arise, prioritize in this order:

1.  Correct, stable real-time audio processing.
2.  Excellent core Granular Engine and Freeze behavior.
3.  State/preset reliability.
4.  Usable effect chain and essential effect families.
5.  Modulation and macros.
6.  Interface polish and visualization.
7.  Optional advanced features.

Do not claim a feature is complete until it has tests and host-level
validation appropriate to the feature.
