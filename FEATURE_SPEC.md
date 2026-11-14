# HYPERCOLOR --- Feature Specification

**Status:** Agreed product requirements for planning. Detailed DSP
algorithms and exact parameter ranges require engineering design and
listening tests.

## 1. Plug-in behavior

-   Audio effect only; no internal sound generator or instrument voice
    engine.
-   VST3 target for macOS and Windows.
-   Real-time processing of host input.
-   Baseline support: mono and stereo layouts. Other layouts require
    explicit design and tests before being advertised.
-   Support common sample rates from 44.1 kHz through 192 kHz.
-   Handle host transport/BPM when available; behave sensibly when tempo
    is unavailable.
-   Report processing latency to the host whenever an algorithm
    introduces latency. Prefer zero added latency for the standard
    live-processing path.

## 2. Granular Engine

### 2.1 Role

The Granular Engine is the signature feature and must receive the
highest DSP and QA priority. It processes incoming audio rather than
functioning merely as a sample player.

### 2.2 Modes

Design a mode system that can accommodate: - **Live Granulation:**
continuously granulate the incoming signal using a bounded internal
audio buffer. - **Freeze:** capture a recent section of incoming audio
and continue generating grains from that captured material. - **Buffer
Playback / other modes:** only add when they provide a distinct useful
workflow and fit the performance budget.

The exact mode list and parameterization should be finalized during DSP
prototyping.

### 2.3 Freeze behavior

-   Freeze is an explicit toggle that can be turned on and off.
-   On activation, capture a recent segment from a circular/ring buffer.
    The initial design target is a configurable capture window in the
    approximate 2--10 second range, subject to memory and CPU
    evaluation.
-   While Freeze is active, the engine continues processing the captured
    audio even if the live input stops.
-   Provide useful control over the captured region/position where
    practical.
-   On deactivation, transition back to live input smoothly. Avoid
    abrupt discontinuities and clicks; use a controlled crossfade or
    equivalent transition.
-   Repeated toggling must not corrupt buffer state or cause
    audio-thread allocations.
-   Decide how the frozen buffer is serialized in plug-in state. Prefer
    restoring the audible frozen state when feasible, but impose
    sensible size limits and avoid bloating every DAW project. If full
    buffer serialization is not feasible, document the fallback clearly
    and preserve all other state.

### 2.4 Musical and Experimental behavior

-   **Musical:** controllable, repeatable, tempo-aware behavior and
    sensible parameter ranges.
-   **Experimental:** broader randomization and more extreme textures.
-   Users should be able to move between these behaviors without losing
    the ability to control individual parameters.
-   Do not make a single global Chaos knob the only way to control
    randomness.

### 2.5 Core parameter families

Exact ranges and units are engineering decisions. Include suitable
controls for: - Grain Size - Density - Position - Spray / Position
Randomness - Pitch - Pitch Randomness - Reverse and Reverse
Probability - Grain Shape / window - Jitter - Pan and Pan Randomness -
Density Randomness - BPM Sync / rhythmic division where appropriate -
Freeze and captured-buffer position/length where appropriate

### 2.6 Sequencing and layering

-   A basic rhythmic grain sequencer is desirable but lower priority
    than a reliable core engine.
-   Multiple simultaneous grain layers/streams are desirable only if a
    standard configuration remains efficient.
-   Start with a performance-conscious implementation. Do not ship a
    multi-layer mode if it causes instability or disproportionate CPU
    use.
-   A future advanced build/fork may explore more expensive multi-layer
    processing.

### 2.7 Visualization

Provide a useful real-time visualization of: - Audio waveform or buffer
content. - Current read/position region. - Grain size and
density/overlap in a comprehensible form. - Freeze state and captured
region.

Visualization must not run heavy DSP or allocate in the audio callback.
Use safe, bounded communication from audio processing to the UI. UI
refresh rate should be capped appropriately.

## 3. Effect chain and modules

### 3.1 Chain behavior

-   Users can add, remove, enable/disable, reorder, and edit modules.
-   The order of modules is user-configurable.
-   Maximum of two instances per module family in the chain.
-   The Granular Engine is a central module; product design may choose
    whether it can be moved or pinned, but the default workflow should
    make it easy to find.
-   No arbitrary audio-routing graph in the MVP.
-   Support global dry/wet blending and a simple parallel dry path
    around the processed chain.

### 3.2 Effect families

The product direction includes these eight families. Implement the
smallest useful set of algorithms in each family that can meet quality
and stability standards.

**DRIVE** - Saturation / waveshaping as a core. - Additional
tape/tube/clip/fuzz characters only when implemented with distinct,
tested behavior.

**DYNAMICS** - Compressor or glue-style dynamics as a baseline. -
Multiband/OTT-like processing is optional and must be evaluated for CPU
and latency.

**SPACE** - Reverb and delay as core candidates. - Shimmer, reverse
reverb, ping-pong, and advanced diffusion are later/optional.

**TEXTURE** - Noise, vinyl/tape-like texture, hiss/crackle, and
controlled wow/flutter. - Keep noise generation deterministic where
needed for stable rendering and state behavior.

**DIGITAL / CRUSH** - Bitcrushing and sample-rate reduction as core
candidates. - Glitch and more elaborate digital destruction are
optional.

**MODULATION** - Chorus, phaser, flanger, tremolo, vibrato, or detune;
select a coherent initial subset.

**FILTER** - Useful multimode filter as a baseline. - Formant,
resonator, and comb modes are optional extensions.

**WIDTH** - Stereo width and safe stereo movement. - Preserve mono
compatibility and avoid uncontrolled phase cancellation.

