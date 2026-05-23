# Sprint A Tracker

## Goal

Land the first highest-value improvements from the May 2026 review without destabilizing the MVP.

## Scope

| Item | Status | Notes |
| --- | --- | --- |
| OGG scan and preview enablement | Done | Uses JUCE basic format registration; app-level gating now includes `.ogg`. |
| Audiocity `.acp` preset indexing and preview | Done | First pass supports binary or XML JUCE ValueTree presets, embedded audio preview, and external sample fallback when the referenced file resolves. |
| RT-safe preview buffer swap | Planned | Replace the current `atomic<shared_ptr>` handoff with a published slot/ring approach. |
| Voice render-loop hoisting | Planned | Split render work into contiguous spans and remove per-sample wrap branches. |
| HQ Hermite interpolation | Planned | Upgrade the HQ path from linear to cubic interpolation. |

## Acceptance Checks

- `.ogg` files scan, show metadata, and preview.
- `.acp` files scan and preview when embedded audio is present.
- `.acp` files remain searchable even when preview falls back to an external sample reference.
- Build `SampleWrangler` with the `vs2026-debug` preset after each completed slice.

## Notes

- `.acp` support is read-only in Sprint A.
- External-sample fallback is best-effort; a later slice should broaden recognized Audiocity path keys if needed.
- Sampler-core performance refactors stay in Sprint A, but after the format bridge lands cleanly.
