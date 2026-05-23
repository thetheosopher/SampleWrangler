# Sprint A Tracker

## Goal

Land the first highest-value improvements from the May 2026 review without destabilizing the MVP.

## Scope

| Item | Status | Notes |
| --- | --- | --- |
| OGG scan and preview enablement | Done | Uses JUCE basic format registration; app-level gating now includes `.ogg`. |
| Audiocity `.acp` preset indexing and preview | Done | First pass supports binary or XML JUCE ValueTree presets, embedded audio preview, and external sample fallback when the referenced file resolves. |
| RT-safe preview buffer swap | Done | Buffer publication now happens on the audio-thread command boundary via staged sample-buffer slots, avoiding `atomic<shared_ptr>` in the callback. |
| Voice render-loop hoisting | Done | Standard resample playback now renders contiguous spans between loop/end boundaries instead of checking wrap conditions per sample. |
| HQ Hermite interpolation | Done | The existing HQ toggle now enables cubic Hermite interpolation for resampling and Rubber Band only when Stretch is enabled. |

## Acceptance Checks

- `.ogg` files scan, show metadata, and preview.
- `.acp` files scan and preview when embedded audio is present.
- `.acp` files remain searchable even when preview falls back to an external sample reference.
- Standard preview playback stays buildable after the slot-based sample-buffer handoff change.
- HQ mode uses cubic interpolation for non-stretch preview and the higher-quality Stretch path when enabled.
- Build `SampleWrangler` with the `vs2026-debug` preset after each completed slice.

## Notes

- `.acp` support is read-only in Sprint A.
- External-sample fallback is best-effort; a later slice should broaden recognized Audiocity path keys if needed.
- Sprint A implementation status is complete; see `docs/ROADMAP_TRACKER.md` for the cross-sprint plan and next-session handoff.
