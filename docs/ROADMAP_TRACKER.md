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
- The current sampler-engine follow-up build is clean after moving Rubber Band state and helpers out of `Source/Audio/Voice.h` into `Source/Audio/Voice.cpp` and tightening the preserve-length render path.
- `SampleWranglerVoiceManagerRenderTests` now builds successfully and gives the sampler engine an offline render harness that exercises primary playback completion, direct and preserve-length loop behavior, preserve-length loop-boundary wrapping, scrub resets, preserve-length duration behavior, source-sample-rate handling, stereo direct steady-state and fade-in channel separation, short-clip HQ fallback audibility, fresh-start HQ preserve-length playback, and deterministic live HQ-toggle deferral to the next note through the public `VoiceManager` API.
- `SampleWranglerAcpPresetReaderTests` builds successfully.
- `SampleWranglerCatalogDbTests` builds successfully and now also round-trip the indexed preset summary fields (`preset_name`, `zone_count`, `key_low/high`, `velocity_low/high`).
- `SampleWranglerScannerAppleLoopTests` builds successfully and now covers Apple Loop AIFF metadata plus indexed SFZ, Bitwig `.multisample`, Korg `.korgmultisample`, SoundFont `.sf2`, DecentSampler `.dspreset`, TAL `.talsmpl`, and TX16Wx `.txprog` preset ingestion.
- `SampleWranglerWaveformPeakTests` passes in CTest.
- CTest now passes all five current tests (`CatalogDb`, `ScannerAppleLoop`, `WaveformPeak`, `AcpPresetReader`, `VoiceManagerRender`).
- The JUCE display deprecation warnings in `Source/App/MainComponent.cpp` are resolved.
- The CMake/CTest `DartConfiguration.tcl` warning is resolved by using `include(CTest)`.

## Full Roadmap

### Sprint A: Preview Engine Foundation

Status: Complete

- OGG support
- Audiocity `.acp` reader + preview bridge
- RT-safe sample-buffer slot publication
- Direct playback loop hoisting
- HQ Hermite interpolation

### Sprint B: Catalog and Library Intelligence Foundation

Status: Complete

- Introduce `PRAGMA user_version` schema migrations. Done.
- Add `content_hash` or near-duplicate hash for duplicate detection and move resilience. Storage, scanner ingestion, and duplicate-query foundation are done.
- Add favorites, ratings, tags, and saved searches. Database foundation and results/browser UI integration are done.
- Consolidate waveform cache persistence strategy if the file-based and blob-based systems remain redundant. Runtime reads now use blob-backed peaks only.
- Add source-folder live watching on Windows. Done via per-root watcher callbacks that queue rescans when the app is idle.

### Sprint C: Format Breadth to Match Audiocity

Status: In progress

Highest-value additions:

- SFZ minimal reader/indexer. Landed: `.sfz` files are cataloged as index-only presets, resolve their first referenced sample when available, respect `default_path`, and persist waveform overview peaks for list previews.
- DecentSampler `.dspreset`. Landed with the same indexed-preset treatment as SFZ.
- Bitwig `.multisample`. Landed with `multisample.xml` parsing, in-archive sample probing, and blob waveform peak persistence.
- Korg `.korgmultisample`. Landed with zip/XML manifest parsing, embedded sample probing, and preset-zone summary metadata.
- TAL `.talsmpl`. Landed as an indexed preset format that resolves its first referenced sample and extracts basic key/audio metadata.
- TX16Wx `.txprog`. Landed as an indexed preset format that resolves its first referenced sample and extracts basic key/audio metadata.
- SF2. Landed as an index-only SoundFont probe that extracts the first preset name, representative sample metadata, key/velocity coverage, loop information, and a lightweight waveform overview from the embedded `smpl` PCM data.

Later candidates:

- EXS24 `.exs`
- Reason NN-XT `.sxt`
- Ableton `.adv` / `.adg`
- MPC `.xpm`
- Korg multisample formats
- Additional Audiocity-compatible preset and instrument containers

### Sprint D: Audio Intelligence and Discovery Features

Status: Not started

Detailed execution plan: see `docs/SPRINT_D_TRACKER.md`.

Recommended landing order:

