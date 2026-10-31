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
- The shipped results workflow is intentionally focused on text search and three browse modes (`All Files`, `Favorites`, `Duplicates`); ratings, tags, and saved searches were removed in 1.1.
- Sortable browser results are implemented: name, date, size, duration, tempo, and key modes apply across all results views.
- Facet filters cover format, channels, loop presence, tempo range, and key pitch class across all results views.
- Duplicate browser support is now implemented via a `Duplicates` results view backed by `listDuplicateFiles`, with existing root, search, sort, and facet filters applied.
- Browser workflow controls now persist and restore across relaunch: results view mode, sort mode, and facet filters are stored in app settings and reapplied on startup.
- The current sampler-engine follow-up build is clean after moving Rubber Band state and helpers out of `Source/Audio/Voice.h` into `Source/Audio/Voice.cpp` and tightening the preserve-length render path.
- `SampleWranglerVoiceManagerRenderTests` now builds successfully and gives the sampler engine an offline render harness that exercises primary playback completion, direct and preserve-length loop behavior, preserve-length loop-boundary wrapping, scrub resets, preserve-length duration behavior, source-sample-rate handling, stereo direct steady-state and fade-in channel separation, short-clip HQ fallback audibility, fresh-start HQ preserve-length playback, and deterministic live HQ-toggle deferral to the next note through the public `VoiceManager` API.
- `SampleWranglerVoiceManagerRenderTests` now also cover repeated preserve-length loop-wrap stress plus wider-channel generic fallback routing for both preserve-length and direct playback paths.
- `SampleWranglerVoiceManagerRenderTests` now also cover true `>32` source-channel routing through the generic add-sample fallback path for both preserve-length and direct playback.
- `SampleWranglerVoiceManagerBenchmark` now builds successfully in release mode and benchmarks the common mono/stereo baselines plus the widened generic fallback paths.
- Sampler-core benchmark history (every kept and reverted optimization pass with its numbers) now lives in `docs/PERF_LOG.md`. The current widened preserve-length baseline is there too.
- `SampleWranglerAcpPresetReaderTests` builds successfully.
- `SampleWranglerCatalogDbTests` builds successfully and now also round-trip the indexed preset summary fields (`preset_name`, `zone_count`, `key_low/high`, `velocity_low/high`).
- `SampleWranglerScannerAppleLoopTests` builds successfully and now covers Apple Loop AIFF metadata plus indexed SFZ, Bitwig `.multisample`, Korg `.korgmultisample`, SoundFont `.sf2`, DecentSampler `.dspreset`, TAL `.talsmpl`, and TX16Wx `.txprog` preset ingestion.
- `SampleWranglerWaveformPeakTests` passes in CTest.
- CTest passes all six current tests (`CatalogDb`, `Analyzer`, `ScannerAppleLoop`, `WaveformPeak`, `AcpPresetReader`, `VoiceManagerRender`) in a few seconds.
- `JobQueue::shutdown` publishes its stop flag under the queue mutex. This fixed a lost-wakeup race that intermittently hung test executables (~40% of `ScannerAppleLoop` runs) and could hang app exit.
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

Status: Complete, with 1.1 narrowing the shipped metadata UI to favorites-only

- Introduce `PRAGMA user_version` schema migrations. Done.
- Add `content_hash` or near-duplicate hash for duplicate detection and move resilience. Storage, scanner ingestion, and duplicate-query foundation are done.
- Favorites-only user metadata is shipped. Ratings, tags, and saved searches were intentionally removed in 1.1 to keep the results workflow focused on text search plus favorites.
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
- SF2. Landed as an index-only SoundFont probe that indexes every bank/program, extracts preset summaries and representative sample metadata, and persists lightweight waveform overviews from embedded `smpl` PCM data.

Recent metadata-fidelity work:

- WAVE parsing preserves complete `smpl` loop records, treats cue points as markers rather than implicit loops, and traverses RF64 metadata.
- AIFF sustain/release loops preserve their endpoint convention separately from WAVE loop semantics.
- Root pitch, tuning, loop records, and metadata provenance are stored in format-neutral catalog fields.

