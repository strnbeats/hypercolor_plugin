# HYPERCOLOR --- Technical Architecture

**Status:** Proposed architecture for engineering validation. This
document is a set of constraints and design directions, not a claim that
the implementation already exists.

## 1. Technology direction

-   Language: C++20.
-   Audio/plugin framework: JUCE.
-   Build system: CMake.
-   Primary plug-in format: VST3.
-   Platforms: macOS and Windows.
-   Tests: Catch2 or another lightweight C++ test framework, selected
    after project bootstrap.
-   Preset metadata/state: versioned structured format such as JSON,
    with a robust binary or chunk container for audio data if needed.
-   Python may be used for offline development tools, preset validation,
    or asset generation; it must not be required at plug-in runtime.

Use supported, pinned dependency versions. Record JUCE licensing
implications and distribution obligations before public/commercial
distribution. Do not vendor large dependencies without a deliberate
decision.

## 2. High-level layers

Separate the project into clear layers:

1.  **Plugin adapter**
    -   JUCE AudioProcessor integration.
    -   Host parameter exposure and automation.
    -   Bus/layout negotiation.
    -   State serialization hooks.
    -   Host transport/tempo access.
2.  **Audio engine**
    -   Owns the processing graph/order.
    -   Processes audio blocks.
    -   Handles dry path, wet path, master gain, and output safety.
    -   Applies quality/oversampling configuration.
    -   Must be independent from UI components.
3.  **DSP modules**
    -   Granular Engine.
    -   Drive, Dynamics, Space, Texture, Digital/Crush, Modulation,
        Filter, Width.
    -   Reusable parameter and modulation interfaces.
    -   No UI dependencies.
4.  **Modulation engine**
    -   Up to four LFOs.
    -   Envelope Follower.
    -   Random sources.
    -   Macro sources and destination mapping.
    -   Deterministic, bounded modulation combination.
5.  **State and preset layer**
    -   Versioned serialization.
    -   Migration from older schema versions.
    -   Factory and user preset handling.
    -   Safe validation of imported data.
6.  **UI**
    -   Main overview.
    -   Module/detail views.
    -   Chain editor.
    -   Modulation assignment interface.
    -   Preset browser.
    -   Metering and granular visualization.

## 3. Real-time audio rules

The audio callback must be treated as a real-time critical section.

Do not perform the following in the audio callback: - Heap
allocation/deallocation. - Mutex locking or waiting. - File or network
I/O. - JSON parsing or serialization. - Blocking calls. - UI operations
or component access. - Unbounded loops or unpredictable work. - Logging
that may allocate or block.

Use preallocated buffers, bounded data structures, lock-free or
otherwise real-time-safe communication where needed, and explicit
maximum block/channel assumptions. Validate assumptions against host
behavior.

Parameter changes from the UI/host must reach DSP safely. Prefer JUCE
parameter abstractions and carefully designed atomic/snapshot
mechanisms. Avoid reading mutable UI-owned objects from the audio
thread.

## 4. Granular Engine design constraints

-   Use a preallocated circular audio buffer for live input and Freeze
    capture.
-   Define maximum buffer duration, channel count, and memory footprint.
-   Separate grain scheduling from grain rendering where useful.
-   Use bounded voice/grain pools; do not allocate one object per grain
    at runtime.
-   Use appropriate window functions to reduce clicks.
-   Handle buffer wrap, channel layouts, sample-rate changes, and
    block-size changes safely.
-   Provide a bounded maximum active grain count and a deterministic
    overload policy.
-   Avoid uncontrolled CPU growth when Density or Grain Size is extreme.
-   Make random behavior reproducible when appropriate for offline
    rendering and preset recall.
-   Smooth parameters that can cause discontinuities.
-   Freeze on/off transitions must be click-safe.
-   The visualization must consume snapshots or decimated data, not
    inspect mutable audio buffers unsafely.

A standard configuration should be the performance baseline.
Multi-layer/parallel grain streams are optional and must be benchmarked
before inclusion.

