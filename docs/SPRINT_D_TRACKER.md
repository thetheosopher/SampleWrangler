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
| Analysis substrate and persistence | Done | Everything else depends on a durable background-analysis path. | Migration 8, stale-result invalidation, and low-priority streaming feature extraction are in place. |
| Duplicate and near-duplicate discovery | Done | Immediate user value; exact duplicates already have catalog support. | Cluster-aware exact duplicate browsing and bounded, explicitly heuristic near-duplicate candidates are in place. |
| BPM, key, transient, and loop inference | Done | Extends metadata the UI already knows how to render. | Versioned worker-thread analysis keeps heuristic estimates separate from embedded metadata and exposes only current results. |
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
- Include a separate feature-vector version so changes to the persisted vector also trigger re-analysis.
- Keep the analysis job below scan priority and decode in bounded blocks with cancellation checks between blocks.

#### Slice 1 Completion Notes

- Schema migration 8 adds `analysis_results`, keyed to `files`, with source fingerprints, analysis status/failure reason, nullable future inference fields, and an analyzed timestamp.
- Feature-vector version 1 stores four little-endian float32 values in a documented order: duration seconds, RMS, peak, and zero-crossing rate.
- Analysis is scheduled on the shared worker queue at startup for stale catalog rows and after scan completion, including root-watch rescans. It does not run in the scanner or audio callback.
- BPM/key/transient/loop inference remains intentionally unset in this slice. Existing embedded BPM/key values are left untouched.
- Focused tests cover persistence, status handling, version/mtime/hash invalidation, multi-block extraction, and preserving embedded BPM/key.

### 2. Duplicate and Near-Duplicate Discovery

- Reuse the exact-duplicate foundation already present in `CatalogDb::listDuplicateFiles()`.
- Group exact duplicate rows by hash and show each cluster's size while preserving root, text, sort, and facet filtering.
- Provide `Near Duplicates` (originally labeled `Find Similar`) and `Reveal` actions for selected files.
- Require current, complete analysis with matching file modification time and content fingerprint before returning near-duplicate candidates.
- Use a bounded SQL prefilter: same channel count and loop-presence classification, sample rate within 10%, and duration within `max(0.25 seconds, 2%)`. Rank at most 500 coarse candidates using the v1 duration/RMS/peak/zero-crossing feature vector and return at most 200.
- Label the result as a feature-match heuristic, not a probability or confirmation of duplication. This is an initial discovery workflow, not semantic similarity search.

#### Slice 2 Completion Notes

- Exact duplicate rows include their global hash-cluster size and remain adjacent when sorted.
- The candidate query excludes matching non-empty content hashes, stale or unsupported analysis, index-only entries, and candidates that fail the blocking filters. Root, text, and facet filters remain available in the browser.
- Candidate scores use normalized differences across the four v1 features. They are for ranking only and do not establish that two files contain the same audio.
- Catalog tests cover cluster counts, candidate ranking and limits, exact-hash exclusion, blocking rules, stale fingerprints, and root scoping.

### 3. BPM, Key, Transient, and Loop Analysis

- Limit analysis to playable audio formats and decode them on worker threads through JUCE readers.
- Resample mono analysis input to 11 kHz and cap retained signal data at the first 180 seconds. Catalog duration and basic feature extraction still cover the full file.
- Estimate tempo from a high-pass RMS onset envelope with autocorrelation over 60-200 BPM; persist a heuristic confidence score.
- Estimate key by accumulating Goertzel energy for MIDI notes 36-83 into a chroma profile and comparing rotated profiles against major/minor templates. Low-confidence estimates are omitted.
- Count thresholded onset candidates with minimum spacing and persist onset density.
- Suggest a start-at-zero loop only when smoothed frame-RMS autocorrelation finds at least three repetitions of a 1-10 second period with correlation >= 0.92. These are suggestions only; they do not change embedded loop records or preview behavior.
- Analysis version 2 invalidates prior feature-only results. Analysis values remain in `analysis_results`; query-time hydration exposes them only when status, analysis/vector versions, mtime, and content fingerprint are current. Embedded BPM/key remain authoritative and are never overwritten.

#### Slice 3 Completion Notes

- Added deterministic synthetic WAV tests for a 120 BPM click track over a C-major chord, onset counts, a repeating two-second motif, stale-fingerprint suppression, re-analysis, and preserving embedded BPM/key.
- Current UI results show inferred BPM/key with confidence labels and display transient counts and suggested loop endpoints separately from embedded metadata.
- All four inference outputs are heuristic. The loop detector only proposes a start-at-zero period, key estimation is limited to MIDI notes 36-83, and long files use the first 180 seconds for these signal-based estimates.

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
- Add a `Find Similar` entry point on the selected file, separate from the shipped `Near Duplicates` action. Surface results with the same banner-and-exit pattern near-duplicate mode uses.
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
4. Add similarity search over persisted features and evaluate whether the v1 vector is sufficient for useful ranking.
5. Reassess whether ML-assisted classification still belongs in Sprint D or should become Sprint E/F material.

## Acceptance Checks

- Full source scans remain responsive and do not block on heavy analysis work.
- Root-watch rescans invalidate and refresh stale analysis only when needed.
- Exact duplicates are grouped and browseable through a dedicated Sprint D duplicates workflow without disturbing the default `All Files` / `Favorites` flow.
- Near-duplicate candidates are bounded, fingerprint-current, root-scopable, and visibly distinguished from confirmed exact duplicates.
- Missing BPM/key values can be inferred for playable files without overwriting embedded authoritative metadata.
- Transient counts and loop suggestions are persisted with confidence, remain distinct from embedded metadata, and are hidden when their source fingerprint is stale.
- Similarity search returns stable, explainable top results for a small curated test corpus.
- The app still builds cleanly, and focused tests cover migrations, invalidation, duplicates, BPM/key inference, and similarity ranking.

## Explicit De-Scopes

- No audio-thread analysis.
- No cloud inference or online services.
- No mandatory external runtimes beyond the existing native toolchain.
- No full-library $O(n^2)$ similarity pass without prefiltering or caching.
- No blind replacement of embedded tempo, key, or loop metadata with inferred values.
