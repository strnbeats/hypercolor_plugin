# HYPERCOLOR --- Development Roadmap

The roadmap is deliberately incremental. Every milestone should leave
the project in a buildable, testable state.

## Phase 0 --- Repository and engineering foundation

Deliverables: - Review existing repository and preserve useful files. -
Establish CMake + JUCE project structure. - Configure VST3 target and
platform build instructions. - Add formatting conventions, warnings, and
basic CI/build checks where practical. - Add a minimal plug-in that
loads in a host and passes audio through. - Add a short developer
README.

Exit criteria: - Clean configure/build on the primary development Mac. -
VST3 artifact loads in a compatible host. - Basic mono/stereo
pass-through works. - No DSP features are started before the baseline is
stable.

## Phase 1 --- Audio core and parameter/state foundation

Deliverables: - Stable parameter IDs and host automation. - Audio
processor lifecycle and bus handling. - Dry/wet and output gain. -
Input/output peak metering. - State save/restore for basic parameters. -
Unit-test structure and test audio utilities.

Exit criteria: - Host automation works. - Save/reopen restores
parameters. - No audio-thread allocation/locking in the basic path.

## Phase 2 --- Granular Engine prototype

Deliverables: - Preallocated circular buffer. - Basic live granular
processing. - Grain scheduling/rendering with bounded resources. - Grain
windows and click reduction. - Core controls: Size, Density, Position,
Pitch. - Basic musical and experimental behavior. - CPU and
audio-quality tests.

Exit criteria: - Stable audio under sustained playback. - No
uncontrolled CPU growth at parameter extremes. - No obvious clicks under
normal parameter changes. - Reproducible test cases for key DSP
behavior.

## Phase 3 --- Freeze and granular visualization

Deliverables: - Toggleable Freeze capture. - Smooth transition from
frozen buffer back to live input. - Buffer position/length controls
where practical. - Granular visualization with safe UI/audio
communication. - Freeze state serialization decision and implementation.

Exit criteria: - Repeated Freeze toggling does not click or corrupt
state. - DAW save/reopen behavior is documented and tested. -
Visualization remains responsive without audio-thread blocking.

## Phase 4 --- Effect chain framework

Deliverables: - Module instance model with stable IDs. -
Add/remove/reorder/enable/disable. - Maximum two instances per family. -
Safe chain reconfiguration. - Global parallel dry path and wet chain. -
Master/output section.

Exit criteria: - Chain changes do not cause dropouts in tested
scenarios. - State restores chain order and module settings. - CPU
behavior is bounded and benchmarked.

## Phase 5 --- Initial effect modules

Suggested implementation order: 1. Filter. 2. Drive. 3. Digital / Crush.
4. Modulation effects. 5. Space. 6. Texture. 7. Width. 8. Dynamics.

This order is a proposal and can change after listening tests. Each
module must have a useful minimum feature set and tests before moving
on.

Exit criteria: - Modules behave correctly at supported sample rates. -
Oversampling is available where useful and reports latency correctly. -
No module produces unexpected NaNs, runaway levels, or unstable state.

## Phase 6 --- Modulation and macros

Deliverables: - Up to four LFOs. - Envelope Follower. - Random
modulation source(s). - Source-to-destination assignment system. -
Per-assignment depth/range and clear UI indication. - Approximately
eight macros. - User macro mappings where practical.

Exit criteria: - Modulation is bounded and stable. - Multiple sources
targeting one destination follow documented combination rules. -
Automation, state restore, and modulation work together predictably.

## Phase 7 --- Presets and workflow

Deliverables: - Versioned preset schema. - Factory and User Presets
categories. - Import/export. - Search/favorites/tags where practical. -
A/B comparison. - Undo/Redo for meaningful operations. - Genre /
Intensity / Character randomization. - Full-chain randomizer if time and
quality permit. - At least 50 curated factory presets.

Exit criteria: - Presets load consistently. - Older preset fixtures
migrate forward. - User presets survive application/plugin updates. -
Randomization can be undone or compared.

## Phase 8 --- UI/UX polish

Deliverables: - Main overview and module/detail views. - Resizable and
HiDPI-aware layout. - Experimental/minimal visual identity. - Granular
visualization polish. - Responsive preset browser and chain editor. -
Clear disabled/bypass/modulation states. - Useful defaults and sensible
parameter ranges.

Exit criteria: - Interface remains readable at supported sizes. - No UI
action blocks the audio thread. - Common workflows are understandable
without documentation.

## Phase 9 --- Release validation

Deliverables: - macOS and Windows builds. - FL Studio validation on both
platforms. - Sample-rate and block-size test matrix. - Long-session
stability tests. - CPU benchmark report. - Preset migration tests. -
Installer/package and licensing review. - Release notes and known
limitations.

Exit criteria: - No known critical crashes, audio dropouts, or state
corruption. - MVP requirements are verified. - 50 factory presets are
curated and checked. - Build and installation instructions are
reproducible.

## Later / possible fork

Potential future work: - More simultaneous granular layers and
streams. - Arbitrary audio routing graph. - Higher-CPU
spectral/time-domain processing. - Expanded sequencer and modulation
tools. - More advanced dynamics and space algorithms. - Additional
effect families and larger preset library.

A separate high-CPU fork should only be created after real usage and
profiling demonstrate a need. Do not prematurely split the codebase.