## 5. Effect chain design

Represent the chain using a bounded set of module instances with stable
IDs and typed module identifiers.

Requirements: - Add/remove/reorder/enable/disable. - Maximum two
instances per module family. - Stable parameter identity for host
automation and state migration. - Preallocate processing resources
before audio processing begins or during safe non-audio-thread
reconfiguration. - Avoid rebuilding or allocating the chain in the audio
callback. - Define safe transition behavior when chain topology
changes. - Global dry path runs in parallel with the processed chain;
this is not a general user-defined audio routing graph. - If chain
rebuilding requires crossfading or temporary dual processing, bound its
CPU/memory cost and ensure it cannot cause dropouts.

Do not implement arbitrary node-based audio routing in the MVP.

## 6. Modulation architecture

Model modulation as source-to-destination assignments, separate from
audio routing.

Sources: - LFO 1--4. - Envelope Follower. - Random source(s). - Macro
controls.

Each assignment should define: - Stable source ID. - Stable destination
parameter ID. - Amount/depth. - Unipolar/bipolar mode or equivalent. -
Optional minimum/maximum range.

Define deterministic combination behavior when multiple sources target
the same parameter. Modulation must be bounded and must not mutate host
parameter values in a way that breaks automation/state. Keep base
parameter value and modulation offset separate.

LFO and envelope state must be processed efficiently per sample or per
block as required. Avoid unnecessary oversampling of modulation control
signals.

## 7. Oversampling and latency

-   Off / 2x / 4x, default Off.
-   Apply oversampling selectively to nonlinear processors where
    beneficial.
-   Document and report latency to the host when applicable.
-   Reconfigure oversampling safely outside the audio callback.
-   Test transitions between modes.
-   Do not promise zero latency for algorithms that require lookahead,
    buffering, or oversampling filters.

## 8. State and preset architecture

Use explicit schema versions.

Separate: - **Host project state:** complete plug-in state required to
restore a session. - **Portable preset:** reusable chain/settings
intended for browser/import/export.

State should include stable IDs, module types, parameter values,
modulation mappings, chain order, and global settings.

Freeze buffer persistence needs a deliberate policy: - Option A:
serialize a bounded captured buffer when Freeze is active. - Option B:
serialize Freeze metadata and restore a safe fallback if audio data is
unavailable. - Evaluate project size, host state limits, load time, and
reliability before choosing. - Never serialize or deserialize large
buffers on the audio thread.

Migration: - New versions should load old schemas and migrate to the
current schema. - Old versions need not load newer schemas. - Unknown
optional data should be ignored safely; unknown required data should
trigger a clear, non-destructive warning/fallback. - Keep fixture files
for migration tests.

## 9. UI architecture and thread communication

-   UI must never own DSP state.
-   DSP must never call UI components.
-   Use bounded snapshots/atomics or a suitable lock-free mechanism for
    meters and visualization.
-   Limit UI refresh rate.
-   Decimate waveform/grain display data.
-   Avoid expensive path generation or large allocations every frame.
-   Support resizing and HiDPI/Retina.
-   Use a consistent layout system and scalable typography.
-   Animations should be meaningful and optional/restrained.

## 10. Performance and robustness

-   Benchmark CPU for representative configurations: idle/bypass,
    granular live, Freeze, full chain, modulation-heavy chain, and
    oversampling 2x/4x.
-   Benchmark at supported sample rates and realistic block sizes.
-   Test mono/stereo.
-   Test extreme parameter values and rapid automation.
-   Test repeated Freeze toggles and chain reorder operations.
-   Test host stop/start, transport changes, sample-rate changes, and
    project reopen.
-   Use sanitizers and static analysis where practical.
-   Keep DSP unit tests independent from the DAW.
-   Validate in FL Studio on macOS and Windows before calling the MVP
    release-ready.

No arbitrary CPU percentage should be promised before measurement on
representative hardware.
