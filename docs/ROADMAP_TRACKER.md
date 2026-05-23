# SampleWrangler Roadmap Tracker

## Purpose

This document is the durable handoff for future sessions. It captures the full product and technical roadmap proposed in the May 2026 review, plus the current implementation state.

## Vision

Build the most compelling desktop sample librarian possible while complementing Audiocity instead of duplicating it:

- SampleWrangler should be the fastest place to scan, search, organize, preview, and compare sample assets and sampler presets.
- Audiocity remains the deep sampler/instrument environment.
- Shared format coverage matters: if Audiocity can import it, SampleWrangler should ideally index it, preview it when practical, and help hand it off into Audiocity.

## Completed So Far

### Sprint A

- OGG scan and preview support is enabled.
- Audiocity `.acp` presets are indexed and previewed in a first pass.
  - Supports binary or XML JUCE `ValueTree` payloads.
  - Decodes embedded base64 float audio.
  - Falls back to referenced external sample paths when resolvable.
- Preview sample-buffer handoff no longer uses `atomic<shared_ptr>` in the audio callback.
  - Buffers are staged into slots on the message thread.
  - The audio thread swaps buffers at command-drain time and retires old slots for message-thread reclamation.
- Standard resample playback now renders contiguous spans between loop/end boundaries instead of checking wrap logic on every sample.
- The HQ toggle now means something:
  - Non-stretch preview uses cubic Hermite interpolation when HQ is enabled.
  - Stretch mode uses the higher-quality Rubber Band path only when HQ is enabled.
  - Without Rubber Band, HQ still improves non-stretch preview quality.

## Current Validation State

- `SampleWrangler` builds successfully with CMake Tools.
- `SampleWranglerAcpPresetReaderTests` builds successfully.
- `SampleWranglerCatalogDbTests` builds successfully and covers `content_hash`, favorites/ratings/tags, duplicate lookup, and saved searches.
- `SampleWranglerScannerAppleLoopTests` builds successfully.
- `SampleWranglerWaveformPeakTests` passes in CTest.
- CTest passes all four current tests (`CatalogDb`, `ScannerAppleLoop`, `WaveformPeak`, `AcpPresetReader`).
- Remaining build warnings are pre-existing JUCE display deprecation warnings in `Source/App/MainComponent.cpp`.
- CMake Tools still emits missing `DartConfiguration.tcl` warnings during test execution, but the tests themselves pass.

## Full Roadmap

### Sprint A: Preview Engine Foundation

Status: Complete

- OGG support
- Audiocity `.acp` reader + preview bridge
- RT-safe sample-buffer slot publication
- Direct playback loop hoisting
- HQ Hermite interpolation

### Sprint B: Catalog and Library Intelligence Foundation

Status: In progress

- Introduce `PRAGMA user_version` schema migrations. Done.
- Add `content_hash` or near-duplicate hash for duplicate detection and move resilience. Storage, scanner ingestion, and duplicate-query foundation are done.
- Add favorites, ratings, tags, and saved searches. Database foundation is done; UI/query integration is still pending.
- Consolidate waveform cache persistence strategy if the file-based and blob-based systems remain redundant.
- Add source-folder live watching on Windows.

### Sprint C: Format Breadth to Match Audiocity

Status: Not started

Highest-value additions:

- SFZ minimal reader/indexer
- DecentSampler `.dspreset`
- Bitwig `.multisample`
- SF2

Later candidates:

- EXS24 `.exs`
- Reason NN-XT `.sxt`
- Ableton `.adv` / `.adg`
- MPC `.xpm`
- TAL `.talsmpl`
- TX16Wx `.txprog`
- Korg multisample formats
- Additional Audiocity-compatible preset and instrument containers

### Sprint D: Audio Intelligence and Discovery Features

Status: Not started

- BPM detection
- Key detection
- Duplicate/near-duplicate browsing
- ML-assisted sample classification
- Similarity search / “find sounds like this”
- Smarter loop-point and transient analysis

### Sprint E: Audiocity Bridge and Product Differentiation

Status: Partially started through `.acp` preview support

- Auto-discover Audiocity preset/library folders
- “Open in Audiocity” handoff action
- Drag-and-drop flows that work cleanly into Audiocity
- Index all Audiocity-importable formats even when preview is index-only
- Broaden `.acp` compatibility if more external path keys or embedded encodings are found

## Product Features Still Recommended

### Library UX

- Tags, favorites, ratings
- Smart folders / saved queries
- Filter sidebar by format, BPM, key, duration, channels, loop availability
- Duplicate finder
- Recently added / recently previewed panes

### Preview UX

- Hover-to-preview
- Pitch-locked preview and tempo-matched preview
- Reverse playback
- Per-file persisted preview settings
- Start/end markers in addition to loop points
- Simple amp envelope and filter for more musical auditioning

### Intelligence

- Key / BPM / transient / loop classification
- Similarity search
- Auto-tagging or category prediction

## Known Technical Follow-Ups

- The direct playback path is improved, but there is still room for deeper sampler-core work:
  - Consider fixed-point phase accumulation in the long term.
  - Consider further SIMD/vectorized mixing for steady-state spans.
  - Revisit the granular fallback quality if Rubber Band is unavailable.
- `Voice.h` remains heavy and still contains Rubber Band machinery that could move into a `.cpp` implementation object later.
- The `MainComponent` class is still large and could eventually be split into controller/state pieces.

## Suggested Next Session Order

1. Continue Sprint B by wiring favorites, ratings, tags, and saved searches into the browser/results UI.
2. Expose duplicate-file browsing/workflows using the new `content_hash` foundation.
3. Add source-folder live watching on Windows.
4. Begin Sprint C with SFZ and DecentSampler because they provide the best format-value/effort ratio.
5. Add Audiocity handoff actions once the catalog can browse both raw samples and `.acp` presets reliably.

## Sprint B Progress Notes

- Catalog schema evolution now uses `PRAGMA user_version` migrations instead of only ad hoc `ALTER TABLE` checks.
- The `files` table stores `content_hash` and indexes it.
- Scanner ingestion computes a sampled content fingerprint from file size plus the first/last 64 KB.
- `CatalogDb::listDuplicateFiles()` is available for future duplicate-browser UI work.
- Favorites, ratings, tags, and saved searches now persist in dedicated catalog tables so rescans do not overwrite user-authored metadata.
- Browser/search UI for those metadata features and live watching are still pending.

## Files Touched in Sprint A

- `Source/Pipeline/AcpPresetReader.h`
- `Source/Pipeline/AcpPresetReader.cpp`
- `Source/Pipeline/Scanner.cpp`
- `Source/App/MainComponent.cpp`
- `Source/Audio/Voice.h`
- `Source/Audio/VoiceManager.h`
- `Source/Audio/VoiceManager.cpp`
- `Source/UI/PreviewPanel.cpp`
- `Tests/AcpPresetReaderTests.cpp`
- `CMakeLists.txt`
- `docs/SPRINT_A_TRACKER.md`

## Quick Validation Checklist

- Build `SampleWrangler`.
- Build `SampleWranglerAcpPresetReaderTests`.
- Verify `.ogg` preview.
- Verify `.acp` preset scan and preview with embedded audio.
- Verify `.acp` preset preview with an external referenced sample path.
- Toggle HQ on/off and confirm standard pitch-shift preview quality changes audibly.
