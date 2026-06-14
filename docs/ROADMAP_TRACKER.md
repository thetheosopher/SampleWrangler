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
- The shipped results workflow is intentionally limited to text search plus `All Files` and `Favorites`; ratings, tags, and saved searches were removed in 1.1.
- Sortable browser results are now implemented: the results panel includes a sort selector with name, date, and size modes that apply across `All Files` and `Favorites` views.
- Lightweight facet filters are now implemented in the results panel for format, channels, and loop presence, and they apply across `All Files` and `Favorites` views.
- Duplicate browser support is now implemented via a `Duplicates` results view backed by `listDuplicateFiles`, with existing root, search, sort, and facet filters applied.
- Browser workflow controls now persist and restore across relaunch: results view mode, sort mode, and facet filters are stored in app settings and reapplied on startup.
- The current sampler-engine follow-up build is clean after moving Rubber Band state and helpers out of `Source/Audio/Voice.h` into `Source/Audio/Voice.cpp` and tightening the preserve-length render path.
- `SampleWranglerVoiceManagerRenderTests` now builds successfully and gives the sampler engine an offline render harness that exercises primary playback completion, direct and preserve-length loop behavior, preserve-length loop-boundary wrapping, scrub resets, preserve-length duration behavior, source-sample-rate handling, stereo direct steady-state and fade-in channel separation, short-clip HQ fallback audibility, fresh-start HQ preserve-length playback, and deterministic live HQ-toggle deferral to the next note through the public `VoiceManager` API.
- `SampleWranglerVoiceManagerRenderTests` now also cover repeated preserve-length loop-wrap stress plus wider-channel generic fallback routing for both preserve-length and direct playback paths.
- `SampleWranglerVoiceManagerRenderTests` now also cover true `>32` source-channel routing through the generic add-sample fallback path for both preserve-length and direct playback.
- `SampleWranglerVoiceManagerBenchmark` now builds successfully in release mode and benchmarks the common mono/stereo baselines plus the widened generic fallback paths.
- The first release-mode benchmark pass shows the recent direct-path work landing where expected: direct mono/stereo render stays around `0.20` to `0.30` us per 64-sample block, widened direct fallback stays around `1.35` to `1.42` us per block, and the next clear hotspot is widened preserve-length playback at roughly `11.32` to `11.85` us per block.
- The follow-up preserve-length generic optimization now caches mixed chunk data per source channel when fanout reuse is high (for example, wide output with fewer source channels), keeps an uncached path for high-unique-source layouts, and precomputes wrapped interpolation indices/fractions per chunk to reduce repeated wrap work across channels.
- On the latest release benchmark pass (`--warmup-blocks 256 --measured-blocks 8192`), widened preserve-length fanout (`4 src -> 34 out`) improved from the original `11.32` us/block baseline down to about `3.25` us/block, while the high-unique-source preserve-length case (`34 src -> 36 out`) improved to about `10.95` us/block and remains the next measurement-driven hotspot.
- The benchmark harness now also includes 256-sample-block preserve-length wide-channel scenarios; current results (`4 src -> 34 out`: `9.99` us/block, `34 src -> 36 out`: `43.30` us/block) show similar per-frame cost separation and help track scaling behavior.
- Two fresh release benchmark sweeps with the expanded harness (including new 512-sample-block preserve scenarios) are stable and reinforce the same shape: preserve `34 src -> 36 out` remains the dominant widened hotspot while preserve `4 src -> 34 out` stays much cheaper.
- The latest high-unique preserve-length pass now groups uncached fanout by source channel, so each source mix is computed once per chunk and then fanned out to all mapped outputs.
- Midpoint values from the two latest post-grouping sweeps (`--warmup-blocks 256 --measured-blocks 8192`) are approximately: preserve `4 src -> 34 out` @64 = `3.23` us/block, preserve `34 src -> 36 out` @64 = `9.11` us/block, preserve `4 src -> 34 out` @256 = `9.70` us/block, preserve `34 src -> 36 out` @256 = `35.26` us/block, preserve `4 src -> 34 out` @512 = `19.92` us/block, preserve `34 src -> 36 out` @512 = `69.38` us/block.
- The latest interpolation-focused pass now specializes the widened preserve-length generic mixer to precompute neighbor indices and inline linear/Hermite interpolation in the per-source chunk loop, removing repeated interpolation function dispatch and neighbor-index math from the hot path.
- Midpoint values from the two latest post-interpolation sweeps (`--warmup-blocks 256 --measured-blocks 8192`) are approximately: preserve `4 src -> 34 out` @64 = `2.86` us/block, preserve `34 src -> 36 out` @64 = `3.96` us/block, preserve `4 src -> 34 out` @256 = `8.03` us/block, preserve `34 src -> 36 out` @256 = `14.35` us/block, preserve `4 src -> 34 out` @512 = `16.46` us/block, preserve `34 src -> 36 out` @512 = `29.05` us/block.
- A follow-up branch-free fast-kernel/SIMD-style micro-pass (all-valid index fast path) was tested and then reverted after two-sweep benchmarking showed regressions in widened preserve-length cases; current baseline remains the post-interpolation values above.
- A follow-up cache-locality pass that generated wrapped interpolation metadata inline during chunk production (to remove a second metadata pass) was also measured and reverted after regressing the primary widened preserve-length hotspot.
- A follow-up metadata packing pass (compact interpolation index storage using 16-bit lanes where valid for current source sizes) held or slightly improved widened preserve-length performance while preserving checksums and render-test behavior.
- Midpoint values from the latest post-packing sweeps (`--warmup-blocks 256 --measured-blocks 8192`) are approximately: preserve `4 src -> 34 out` @64 = `2.82` us/block, preserve `34 src -> 36 out` @64 = `3.92` us/block, preserve `4 src -> 34 out` @256 = `7.97` us/block, preserve `34 src -> 36 out` @256 = `14.10` us/block, preserve `4 src -> 34 out` @512 = `16.18` us/block, preserve `34 src -> 36 out` @512 = `28.22` us/block.
- A follow-up grouped fanout write-side rewrite using explicit channel lists (instead of bitmask scanning) was measured and reverted after regressing the widened preserve-length high-unique-source case.
- A low-risk Hermite neighbor-index cleanup (using unsigned packed neighbor lanes directly in the hot loop) was retained after three sweeps showed small improvements in the primary widened preserve `64/256` block scenarios and near-neutral behavior at `512` blocks.
- Median values from the latest three sweeps for widened preserve cases are approximately: `4 src -> 34 out` @64 = `2.76` us/block, `34 src -> 36 out` @64 = `3.85` us/block, `4 src -> 34 out` @256 = `7.89` us/block, `34 src -> 36 out` @256 = `13.90` us/block, `4 src -> 34 out` @512 = `16.24` us/block, `34 src -> 36 out` @512 = `28.07` us/block.
- A follow-up Hermite/linear arithmetic dedup pass (shared prepared interpolation helpers inside the hot loop) is also retained after two sweeps showed additional improvement in widened preserve playback while keeping checksums and tests stable.
- Midpoint values from the latest post-dedup sweeps (`--warmup-blocks 256 --measured-blocks 8192`) are approximately: `4 src -> 34 out` @64 = `2.71` us/block, `34 src -> 36 out` @64 = `3.86` us/block, `4 src -> 34 out` @256 = `7.59` us/block, `34 src -> 36 out` @256 = `13.86` us/block, `4 src -> 34 out` @512 = `15.68` us/block, `34 src -> 36 out` @512 = `27.85` us/block.
- A follow-up gain-weight local-staging micro-pass (hoisting `gainWeightA/B` reads into per-sample locals) was measured and reverted after two sweeps regressed widened preserve playback, most notably in the high-unique-source path.
- Midpoint values from the reverted local-staging sweeps were approximately: `4 src -> 34 out` @64 = `2.68` us/block, `34 src -> 36 out` @64 = `4.13` us/block, `4 src -> 34 out` @256 = `7.70` us/block, `34 src -> 36 out` @256 = `14.09` us/block, `4 src -> 34 out` @512 = `15.81` us/block, `34 src -> 36 out` @512 = `29.14` us/block.
- A follow-up chunk-metadata pointer-alias micro-pass (`.data()` aliases for index/fraction/gain lanes inside `fillMixedSamples`) was measured and reverted after two sweeps regressed widened preserve playback versus the retained post-dedup baseline.
- Midpoint values from the reverted pointer-alias sweeps were approximately: `4 src -> 34 out` @64 = `2.74` us/block, `34 src -> 36 out` @64 = `4.05` us/block, `4 src -> 34 out` @256 = `8.53` us/block, `34 src -> 36 out` @256 = `14.31` us/block, `4 src -> 34 out` @512 = `15.47` us/block, `34 src -> 36 out` @512 = `28.80` us/block.
- A follow-up index-validity-lane micro-pass (precomputed `idxA/idxB` valid flags reused in `fillMixedSamples`) was measured and reverted after two sweeps produced large widened preserve regressions, especially in the high-unique-source and larger block-size scenarios.
- Midpoint values from the reverted validity-lane sweeps were approximately: `4 src -> 34 out` @64 = `2.79` us/block, `34 src -> 36 out` @64 = `4.44` us/block, `4 src -> 34 out` @256 = `7.87` us/block, `34 src -> 36 out` @256 = `16.70` us/block, `4 src -> 34 out` @512 = `16.16` us/block, `34 src -> 36 out` @512 = `33.21` us/block.
- A follow-up reciprocal-blend micro-pass (replace per-sample grain-length division with multiply by precomputed reciprocal in preserve blending) was measured and reverted after two sweeps showed no stable improvement and a worse midpoint on the widened preserve high-unique-source baseline.
- Midpoint values from the reverted reciprocal-blend sweeps were approximately: `4 src -> 34 out` @64 = `2.91` us/block, `34 src -> 36 out` @64 = `3.96` us/block, `4 src -> 34 out` @256 = `7.69` us/block, `34 src -> 36 out` @256 = `14.62` us/block, `4 src -> 34 out` @512 = `15.68` us/block, `34 src -> 36 out` @512 = `28.79` us/block.
- A follow-up SIMD-shaped dataflow pass in `fillMixedSamples` (compute unweighted A/B sample lanes, then vector multiply/add with `FloatVectorOperations`) was measured and reverted after two sweeps regressed widened preserve high-unique-source scenarios.
- Midpoint values from the reverted SIMD-shaped sweeps were approximately: `4 src -> 34 out` @64 = `2.74` us/block, `34 src -> 36 out` @64 = `4.16` us/block, `4 src -> 34 out` @256 = `7.79` us/block, `34 src -> 36 out` @256 = `14.96` us/block, `4 src -> 34 out` @512 = `15.81` us/block, `34 src -> 36 out` @512 = `30.11` us/block.
- A follow-up metadata-generation reduction pass (skip Hermite-only neighbor index generation when linear interpolation is selected in generic preserve mixing) produced mixed results and was reverted to keep the widened preserve baseline stable.
- Midpoint values from the reverted linear-only metadata-generation sweeps were approximately: `4 src -> 34 out` @64 = `2.69` us/block, `34 src -> 36 out` @64 = `3.91` us/block, `4 src -> 34 out` @256 = `7.40` us/block, `34 src -> 36 out` @256 = `13.82` us/block, `4 src -> 34 out` @512 = `15.04` us/block, `34 src -> 36 out` @512 = `28.09` us/block.
- A follow-up working-set experiment lowering the generic preserve chunk size from `256` to `128` samples produced mixed outcomes and was reverted to keep the high-unique-source widened preserve baseline stable.
- Midpoint values from the reverted chunk-size-128 sweeps were approximately: `4 src -> 34 out` @64 = `2.21` us/block, `34 src -> 36 out` @64 = `3.86` us/block, `4 src -> 34 out` @256 = `7.53` us/block, `34 src -> 36 out` @256 = `14.10` us/block, `4 src -> 34 out` @512 = `15.26` us/block, `34 src -> 36 out` @512 = `28.35` us/block.
- A follow-up working-set experiment increasing the generic preserve chunk size from `256` to `512` samples was also reverted after two sweeps showed severe regressions in widened preserve fanout-heavy scenarios and no consistent high-unique-source baseline improvement.
- Midpoint values from the reverted chunk-size-512 sweeps were approximately: `4 src -> 34 out` @64 = `3.65` us/block, `34 src -> 36 out` @64 = `3.94` us/block, `4 src -> 34 out` @256 = `8.55` us/block, `34 src -> 36 out` @256 = `14.05` us/block, `4 src -> 34 out` @512 = `15.59` us/block, `34 src -> 36 out` @512 = `27.60` us/block.
- A follow-up scratch-array initialization pass (remove redundant zero-initialization for hot-path generic preserve scratch buffers that are always overwritten per produced span) is retained after three sweeps showed large fanout-heavy preserve gains and near-neutral high-unique-source behavior.
- Median values from the latest three post-initialization sweeps were approximately: `4 src -> 34 out` @64 = `1.81` us/block, `34 src -> 36 out` @64 = `3.83` us/block, `4 src -> 34 out` @256 = `6.80` us/block, `34 src -> 36 out` @256 = `13.92` us/block, `4 src -> 34 out` @512 = `13.93` us/block, `34 src -> 36 out` @512 = `28.24` us/block.
- A follow-up source-cache bookkeeping pass (stamp/epoch-based `srcCh -> slot` lookup to avoid per-chunk `fill(-1)` clears in the cached mixed-source path) is retained after two sweeps showed small additional high-unique-source improvements while preserving the fanout-heavy gains from the prior scratch-init cleanup.
- Midpoint values from the latest post-stamp sweeps (`--warmup-blocks 256 --measured-blocks 8192`) are approximately: `4 src -> 34 out` @64 = `1.83` us/block, `34 src -> 36 out` @64 = `3.81` us/block, `4 src -> 34 out` @256 = `6.81` us/block, `34 src -> 36 out` @256 = `13.90` us/block, `4 src -> 34 out` @512 = `14.05` us/block, `34 src -> 36 out` @512 = `28.16` us/block.
- A follow-up source-cache slot-assignment pass (persist `srcCh -> slot` across chunks and only stamp per-chunk data validity) was later re-validated and reverted after subsequent sweeps showed widened preserve regressions, especially in the high-unique-source path.
- Midpoint values from the reverted post-slot-assignment re-validation sweeps were approximately: `4 src -> 34 out` @64 = `2.29` us/block, `34 src -> 36 out` @64 = `4.30` us/block, `4 src -> 34 out` @256 = `6.96` us/block, `34 src -> 36 out` @256 = `15.43` us/block, `4 src -> 34 out` @512 = `14.07` us/block, `34 src -> 36 out` @512 = `30.71` us/block.
- A follow-up bookkeeping-bypass pass (skip certain source-cache/grouped-fanout setup when no fanout reuse is possible) was measured and reverted after regressing the widened preserve high-unique-source baseline.
- Midpoint values from the reverted bookkeeping-bypass sweeps were approximately: `4 src -> 34 out` @64 = `1.95` us/block, `34 src -> 36 out` @64 = `4.78` us/block, `4 src -> 34 out` @256 = `6.98` us/block, `34 src -> 36 out` @256 = `14.16` us/block, `4 src -> 34 out` @512 = `14.34` us/block, `34 src -> 36 out` @512 = `28.79` us/block.
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

