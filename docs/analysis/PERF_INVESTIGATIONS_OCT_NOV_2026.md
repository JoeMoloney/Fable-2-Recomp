# Earlier performance investigations (Oct 24 – Nov 10, 2026) — summary

Written 2026-11-15. This file replaces 20 plan files that were deleted in the repo
cleanup: the October Steam Deck investigation (`plans/STEAMDECK_PERF_HANDOFF.md`,
`STEAMDECK_PERF_NEXT_SESSION.md`, `STEAMDECK_PERF_RESUME_AUDIT.md` and 16
`plans/steamdeck-*.md` notes) and `plans/OFFLINE_PERFORMANCE_NEXT_SESSION.md`.
They were never committed, so this summary is the only record. It keeps
**what was learned**. It drops the run-by-run hashes, Deck rollback steps and
staging checklists, which no longer apply.

The current work lives in `plans/STEAMDECK_SESSIONS.md` (Deck, 30 FPS since
2026-11-14) and `plans/PERF_SESSIONS.md` (desktop, done).

---

## 1. Steam Deck investigation, Oct 24–26

### Starting point
- The Deck ran the game at **~7.5–8.6 guest FPS** in Gaming Mode. Steam's HUD showed
  300–500 FPS: those were **host presents, not guest frames**. Always read the F3
  "Guest" line. Vulkan used IMMEDIATE present mode; presenter pacing made Steam's
  number match the guest rate and cut UI-thread CPU, but did not raise the guest FPS.
- Third-party Vulkan layers (LSFG frame generation, Steam overlay, Fossilize) were
  loaded and spoiled the baseline. Disable LSFG per launch with `DISABLE_LSFG=1` /
  `DISABLE_LSFGVK=1`; don't edit global manifests.
- The Deck build runs **inside the `fable2-build` Distrobox** (host glibc is too old),
  built with at most `-j2` on the Deck.

### What made a difference
| Change | Result | Where it lives now |
|---|---|---|
| Linux `QueryProtect` via the `PROCMAP_QUERY` ioctl instead of parsing `/proc/self/maps` | Sampled query 1916 µs → **16.6 µs**; guest 8.6 → **10.25 Hz** (sequential runs, not a matched A/B). Also made the old global-lock wait small (70% → 13% of sampled request time) | SDK commit `aca9333`, `src/core/memory_posix.cpp` |

### What was ruled out or didn't help (don't repeat)
- **Fiber switching / `rt_sigprocmask`:** `swapcontext` makes a syscall on each switch,
  but `perf stat` counted **zero** mask syscalls on the 3D Engine, GameThread and GPU
  Commands threads. Not a cost in that scene.
- **Yield spinning:** ~370k `sched_yield`/s on 3D Engine (`FrameLimiterWait_82242628`)
  and ~260k/s on GameThread. A 50 µs sleep cut CPU (96% → 71%) but **not FPS** at the
  time. (In November, D1/D2 used 200 µs sleeps and did help, because the freed power
  budget went to the GPU once the GPU was the limit.)
- **Write-watch / `mprotect` churn:** thousands/s, and both the arm and unwatch sides
  already batch. No evidence it was on the critical path.
- **Readback buffer allocation/cleanup:** ~0.27 ms/frame. Caching allocations was
  not a real win; the cost was the fence wait.

### The memexport readback stall (the main October lead)
- Vulkan timing showed **two full memexport readback fallbacks per frame**, costing
  **~35.8 ms/frame in `vkWaitForFences`**. All were `fallback_previous_open`: the
  fast path's previous slot was still in the open submission, so the full path ended
  the submission and waited for **all queued GPU work**. The CPU was waiting for
  queued GPU work, not for one slow shader.
- Two ranges, same shader pair (**VS `695413a9831d88da`, PS `a4a965c189287b99`**):
  384 B (dominant, ~37 ms wait) and 256 B (~1 ms). Physical addresses change per
  launch. Trace bases were in dwords (`<<2` for bytes).
- **Who sets it up:** guest `FUN_8222EA68` calls the logical-constant setter
  `FUN_82210418` with ID 313 (`0x139`) at `0x8222ED88`, which resolves to shader
  constant slot 8 (stream `c8`). A 10 s paired capture joined 97/98 and 97/97
  readbacks to this caller. The function loops over a dynamic entry array, writes
  one 0x400-byte stride per entry, and sets a per-entry visibility flag.