- Analysis substrate and persistence
- Duplicate and near-duplicate discovery
- BPM, key, transient, and loop inference
- Similarity search
- ML-assisted classification as a stretch goal

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
  - Direct non-stretch playback now has a fixed-phase steady-state fast path for the common active-voice case, reducing per-sample branching in `VoiceManager::renderVoice`.
  - The non-Rubber-Band granular fallback now uses a smoother shaped grain crossfade instead of a raw linear blend.
  - Rubber Band state and helper machinery now live behind a `Voice.cpp` implementation object instead of inflating `Voice.h`.
  - The active Rubber Band RT render path now updates pitch scale through the real chunked preserve-length path.
  - The non-Rubber-Band preserve-length fallback now advances its source anchor using the natural source-rate step (`bufferSampleRate / currentSampleRate`) instead of assuming a 1:1 file/output rate.
  - Preserve-length HQ mode is now latched per note, so changing the HQ setting during active playback defers the render-mode change to the next note instead of attempting to hot-switch between granular and Rubber Band paths mid-stream.
  - The common looped mono/stereo granular preserve-length path now shares the fixed-phase, chunk-mixed fast path via wrap-aware grain-phase reads instead of falling back to the older per-sample double-position loop.
  - The granular preserve-length fallback now caches mixed source samples per frame before fanning them out to output channels, avoiding repeated interpolation reads for mono/stereo previews.
  - The common no-loop mono/stereo granular preserve-length path now has a fixed-phase, chunk-mixed fast path that reuses the selected interpolator and fans out through contiguous scratch buffers before adding into the output.
  - The granular preserve-length fallback now also uses mono/stereo-specific output fanout in the common 1- and 2-channel cases, removing another per-sample channel-mapping layer from that hot path.
  - The granular preserve-length path now routes mono/stereo sample reads through pointer-specialized interpolators built around the already-selected interpolation function, removing another layer of per-sample channel and interpolation-mode dispatch from the common case.
  - The steady-state non-preserve mono/stereo path now mixes in chunks and uses vectorized fanout (`FloatVectorOperations::add` plus contiguous scratch buffers) instead of per-sample per-channel accumulation in the common case.
  - The faded non-preserve direct mono/stereo path now also mixes in chunks and uses vectorized fanout in the common attack/release case, reducing per-sample output-channel dispatch without changing fade progression.
  - The faded non-preserve direct path now caches interpolated source samples per frame before distributing them to output channels, removing another repeated-read path during attack/release playback.
  - When Rubber Band RT cannot produce startup output before a short clip is exhausted, the current note now degrades to the granular preserve-length path instead of failing silently.
  - Consider further SIMD/vectorized mixing for steady-state spans.
  - Consider pushing the same chunked/vector fanout pattern into the remaining wider-channel generic paths if profiling shows it is worth the extra complexity.
- If true in-flight HQ/Rubber Band switching is ever required, design it explicitly as a crossfade or note re-arm flow; the current contract is deterministic deferral to the next note.
- Extend the offline `VoiceManager` render tests further with repeated-wrap stress cases, wider-channel fallback coverage, and any future true in-flight mode-switch design if that becomes a product goal.
- The `MainComponent` class is still large and could eventually be split into controller/state pieces.

## Suggested Next Session Order

1. Keep Sprint C paused for now and continue sampler-core work by profiling the remaining wider-channel and generic render paths for additional SIMD/vector opportunities.
2. Revisit the non-Rubber-Band preserve-length fallback after listening tests and profiling, especially around grain sizing, spacing, denormal safety, and any further fixed-phase opportunities.
3. Extend the offline `VoiceManager` harness with repeated-wrap stress cases and wider-channel fallback coverage before attempting more invasive render-path changes.
4. Resume Sprint C only after the sampler engine slice is stable enough to support more ambitious preview workflows.
5. Expose richer preset-specific metadata in the UI beyond the new summary row once format breadth work resumes.

## Sprint B Completion Notes

- Catalog schema evolution now uses `PRAGMA user_version` migrations instead of only ad hoc `ALTER TABLE` checks.
- The `files` table stores `content_hash` and indexes it.
- Scanner ingestion computes a sampled content fingerprint from file size plus the first/last 64 KB.
- `CatalogDb::listDuplicateFiles()` is available for future duplicate-browser UI work.
- Favorites, ratings, tags, and saved searches now persist in dedicated catalog tables and are wired into the results UI.
- Results browsing now supports recent/favorites/duplicates views plus saved-search recall and per-file metadata editing.
- Source roots now rebuild Windows directory watchers and queue automatic rescans when the library changes on disk.
- Results/runtime waveform reads now treat `WaveCacheBlobDb` as authoritative instead of falling back to legacy `.peak` files.

## Sprint C Progress Notes

- `.sfz`, `.dspreset`, `.multisample`, `.korgmultisample`, `.sf2`, `.talsmpl`, and `.txprog` files now scan into the catalog as index-only preset assets.
- The scanner extracts the first referenced sample when available to populate useful metadata (`sampleRate`, `channels`, `durationSec`, `codec`, `key`) for those preset files, including embedded zip samples for Bitwig and Korg multisample containers plus lightweight PCM probing for `.sf2` SoundFonts.
- Indexed preset files now persist a richer sampler summary into the catalog (`preset_name`, `zone_count`, `key_low/high`, `velocity_low/high`) and surface that summary in the results metadata row.
- Indexed sampler preset resolution is more tolerant of real-world layouts via SFZ `default_path` support and broader relative `Samples/` path fallback handling.
- Indexed sampler presets now persist blob-backed waveform peaks so the results list can still render a waveform thumbnail even before preset playback exists.

## Files Touched in Sprint A

- `Source/Pipeline/AcpPresetReader.h`
- `Source/Pipeline/AcpPresetReader.cpp`
- `Source/Pipeline/Scanner.cpp`
- `Source/App/MainComponent.cpp`
- `Source/Audio/Voice.cpp`
- `Source/Audio/Voice.h`
- `Source/Audio/VoiceManager.h`
- `Source/Audio/VoiceManager.cpp`
- `Source/UI/PreviewPanel.cpp`
- `Tests/VoiceManagerRenderTests.cpp`
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
