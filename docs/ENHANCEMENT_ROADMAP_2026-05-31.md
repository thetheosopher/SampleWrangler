Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: SampleWrangler (Windows JUCE audio sample librarian)

# SampleWrangler — Enhancement Roadmap

Companion to `CODE_REVIEW_2026-05-31.md`. This is the actionable handoff document.

## Summary Table

| # | Item | Category | Perspective | Effort | Impact | Area |
|---|------|----------|-------------|--------|--------|------|
| QW1 | Targeted analysis query (stop fetching whole catalog) | Performance | Engineering | S | High | Pipeline/Catalog |
| QW2 | Log swallowed setting-parse failures | Reliability | Engineering | XS | Medium | App |
| QW3 | Log skipped/un-analyzable files | Observability | Engineering | XS | Medium | Pipeline |
| QW4 | Root-path cache for jobs | Performance | Engineering | S | Medium | Pipeline/Catalog |
| QW5 | Batch scanner upserts in transactions | Performance | Engineering | S | Medium | Pipeline/Catalog |
| MP1 | Sortable results (name/duration/BPM/key/date) | Feature/UX | Both | M | High | UI/Catalog |
| MP2 | Lightweight facet filters | Feature/UX | Both | M | High | UI/Catalog |
| MP3 | Duplicate browser (uses existing data) | Feature | Both | M | High | UI/Catalog |
| MP4 | "Recently Added" view | Feature | Product | S | Medium | UI/Catalog |
| MP5 | Keyboard shortcuts + empty/loading states | UX | Both | M | Medium | UI |
| SI1 | Sprint D analysis substrate + BPM/key inference | Feature | Both | L | High | Pipeline/Catalog/UI |
| SI2 | Similarity / near-duplicate search | Feature | Both | XL | High | Pipeline/Catalog/UI |
| SI3 | Audiocity "Open in" bridge (Sprint E) | Feature | Product | M | High | App/UI |

---

## 1. Quick Wins (< 1 day each, high leverage)

### QW1 — Targeted analysis query *(implemented in this change)*