- **What the shader does:** offline disassembly shows the VS fetching 48-byte
  records, running dot-product/compare/predicate sequences and exporting via eM0;
  the PS is trivial. **Best guess: a visibility/culling classifier** (not proven).
- **Who reads the output:** a GPU draw. Adaptive quad-patch tessellation reads both
  ranges directly from the shared GPU buffer by DMA (consumer VS `5003700b7c9b1c16`,
  PS `7ddad5c29b3f56ee`). **No guest CPU reader was found**, but the probe could not
  prove that none exists (its capture also overflowed after ~2 s).
- **Outcome:** November unit D3 added the SDK cvar `readback_memexport_fast_no_stall`,
  seeded **true** for Fable (18 → 25 FPS on the Deck). It relies on the finding above
  that the output's consumer is a GPU draw. **Open risk:** if some guest CPU code
  does read this output, it would now see older data. No visual problems have been
  seen. If odd culling/visibility bugs ever show up, check this first.
- r14/r15 (Nov 15) then seeded `readback_memexport_keep_frames=1800` to stop the
  readback buffers being freed and recreated on camera turns.

### Smaller findings still worth knowing
- **`tools/linux.py run` boolean override bug:** the helper puts `--fullscreen=false
  --frame_limit=30` before any extra args. With CLI11, `--fullscreen=false
  --fullscreen=true` ends up **false**. Pass each option once (as `tools/deck/flags.sh`
  does) or fix the helper. Still present at `tools/linux.py:153`.
- `FABLE2_PROFILER_FRIENDLY` is a no-op; `perf_log_csv` is not wired to its setter;
  Tracy is never started in Release. Use `perf` (DWARF unwinding was poor on the
  GPU thread) or the custom `--gpu_timing_stats` / `--readback_memexport_stats`.
- The Linux UI tick wait and vsync present gate in the SDK presenter are Win32-only.
- `pace_to_guest_vblank` actually gates on the guest-output refresh count.
- A Gaming Mode run logged 322 pipeline creations, most on the command-processor
  thread: first-encounter stalls are possible with async shader compilation off.
- The F3 Guest counter counts consumed swap packets, not completed GPU frames.
- Earlier user observations, none investigated: an intermittent white screen
  before the splash screens, the main menu sitting at exactly 30, and one
  Deck freeze during a Steam update.

### Local model trial (Bonsai)
`ollama/bonsai-2:27b` was tried as a read-only reviewer. It was useful for
finding things in the source and as a checklist, but its first design proposal
needed four corrections, and it saved no measurable context. Don't trust it
with correctness-critical work unless someone checks it.

---

## 2. Offline CPU investigation, Nov 10

Detailed reports in this folder: `OFFLINE_SUBSYSTEM_AUDIT.md`,
`LOCALIZATION_ACTUAL_CODEGEN_RESULTS.md`, `ARITHMETIC_AND_NATIVE_MEMORY_RESULTS.md`,
`COPY_CALLER_AND_WRITE_WATCH_AUDIT.md`. Test harnesses: `tools/repro/{localization,
arithmetic,native_memory}/`, plus `tools/repro/audit_*.py`.

- **Ignored codegen options:** the eight register-localization options in
  `fable_2_manifest.toml` (`skip_lr` … `non_volatile_as_local`, under "Conservative
  settings") sit **inside the preceding `[[entrypoint.midasm_hook]]` table**, so the
  recompiler ignores them and uses its defaults (all false). The generated code has
  zero local GPR/CR declarations. The comment says the opposite of what the values
  would do. Moving them under `[entrypoint]` would change all codegen; don't do that
  casually.
- Turning on `non_argument_as_local` for leaf functions passed correctness checks but
  gave **no consistent timing win**.
- Blanket AVX/FMA **failed** a strict NaN-sign equivalence check, so it's not safe.
- A native RAM copy primitive was 8.7–56× faster in a hot-buffer microbenchmark
  (not a full-game result). The best lead is an overlap-safe, aligned word-array
  move for guest helper `0x82CA30E8` (caller `0x82DF81A0`), **not** a global memcpy
  swap. Not implemented; the proposed register post-state is untested.
- The desktop and Deck then reached 30 FPS through GPU-side work (Nov 13–15), so
  this line was never resumed.

Memory-service records: project 3, entries 481–487.
