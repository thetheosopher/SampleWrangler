# Sprint D Tracker

## Goal

Turn SampleWrangler into a practical discovery tool by adding offline audio intelligence without slowing down the existing scan, catalog, and preview loop.

## Delivery Principles

- Keep first-pass scanning cheap. `Scanner` should continue to handle file discovery, embedded/container metadata, and waveform-overview generation only.
- Run expensive DSP later on worker threads. Sprint D should build around the existing `Analyzer` concept instead of pushing more analysis into scan time.
- Preserve authoritative metadata. Embedded tempo/key data from ACID, Apple Loop, REX, `.acp`, and other container metadata should win over inferred values.
- Ship value in layers. Exact duplicates and core analysis should land before similarity ranking and any ML-assisted labeling.
- Stay local-only and native. Do not add cloud services, Python runtimes, or anything that compromises the current JUCE/CMake desktop setup.

## Proposed Scope

| Slice | Status | Why it comes first | Notes |
| --- | --- | --- | --- |
| Analysis substrate and persistence | Planned | Everything else depends on a durable background-analysis path. | Wire analysis jobs after scan completion and store derived features with versioning. |
| Duplicate and near-duplicate discovery | Planned | Immediate user value; exact duplicates already have catalog support. | Introduce a dedicated duplicates workflow without complicating the default `All Files` / `Favorites` results model. |
| BPM, key, transient, and loop inference | Planned | Extends metadata the UI already knows how to render. | Use offline analysis only for playable audio and never on the audio thread. |
| Similarity search | Planned | Needs persisted features and a query flow first. | Start with deterministic feature vectors and ranked search, not embeddings. |
| ML-assisted classification | Stretch | Highest uncertainty and weakest current foundation. | Defer until feature quality and UX prove out. |

## Implementation Approach

### 1. Analysis Substrate

- Instantiate and own an `Analyzer` beside `Scanner` in `MainComponent`, using the existing shared `JobQueue`.
- Trigger analysis after scan completion and on incremental root-watch rescans instead of adding more work to `Scanner::scanRoot`.
- Add a new schema migration for a dedicated analysis table keyed by `file_id`, with at least:
  - `analysis_version`
  - `analyzed_modified_time`
  - `analyzed_content_hash`
  - detected tempo/key values plus confidence
  - transient count / onset density
  - suggested loop start/end plus confidence or score
  - a compact persisted feature vector for similarity search
- Keep canonical user-facing metadata in `files` for existing search and rendering.
- Only backfill `files.bpm` or `files.key` from analysis when those fields are currently empty.
- Use `content_hash`, `modified_time`, and `analysis_version` to invalidate stale analysis rows cheaply after rescans.

### 2. Duplicate and Near-Duplicate Discovery

- Reuse the exact-duplicate foundation already present in `CatalogDb::listDuplicateFiles()`; Sprint D will need to add a dedicated duplicates workflow because the current results UI intentionally ships with only `All Files` and `Favorites`.
- Improve the duplicates UX before attempting near-duplicate ranking:
  - group duplicate rows by hash cluster
  - show cluster counts
  - keep root filtering and text filtering working
  - add quick compare or reveal actions if the UI cost stays low
- Add a near-duplicate prefilter instead of naive all-pairs comparison. Use blocking keys such as duration bucket, channel count, sample-rate bucket, and one-shot/loop classification to keep the search space manageable.

### 3. BPM, Key, Transient, and Loop Analysis

- Limit analysis to playable audio formats and decode them on worker threads through JUCE readers.
- Normalize decoded audio to a mono analysis stream at a fixed sample rate to keep algorithms stable and testable.
- BPM detection approach:
  - build an onset envelope
  - run autocorrelation or tempogram scoring
  - normalize obvious double/half-tempo cases
  - emit a confidence score
- Key detection approach:
  - accumulate chroma/HPCP-style energy over tonal frames
  - compare against major/minor templates
  - emit both key label and confidence
- Transient and loop analysis approach:
  - count onset candidates
  - derive onset density for one-shot vs loop heuristics
  - score candidate loop boundaries using envelope continuity and short-window similarity
- Prefer suggested loop metadata in the analysis table instead of overwriting embedded loop points.

### 4. Similarity Search

- Start with a deterministic feature vector, not ML embeddings.
- Candidate feature set for v1:
  - duration
  - loudness / RMS
  - zero-crossing rate
  - spectral centroid / spread / rolloff
  - chroma summary
  - onset density / transient count
  - loop-confidence score
- Rank candidates on a worker thread with a straightforward cosine or weighted-distance search.
- Add a simple entry point such as `Find Similar` on the selected file and surface results as a dedicated result mode or temporary query state.
- Defer approximate-nearest-neighbor indexing until brute-force ranking over persisted features proves too slow on real libraries.

### 5. ML-Assisted Classification

- Treat this as a stretch goal, not the Sprint D critical path.
- First ship rule-based labels that already fall out of the analysis feature set:
  - loop vs one-shot
  - tonal vs atonal
  - percussive vs sustained
- Only add a real classifier once there is a validated local feature set, a small evaluation corpus, and a clear UX for showing confidence and correcting bad labels.

## Likely Implementation Surfaces

- `Source/Pipeline/Analyzer.h`
- `Source/Pipeline/Analyzer.cpp`
- `Source/App/MainComponent.h`
- `Source/App/MainComponent.cpp`
- `Source/Catalog/CatalogSchema.cpp`
- `Source/Catalog/CatalogDb.h`
- `Source/Catalog/CatalogDb.cpp`
- `Source/Catalog/CatalogModels.h`
- `Source/UI/ResultsPanel.h`
- `Source/UI/ResultsPanel.cpp`
- `Tests/CatalogDbTests.cpp`
- new focused analysis tests for tempo/key/feature extraction

## Recommended Execution Order

1. Wire the analysis lifecycle and persistence layer first.
2. Upgrade the exact-duplicate browser and add cluster-aware UX.
3. Land BPM/key/transient/loop inference with synthetic test fixtures.
4. Add similarity search over persisted features.
5. Reassess whether ML-assisted classification still belongs in Sprint D or should become Sprint E/F material.

## Acceptance Checks

- Full source scans remain responsive and do not block on heavy analysis work.
- Root-watch rescans invalidate and refresh stale analysis only when needed.
- Exact duplicates are grouped and browseable through a dedicated Sprint D duplicates workflow without disturbing the default `All Files` / `Favorites` flow.
- Missing BPM/key values can be inferred for playable files without overwriting embedded authoritative metadata.
- Similarity search returns stable, explainable top results for a small curated test corpus.
- The app still builds cleanly, and focused tests cover migrations, invalidation, duplicates, BPM/key inference, and similarity ranking.

## Explicit De-Scopes

- No audio-thread analysis.
- No cloud inference or online services.
- No mandatory external runtimes beyond the existing native toolchain.
- No full-library $O(n^2)$ similarity pass without prefiltering or caching.
- No blind replacement of embedded tempo, key, or loop metadata with inferred values.