## 4. Modulation system

### 4.1 Sources

-   Up to four independent LFOs.
-   Envelope Follower.
-   Random source(s).
-   Global macro controls as modulation sources.
-   Additional sources can be considered later.

### 4.2 LFO controls

Each LFO should support, as practical: - Sine, Triangle, Saw, Square,
and Random shapes. - Free rate and host-tempo synchronization. -
Phase. - Amount. - Reset/retrigger behavior where useful. - Assignment
to multiple compatible destinations.

### 4.3 Assignments

-   Source-to-destination mapping.
-   Per-assignment amount/depth.
-   Bipolar/unipolar behavior or an equivalent clear range control.
-   Destination range/minimum/maximum where useful.
-   Clear visual indication that a parameter is modulated.
-   Avoid ambiguous stacking behavior when multiple sources target one
    parameter; define and test the modulation-combination rule.

### 4.4 Envelope Follower

-   Tracks input envelope with configurable response/smoothing.
-   Can modulate multiple destinations.
-   Must be stable and useful for vocals, drums, and other transient
    material.
-   Must not cause clicks or parameter discontinuities.

### 4.5 Audio routing exclusion

No user-configurable audio routing matrix or arbitrary parallel node
graph in the MVP. Modulation routing is included. A more advanced
audio-routing system is deferred to a possible future fork/version.

## 5. Macros and randomization

### 5.1 Macros

-   Target approximately eight global macro controls.
-   Initial suggested names: Color, Space, Crush, Movement, Texture,
    Intensity, Hyper, and one user-configurable macro.
-   Names are provisional.
-   Users should ideally be able to map one macro to multiple parameters
    with custom ranges and polarity.
-   Macro mappings must be saved in plug-in state and presets.

### 5.2 Randomizer

-   Provide randomization profiles based on Genre, Intensity, and
    Character.
-   Randomize relevant parameters within musically useful bounds.
-   Provide per-parameter randomness controls in the Granular Engine.
-   Avoid changing every parameter blindly; preserve output safety and
    useful signal levels.
-   A full-chain randomizer that can select module types/order is
    desirable but lower priority than parameter randomization.
-   Consider a seed or undo-friendly behavior so users can compare and
    recover results.

## 6. Master section and metering

Include a master/output area with: - Output Gain. - Global Dry/Wet. -
Input and output level metering. - Peak indication and clipping
indicator. - RMS metering where practical. - Stereo width indication
where practical. - Safety clipping/limiting only if it can be
implemented transparently and clearly. Do not silently alter dynamics in
a way users cannot understand. - A spectrum analyzer is optional;
include only if it is useful and does not compromise performance or
visual clarity.

## 7. Quality and performance controls

-   Oversampling options: Off / 2x / 4x.
-   Default: Off.
-   Oversampling should be applied only where it materially improves
    quality, not indiscriminately to the entire plug-in.
-   Optional Eco / Normal / High quality modes may be added only if they
    provide clear, non-duplicative behavior beyond oversampling.
-   Prioritize low latency for live processing.
-   Measure and report latency accurately when non-zero.
-   Do not make unsupported claims about CPU usage; benchmark
    representative chains and publish measured results internally.

## 8. Presets

### 8.1 Factory categories

Organize primarily by source/instrument: - Vocals - Drums - Leads -
Plucks - Bass - Pads - Keys - FX - Experimental

### 8.2 User presets

-   Dedicated User Presets category.
-   Save current full chain and parameter state.
-   Favorites, search, and tags are desirable.
-   Import/export user presets.
-   Preset browser must distinguish factory and user content.

### 8.3 MVP quantity

At least 50 curated factory presets. Sound quality and usefulness are
more important than quantity.

### 8.4 Version compatibility

-   Every preset/state document must include a format/schema version.
-   Newer plug-in versions should aim to load older presets and migrate
    them forward.
-   Older plug-in versions are not required to load presets created by
    newer versions.
-   Unknown parameters/modules must be handled safely: use defaults,
    skip unsupported optional data, or show a clear warning rather than
    corrupting state.
-   Define migration tests for representative old preset fixtures.
-   Plug-in project state and portable user preset files may use related
    but distinct containers/schemas.

## 9. Workflow features

-   A/B comparison, if it can be implemented without duplicating
    expensive DSP paths continuously. Prefer storing two parameter/chain
    states and switching safely rather than running two complete chains
    in parallel.
-   Undo / Redo for meaningful user operations, including randomization,
    chain edits, and parameter changes where feasible.
-   Full DAW state save/restore.
-   Parameter automation support through the host for exposed
    parameters.
-   MIDI learn is not a priority.

## 10. User interface and identity

-   Hybrid layout: main overview with macros and key controls, plus
    dedicated module/detail views.
-   Resizable UI.
-   HiDPI / Retina-aware rendering.
-   Experimental visual character first, supported by minimal/premium
    readability and restrained cyber/neon accents.
-   Combine meaningful animation with static elements.
-   Granular visualization is a signature UI element.
-   Consistent typography, spacing, color system, and original visual
    identity.
-   Tooltips/help text are optional and not a priority.
-   UI should remain responsive and should not block audio processing.

## 11. State persistence

Persist all necessary state through the VST3/host state mechanism: -
Chain topology and order. - Module enabled state and parameters. -
Modulation sources and assignments. - Macro mappings. - Global controls
and quality settings. - Preset identity where relevant. - Freeze state
and captured audio where feasible within defined limits.

Test save/reopen behavior in FL Studio. State restoration must be robust
when loading older versions and when a preset contains data not
recognized by the current build.
