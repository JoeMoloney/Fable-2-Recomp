# Plans

Multi-session work plans. Each plan says at the top whether it is active or done.

## Active

| Plan | What it is |
|---|---|
| [STEAMDECK_SESSIONS.md](STEAMDECK_SESSIONS.md) | Steam Deck performance. 30 FPS at the test spot since 2026-11-15; the busy view (~28) is next. Start at its "Next session prompt". |
| [THESEUS_PILOT.md](THESEUS_PILOT.md) | Ship of Theseus pilot: DONE 2026-11-16 (20/20 rewrites pass, 19 run in game). Verdict: batch job for leaf functions. |
| [THESEUS_BATCH.md](THESEUS_BATCH.md) | Theseus batch job over the 4,766-function leaf pool: queue, packets, mechanical mutants, gate, ship in batches. B0 done; next B1 (`mutate.py`). |
| [RE_NAMING_PASS.md](RE_NAMING_PASS.md) | Name and place functions from evidence in the binary (profiler strings, assert paths, RTTI vtables). Reviewed names recorded (`docs/re/NAMES_EVIDENCE.tsv`) and 3,513 applied in Ghidra (N3b); next is N5 (LLM-assisted naming). |

## Done ([done/](done/))

| Plan | Outcome |
|---|---|
| [PERF_SESSIONS.md](done/PERF_SESSIONS.md) | Desktop performance: solid 30 FPS with guest vblank pacing on (2026-11-14). Has the follow-up ideas (unit 5: pipeline cache). |
| [ai-remote-input-control.md](done/ai-remote-input-control.md) | Remote-control server and `tools/fable2_control.py`, used to drive the game without a human at the controls. |
| [av-clean-builds.md](done/av-clean-builds.md) | Windows builds that don't trip antivirus without a code-signing certificate (`tools/repro/`). |
| [disable-motion-blur.md](done/disable-motion-blur.md) | `[patches] disable_motion_blur` option. |
| [f5-guest-null-av.md](done/f5-guest-null-av.md) | Fix for the intermittent null access violation on F5 (Lua runner). |
| [hero-dog-texture-readback.md](done/hero-dog-texture-readback.md) | First fix for the black hero/dog: targeted readback (now a fallback). |
| [hero-dog-shifty-texture-mipmap.md](done/hero-dog-shifty-texture-mipmap.md) | Investigation log for the "shifty" dog texture; replaced by realtime morphing. |
| [hero-dog-realtime-texture-morphing.md](done/hero-dog-realtime-texture-morphing.md) | Current black hero/dog fix: realtime texture morphing on the GPU (default on). |
| [main-menu-mod-item.md](done/main-menu-mod-item.md) | Extra main-menu item that runs our own function (demo passed). |
| [plan-direct-mouse-1to1.md](done/plan-direct-mouse-1to1.md) | 1:1 mouse camera through right-stick injection. |
| [process-game-frame-hand-tune.md](done/process-game-frame-hand-tune.md) | `ProcessGameFrame` rewritten as readable C++ with no gotos. |

## Future / paused ([future/](future/))

| Folder | What it is |
|---|---|
| [native-renderer/](future/native-renderer/) | Research (2026-10-26) into a Fable-specific native renderer like Skate 3 / Unleashed Recomp. Start with `NATIVE_RENDERER_FEASIBILITY.md`. Paused once the existing renderer reached 30 FPS. Some of its rules (local models, Deck diagnostics) are out of date. |

## Conventions

- New plans go in `plans/`. When a plan finishes, mark it `STATUS: DONE`, move it with
  `git mv` into `done/`, update anything that links to it (`git grep <file name>`) and
  update this index.
- Results of finished investigations that aren't plans go in `docs/analysis/`.
- Summary of the October Deck and November offline investigations (their original
  files were deleted): [docs/analysis/PERF_INVESTIGATIONS_OCT_NOV_2026.md](../docs/analysis/PERF_INVESTIGATIONS_OCT_NOV_2026.md).