- Keep the primary browse flow simple: text search, source-tree scoping, and favorites.
- Consider lightweight secondary filters for format, BPM, key, duration, channels, and loop availability only if they earn their keep.
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
  - The wider-channel generic preserve-length and direct fallback paths now hoist cached source-pointer selection out of their inner per-sample loops, trimming repeated source lookup work in the covered `>2` channel cases.
  - The wider-channel generic non-preserve direct fallback path now also mixes in chunks by caching sample positions and fade gains per chunk before vector-adding each output channel, reducing per-sample output dispatch in the scalar `>2` channel case.
  - The wider-channel generic preserve-length fallback path now also mixes in chunks by caching granular read positions and crossfade weights per chunk before vector-adding each output channel, removing the last obvious per-sample output-dispatch path in the `>2` channel preserve-length case.
  - The widened preserve-length generic fallback now also caches mixed chunk data per source channel when channel mapping fanout would otherwise recompute the same source repeatedly; this significantly reduced the `4 src -> 34 out` preserve-length case.
  - The high-unique-source uncached preserve-length branch now also groups output fanout by source channel (bitmask-based direct/tail fanout), reducing redundant per-source chunk recompute work in `>32` source layouts.
  - The high-unique-source preserve-length path now also precomputes wrapped index/fraction neighbors and uses inline linear/Hermite interpolation in the per-source chunk mixer, sharply reducing interpolation overhead in widened generic playback.
  - A later branch-free fast-path experiment for all-valid indices was reverted after measured regressions, so it is currently a known non-win for this code shape/hardware path.
  - A later in-loop metadata-generation experiment (compute wrapped indices/fractions during sample production instead of a separate pass) was also reverted after measured regressions.
  - A later metadata-packing pass (compact interpolation index lanes) is now retained and forms the current baseline for widened preserve-length playback on the measured hardware.
  - A later grouped-fanout channel-list rewrite (replacing bitmask scans with explicit channel arrays) was also reverted after measured regressions.
  - A later Hermite neighbor-index cleanup (packed unsigned neighbor lanes in the hot loop) is retained as a low-risk micro-improvement for widened preserve playback.
  - A later Hermite/linear arithmetic dedup cleanup (shared prepared interpolation helpers) is retained and currently represents the latest widened preserve baseline.
  - A later gain-weight local-staging micro-pass (per-sample `gainWeightA/B` temporaries) was reverted after measured regressions in widened preserve high-unique-source scenarios.
  - A later chunk-metadata pointer-alias micro-pass (`.data()` aliases for chunk lanes in `fillMixedSamples`) was also reverted after measured regressions.
  - A later index-validity-lane micro-pass (precomputed `idxA/idxB` valid flags) was also reverted after large widened preserve regressions.
  - A later reciprocal-blend micro-pass (precomputed grain-length inverse for preserve crossfade blend) was also reverted after midpoint regressions/noise in widened preserve baselines.
  - A later SIMD-shaped A/B lane combine pass in `fillMixedSamples` (vector multiply/add of separate A/B buffers) was also reverted after widened preserve regressions.
  - A later linear-only metadata-generation reduction pass (skip Hermite-only neighbor index writes when linear interpolation is active) also remained a non-win/mixed result and was reverted to keep baseline stability.
  - A later generic preserve chunk-size reduction (`256` -> `128`) was also reverted after mixed outcomes and slight high-unique-source regressions at larger block sizes.
  - A later generic preserve chunk-size increase (`256` -> `512`) was also reverted after severe regressions in fanout-heavy widened preserve scenarios and no stable high-unique-source gain.
  - A later scratch-array zero-init removal in the generic preserve hot path is retained; it materially improved fanout-heavy widened preserve cases while keeping high-unique-source metrics near baseline.
  - A later cached-source stamp/epoch lookup pass (replacing per-chunk cache-slot clears) is retained as a small additional improvement on widened preserve baselines.
  - A later persistent source-slot assignment pass (reuse `srcCh -> slot` across chunks with per-chunk data stamps) was re-validated and reverted after high-unique-source widened preserve regressions.
  - A later no-fanout bookkeeping-bypass pass (skip cache/grouped setup when `numOutChannels <= numSrcChannels`) was reverted after high-unique-source widened preserve regressions.
  - Benchmark scaling at 64/256/512 block sizes remains close in per-frame terms, indicating the remaining `34 src -> 36 out` cost is largely compute-bound interpolation/mixing work rather than small fixed per-block overhead.
  - When Rubber Band RT cannot produce startup output before a short clip is exhausted, the current note now degrades to the granular preserve-length path instead of failing silently.
  - A dedicated release-mode benchmark harness now exists in `Tests/VoiceManagerBenchmark.cpp`; after the latest grouped-fanout plus interpolation-specialization passes, widened preserve-length playback is much closer to widened direct playback and the remaining opportunities are likely lower-level SIMD/cache tuning rather than major control-flow reshaping.
  - Consider further SIMD/vectorized mixing for steady-state spans.
  - Focus the next measurement-driven pass on SIMD/cache tuning for the widened preserve-length generic mixer (`>32` source channels), especially around improving data locality and evaluating vectorized interpolation/mix kernels.