Later candidates:

- EXS24 `.exs`
- Reason NN-XT `.sxt`
- Ableton `.adv` / `.adg`
- MPC `.xpm`
- Additional Audiocity-compatible preset and instrument containers

### Sprint D: Audio Intelligence and Discovery Features

Status: In progress (analysis substrate, duplicate discovery, and DSP inference complete)

Detailed execution plan: see `docs/SPRINT_D_TRACKER.md`.

Recommended landing order:

- Analysis substrate and persistence (complete)
- Duplicate and near-duplicate discovery (complete)
- BPM, key, transient, and loop inference (complete)
- Similarity search (next)
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

## Usability Pass (October 2026)

Landed after the roadmap review:

- Search accepts raw user text. Every word is prefix-matched through a sanitized FTS5 expression, so `C#`, `kick-808`, `(Live)`, and stray quotes no longer fail silently. `Favorites`, `Duplicates`, and near-duplicate views use the same word-prefix semantics in memory (`Source/Catalog/SearchText.*`).
- Search is debounced (150 ms) while typing. Return searches immediately and moves focus to the results; Esc clears the search.
- Rebuilding the results list keeps the current selection without re-triggering preview load or autoplay. Previously, refreshes (including the every-25-files refresh during scans) silently dropped the selection.
- Refreshes no longer reset the active search text after scans, source changes, or cancellation.
- "Find Similar" is renamed "Near Duplicates" so the name stays free for Slice 4 similarity search. Near-duplicate mode shows a banner naming the reference file, with a "Back to Results" button; Esc also exits.
- Empty states explain why the list is empty and offer a one-click fix: add a source, clear the search, clear filters, show all files, or leave near-duplicate mode.
- Active filters are outlined and counted on a "Clear Filters (n)" button.
- New Tempo-range and Key filters plus Duration/Tempo/Key sort modes, persisted like the existing browser controls. They use embedded metadata first, then the current analysis estimate; files without a value sort last.
- The metadata row leads with duration, tempo, key, and loop details; technical specs follow. Row tooltips show the full metadata line. Confidence values are rounded to whole percents.
- The status bar shows queued analysis jobs, and the results list refreshes once when an analysis batch finishes, so inferred tempo/key appear without user action.
- The source list has an explicit "All Sources" row, so clicking a selected source no longer secretly deselects it. Its context menu now offers Rescan, Open in Explorer, Rename, Remap, and Delete.
- Keyboard shortcuts: Ctrl+F (search), Esc (clear search / leave near-duplicates), Up/Down (move selection), Enter (play), Space (play/stop), F (toggle favorite), Ctrl+R (reveal), +/- (transpose by one semitone), 0 (reset transpose).
- Waveform thumbnails in the results list are capped at 2048 cached entries.

## Product Features Still Recommended

### Library UX

- Keep the primary browse flow simple: text search, source-tree scoping, favorites, and duplicates.
- Secondary filters now cover format, channels, loop, tempo range, and key pitch class. Sorting covers name, date, size, duration, tempo, and key. Add more only if they clearly improve browse speed.
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

- Sampler core: the direct and preserve-length render paths have been through many measured optimization passes (see `docs/PERF_LOG.md`). The remaining `34 src -> 36 out` cost is compute-bound interpolation/mixing. Further SIMD/cache tuning is low priority because >32-channel previews are rare in a sample librarian.
- If true in-flight HQ/Rubber Band switching is ever required, design it explicitly as a crossfade or note re-arm flow; the current contract is deterministic deferral to the next note.
- Extend the offline `VoiceManager` render tests further with any future true in-flight mode-switch design, more pathological loop-shape cases, and any new render-path specializations that get introduced.
- The `MainComponent` class is still large and could eventually be split into controller/state pieces.

## Suggested Next Session Order