- **Category:** Performance · **Effort:** S · **Impact:** High
- **Area:** [Source/Pipeline/Analyzer.cpp](../Source/Pipeline/Analyzer.cpp#L71),
  [Source/Catalog/CatalogDb.cpp](../Source/Catalog/CatalogDb.cpp#L420)
- **Problem:** `analyzeRoot` fetched up to 1,000,000 full `FileRecord`s and filtered in
  memory.
- **Fix:** Added `CatalogDb::listFileIdsNeedingAnalysisByRoot(rootId)` that pushes the
  `root_id = ? AND index_only = 0 AND duration_sec IS NULL` predicate into SQL and returns
  only ids. `analyzeRoot` now iterates that result directly.

### QW2 — Log swallowed setting-parse failures

- **Category:** Reliability · **Effort:** XS · **Impact:** Medium
- **Area:** [Source/App/MainComponent.cpp](../Source/App/MainComponent.cpp)
- **Fix:** Replace empty `catch` blocks around `std::stod` of persisted settings with a
  `SW_LOG_WARN` naming the setting key and raw value.

### QW3 — Log un-analyzable / skipped files

- **Category:** Observability · **Effort:** XS · **Impact:** Medium
- **Area:** [Source/Pipeline/Analyzer.cpp](../Source/Pipeline/Analyzer.cpp#L43)
- **Fix:** Emit a rate-limited debug log when `createReaderFor` returns null so missing
  metadata is explainable.

### QW4 — Root-path cache for jobs

- **Category:** Performance · **Effort:** S · **Impact:** Medium
- **Area:** Analyzer + WaveformCache job bodies.
- **Fix:** Resolve a `rootId -> path` map once per job batch (or cache + invalidate on root
  mutation) instead of `allRoots()` + `find_if` per job.

### QW5 — Batch scanner upserts in transactions

- **Category:** Performance · **Effort:** S · **Impact:** Medium
- **Area:** [Source/Pipeline/Scanner.cpp](../Source/Pipeline/Scanner.cpp)
- **Fix:** Wrap N upserts in `beginTransaction`/`commitTransaction` to cut mutex churn and
  fsync pressure during bulk scans.

---

## 2. High-Impact Medium Projects (1–2 weeks each)

### MP1 — Sortable results

- **Perspective:** Both · **Effort:** M · **Impact:** High
- Add clickable column-style sorting (name, duration, BPM, key, date added, size). Push
  `ORDER BY` into the catalog queries; persist the last sort in `app_settings`.

### MP2 — Lightweight facet filters

- **Perspective:** Both · **Effort:** M · **Impact:** High
- Toggle chips for format group (audio vs preset), playable vs index-only, has-loop, and
  BPM/key presence. All filterable from existing columns; compose with FTS `MATCH`.

### MP3 — Duplicate browser

- **Perspective:** Both · **Effort:** M · **Impact:** High
- Surface `listDuplicateFiles()` in the UI: group by `content_hash`, show cluster size,
  add reveal/compare/keep-one actions. Data + query already exist.

### MP4 — "Recently Added" view

- **Perspective:** Product · **Effort:** S · **Impact:** Medium
- New view mode sorted by `modified_time` desc, capped (e.g., last 200).

### MP5 — Keyboard shortcuts + empty/loading states

- **Perspective:** Both · **Effort:** M · **Impact:** Medium
- Ctrl+F focus search, F toggle favorite, arrow-key result navigation, +/- pitch nudge;
  placeholder text for empty results and an active-scan indicator.

---

## 3. Strategic Initiatives (multi-week)

### SI1 — Sprint D analysis substrate + BPM/key inference

- **Problem:** BPM/key are display-only; no inference for raw audio.
- **Approach:** Per `docs/SPRINT_D_TRACKER.md` — add an analysis table, run offline
  worker-thread feature extraction, persist results, surface in UI (and enable MP1/MP2
  sorting/filtering on inferred values).
- **Risks:** Detection accuracy; CPU budget on large libraries. **Metrics:** % files with
  confident BPM/key; analysis throughput (files/min); user override rate.

### SI2 — Similarity / near-duplicate search ("find sounds like this")

- **Approach:** Spectral/statistical feature vectors persisted alongside analysis; nearest
  -neighbor ranking. Builds on SI1. **Risks:** Index size, ranking quality.
  **Metrics:** retrieval precision on a labeled set; query latency.

### SI3 — Audiocity bridge (Sprint E)

- **Approach:** Auto-discover Audiocity install/library folders; add "Open in Audiocity"
  context action and clean drag-to-DAW export. **Risks:** Path discovery robustness across
  installs. **Metrics:** handoff success rate; adoption of the action.

---

## 4. Debt Retirement Candidates

- **Dual waveform cache paths.** `Source/Pipeline/WaveformCache.cpp` (file-based `.peak` +
  `wave_cache` table) coexists with `Source/Catalog/WaveCacheBlobDb.cpp` (blob-based, the
  documented runtime read path). Confirm which is authoritative and retire/clearly demote
  the redundant one to remove a class of silent-inconsistency bugs.
- **Repeated `allRoots()` scans** (folded into QW4).

## 5. Dependency Upgrade Path

Dependencies are current and purposeful. No upgrades required this cycle. When touched,
prefer (low→high risk): SQLite amalgamation refresh → Rubber Band point release → JUCE
minor (re-run full CTest after each).

---

## Prompt Handoff

> Implement **MP1 (sortable results)** in SampleWrangler. Add an `ORDER BY` clause and a
> `SortKey { Name, Duration, Bpm, Key, DateAdded, Size }` + direction parameter to the
> relevant `CatalogDb` query methods (`searchFiles`, `listRecentFiles`,
> `searchFilesByRoot`, `listRecentFilesByRoot`) in
> `Source/Catalog/CatalogDb.cpp`/`.h`. Add a sort selector to
> `Source/UI/ResultsPanel.{h,cpp}` (~current `applySort`), persist the choice in
> `app_settings` via `CatalogDb::setAppSetting`, and wire it through
> `Source/App/MainComponent.cpp` result refresh. Keep changes compilable; run the five
> CTest targets after.

> Implement **MP3 (duplicate browser)**: add a `Duplicates` view mode to
> `Source/UI/ResultsPanel.{h,cpp}` that calls `CatalogDb::listDuplicateFiles()`
> (`Source/Catalog/CatalogDb.cpp#L570`), groups rows by `content_hash`, and renders cluster
> headers with counts plus reveal/compare actions. No schema change needed.