- If true in-flight HQ/Rubber Band switching is ever required, design it explicitly as a crossfade or note re-arm flow; the current contract is deterministic deferral to the next note.
- Extend the offline `VoiceManager` render tests further with any future true in-flight mode-switch design, more pathological loop-shape cases, and any new render-path specializations that get introduced.
- The `MainComponent` class is still large and could eventually be split into controller/state pieces.

## Suggested Next Session Order

1. Keep Sprint C paused for now and continue sampler-core work by targeting the widened preserve-length path first; the new release benchmark shows widened direct playback is already cheap enough relative to preserve-length playback.
2. Revisit the non-Rubber-Band preserve-length fallback after listening tests and profiling, especially around grain sizing, spacing, denormal safety, crossfade-weight generation, and any further fixed-phase opportunities.
3. Use the expanded offline `VoiceManager` render tests plus the new `SampleWranglerVoiceManagerBenchmark` target as the safety net for any further wider-channel or generic-path optimization work.
4. Resume Sprint C only after the sampler engine slice is stable enough to support more ambitious preview workflows.
5. Expose richer preset-specific metadata in the UI beyond the new summary row once format breadth work resumes.

## Sprint B Completion Notes

- Catalog schema evolution now uses `PRAGMA user_version` migrations instead of only ad hoc `ALTER TABLE` checks.
- The `files` table stores `content_hash` and indexes it.
- Scanner ingestion computes a sampled content fingerprint from file size plus the first/last 64 KB.
- `CatalogDb::listDuplicateFiles()` is available for future duplicate-browser UI work.
- `file_user_data` now persists favorites only; schema migration 6 removes the older ratings, tags, and saved-search tables.
- Results browsing is intentionally limited to text search plus `All Files` and `Favorites`, with per-root scoping coming from the source browser.
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