1. Sprint E: "Open in Audiocity" handoff and Audiocity folder auto-discovery. This is the product's main differentiator and is still unstarted.
2. Sprint D Slice 4: similarity search over the versioned analysis results. Measure ranking quality on a small curated corpus and decide whether to extend the persisted feature vector. The entry point should be a new "Find Similar" action; the existing "Near Duplicates" action stays separate.
3. Results performance on large libraries: push facet filters and sorting into SQL, and move result queries off the message thread.
4. Split `MainComponent` into results/browser controllers before adding more UI state.
5. Resume Sprint C later-candidate format breadth (`.exs`, `.sxt`, `.adv`/`.adg`, `.xpm`).
6. Sampler-core SIMD/cache tuning for widened (>32 channel) preserve-length playback, only if a real workload needs it.

## Sprint B Completion Notes

- Catalog schema evolution now uses `PRAGMA user_version` migrations instead of only ad hoc `ALTER TABLE` checks.
- The `files` table stores `content_hash` and indexes it.
- Scanner ingestion computes a sampled content fingerprint from file size plus the first/last 64 KB.
- `CatalogDb::listDuplicateFiles()` now powers the shipped `Duplicates` results mode.
- `file_user_data` now persists favorites only; schema migration 6 removes the older ratings, tags, and saved-search tables.
- Results browsing now supports text search plus `All Files`, `Favorites`, and `Duplicates`, with per-root scoping, sortable results, and lightweight facet filters (format/channels/loop).
- Source roots now rebuild Windows directory watchers and queue automatic rescans when the library changes on disk.
- Results/runtime waveform reads now treat `WaveCacheBlobDb` as authoritative instead of falling back to legacy `.peak` files.

## Sprint C Progress Notes

- `.sfz`, `.dspreset`, `.multisample`, `.korgmultisample`, `.sf2`, `.talsmpl`, and `.txprog` files now scan into the catalog as index-only preset assets.
- The scanner extracts the first referenced sample when available to populate useful metadata (`sampleRate`, `channels`, `durationSec`, `codec`, `key`) for those preset files, including embedded zip samples for Bitwig and Korg multisample containers plus lightweight PCM probing for `.sf2` SoundFonts.
- Indexed preset files now persist a richer sampler summary into the catalog (`preset_name`, `zone_count`, `key_low/high`, `velocity_low/high`) and surface that summary in the results metadata row.
- Indexed sampler preset resolution is more tolerant of real-world layouts via SFZ `default_path` support and broader relative `Samples/` path fallback handling.
- Indexed sampler presets now persist blob-backed waveform peaks so the results list can still render a waveform thumbnail even before preset playback exists.
- WAVE/AIFF metadata fidelity now preserves loop semantics and provenance, including RF64 metadata traversal and distinct AIFF sustain/release endpoint conventions.
- SF2 discovery now indexes every non-terminal bank/program rather than only the first preset.

## Sprint D Progress Notes

- The versioned analysis substrate is shipped: schema migration 8 stores fingerprints, outcomes, optional future inference fields, and a versioned little-endian feature vector.
- Background analysis is scheduled at low priority on the shared worker queue after startup backfill and root-scan completion, including watcher-triggered rescans. It persists the v1 duration/RMS/peak/zero-crossing features plus version-2 BPM/key/transient/loop estimates.
- Exact duplicate browsing now keeps hash clusters together and shows each cluster size, with Reveal and Near Duplicates actions while retaining root, text, and facet filtering. The action was originally labeled "Find Similar"; it was renamed so that name stays free for Slice 4.
- Near-duplicate candidates use migration 9's prefilter index, current analysis fingerprints, bounded format-neutral metadata blocking, and a visibly labeled feature-match heuristic. They are ranked candidates, not confirmed duplicates or probabilities.
- Analysis version 2 adds worker-thread tempo, key, transient, and loop estimates without overwriting embedded BPM/key/loop metadata. Only current, fingerprint-matched results are hydrated into browsing records.
- Inference is deliberately bounded: an 11 kHz mono signal capped at 180 seconds; tempo autocorrelation over 60-200 BPM; Goertzel/chroma-template key estimation over MIDI notes 36-83; and conservative start-at-zero loop suggestions from repeated frame-RMS patterns.

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
- `Tests/VoiceManagerBenchmark.cpp`
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
