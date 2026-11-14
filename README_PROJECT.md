# HYPERCOLOR --- Project Documentation

This folder contains the agreed product and engineering baseline for
HYPERCOLOR.

## Documents

-   `PRODUCT_VISION.md` --- product identity, principles, audience, MVP
    definition.
-   `FEATURE_SPEC.md` --- functional requirements and feature behavior.
-   `ARCHITECTURE.md` --- proposed technical structure and real-time
    constraints.
-   `ROADMAP.md` --- staged development plan and exit criteria.
-   `CODEX_INSTRUCTIONS.md` --- working rules for AI coding agents.

## Current status

HYPERCOLOR is at the product-definition stage. The implementation has
not started. These documents describe intended behavior and proposed
architecture; they do not claim that any feature has been implemented or
validated.

## Important scope decisions

-   Audio effect only; no built-in synthesizer.
-   Granular processing of incoming audio is the central feature.
-   Freeze is a toggle and must transition smoothly back to live input.
-   Effect chain is reorderable, with at most two instances per module
    family.
-   Modulation is included; arbitrary audio routing is deferred.
-   Up to four LFOs, Envelope Follower, random sources, and macro
    modulation.
-   Performance and stability take priority over expensive feature
    count.
-   MVP target: 50 curated factory presets, DAW state persistence,
    resizable HiDPI UI, and FL Studio validation on macOS and Windows.

## Next engineering step

Inspect the existing repository, then create a minimal JUCE + CMake VST3
pass-through plug-in. Keep the first milestone small and reproducible.
