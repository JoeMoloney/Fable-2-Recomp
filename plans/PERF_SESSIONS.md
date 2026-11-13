# Desktop performance — multi-session plan (START HERE)

Started 2026-11-13. Supersedes the older Steam Deck / offline perf handoffs as the
*active* plan. Those stay as evidence; don't re-read them unless a step below says so.

**Goal:** steady 60 FPS on the desktop (i9-10900K, RTX 3090, CachyOS), then the
Steam Deck. Long-term: replace hot recompiled code with readable C++ that we write
ourselves (the Ship of Theseus approach; later Rust).

**Working style (keeps usage down):** one focused unit per session. The user drives
the game and runs A/B launches; Claude reads numbers, edits code and rebuilds.
Each session updates the status table below and nothing else.

## Session hand-off rule

At the end of every session Claude updates the status table and replaces the
**Next session prompt** below. The user pastes it into a fresh session.

### Next session prompt

```
Continue Fable II desktop performance work. Read ONLY plans/PERF_SESSIONS.md
(don't re-read older handoffs unless it says so). Do session unit 3: cap the
presenter so it presents only when there is a new guest frame and/or at display
refresh (FIFO/MAILBOX) instead of ~3,900 FPS. Start in the presenter.cpp WIN32-only
UI-tick/vsync gates (~1594/~1630) and the swapchain present-mode choice. Rebuild
only what's needed, then tell me when to launch tools/perf/run-profile.sh so you
can run tools/perf/capture.sh and compare with out/perf/unit2-blocking-wait-203557
(guest F3 FPS there was 16). Keep it focused, explain the change simply (I'm a
Java dev). At the end: update the status table + this prompt, and store the
findings/lessons in the memory MCP (Fable II = projectId 3; reusable lessons GLOBAL).
```

## How to run things

| Task | Command |
|---|---|
| Full Release build (~5 min, first time) | `bash build-release-linux_amd64.sh` (edit `-j4` to `-j12` locally if wanted) |
| Rebuild after an SDK runtime/GPU change only | `cmake --build out/build/linux-amd64-release --target fable_2 -j12` |
| Launch with MangoHud overlay | `tools/perf/run-profile.sh [extra --flags]` |
| 20 s CPU profile of the running game | `tools/perf/capture.sh <label>` → `out/perf/<label>-HHMMSS/` |
| Read a profile | `perf report -i <dir>/perf.data --no-children -g none --sort comm,dso,sym --stdio` |

- Test save: copied from `../Fable-2-Recomp-main/out/preserved-build-data-20261107/tested-tmp-release/saves`
  into `out/build/linux-amd64-release/saves` (frame drops are bad at that location).
  A full rebuild with `cmake --fresh` keeps `saves/`, but check after any `rm` of the build dir.
- `generated/` backup: `out/backup/generated-20261113.tar` (codegen reported "0 written", so it was unchanged).
- perf needs `--user-callchains` (perf_event_paranoid=2). LBR call graphs work.
- **MangoHud's FPS is the host presenter (~3,900 FPS), not the game.** For game FPS use the
  in-game F3 guest meter until finding #2 is fixed.

## Baseline findings (2026-11-13, `out/perf/20261113-baseline/`)

20 s at the bad-FPS save location, 188k samples. **No game thread is CPU-saturated**
(GameThread ~0.6 core, 3D Engine ~0.55 core, GPU Commands ~0.7 core). The game is limited
by **waiting and synchronization, not by slow recompiled code.** In order:

1. **`WaitMultiple` is a polling loop** — `thirdparty/rexglue-sdk/src/core/threading_posix.cpp:323`.
   Multi-handle waits (`WaitForMultipleObjects`) trylock-spin plus `sleep_for(1ms)` instead of
   blocking on a condition variable. Effects: up to ~1 ms+ extra wake-up latency on every
   multi-object wait, and the **Audio Worker burns ~21% of all samples (~1 full core)** in
   `pthread_mutex_trylock`/`clock_gettime`. When the remaining time is under 1 ms,
   `duration_cast<ms>` gives 0, so `sleep_for(0)` becomes a pure busy spin.
2. **Presenter runs uncapped at ~3,900 FPS** (main `fable_2` thread ~62% of a core, plus GPU
   and driver contention with the GPU Commands thread). The earlier Deck audit noted that the
   UI-tick and vsync gates in `presenter.cpp` (~1594/~1630) are WIN32-only, and that the swapchain
   uses IMMEDIATE mode. It should present at most once per new guest frame / display refresh.
3. **Timer thread** `rex::thread::TimerQueue::TimerThreadMain` ~4% (probably also a poll loop; check alongside #1).
4. **GPU sync stalls** (from the Deck work): memexport readback (~37 ms per frame on the Deck)
   and occlusion queries that drain the GPU. Both are hot-reload cvars, so they can be A/B tested
   without rebuilding: `--readback_memexport=false`, `--occlusion_query_enable=false`.
   Pass each flag only **once** (CLI11 quirk).
5. Game frame loop `ProcessGameFrame_82276C30` (hotfunc) spins `Yield_82CBD098` while waiting
   for the frame budget. This is the original game's own behaviour; only revisit after 1–4.
6. Hot guest functions (Theseus candidates, small for now): `ProcessAndProcessAndProcess504_821FE378`,
   `InterpolateFloatArray_82C62548`, `ProcessAndProcessAndProcess743_82C52F60`, `FindChainValue_82C469F0`.

## Session queue

| # | Unit | Who does what | Status |
|---|---|---|---|
| 1 | Build, save, profiling setup, baseline profile | done | ✅ 2026-11-13 |
| 1b | **Flag A/B (no Claude needed):** run `tools/perf/run-profile.sh` 3×: no flags / `--readback_memexport=false` / `--occlusion_query_enable=false`. Same spot, note the F3 guest FPS and any visual breakage. Write the results in the table below. | user | ⏳ |
| 2 | Replace polling `WaitMultiple` (and the timer queue if it's the same pattern) with a blocking implementation: a shared waiter condvar/futex registered on each handle. Unit-test wait-any/wait-all/timeout, rebuild, re-profile. | Claude + user test | ✅ 2026-11-13 — see "Unit 2 results" |
| 2b | (small, optional) Alertable waits / `AlertableSleep` still wake every 1 ms (`kAlertablePollSlice`) to check for queued APCs. Make `QueueUserCallback` wake the thread's waiter instead. Only do this if a profile shows it. | Claude | ⏳ |
| 3 | Cap the presenter: present only when there's a new guest frame and/or at the display refresh (FIFO/MAILBOX). Re-measure with MangoHud, which then becomes meaningful. | Claude + user test | ⏳ |
| 4 | Act on 1b: fix the GPU sync stall properly (async/double-buffered memexport readback, or keep the data GPU-side; batch occlusion query results). | Claude + user test | ⏳ |
| 5 | Pipeline-creation hitches: persistent `VkPipelineCache` + async/background pipeline compile. | Claude + user test | ⏳ |
| 6+ | Theseus: rewrite the top guest functions from the latest profile as readable C++ hotfuncs, verified against the originals. | together | ⏳ |
| later | Port fixes 2–5 to the Steam Deck build and measure there. | | ⏳ |

## Unit 2 results (2026-11-13, `out/perf/unit2-blocking-wait-203557/`)

SDK changes (submodule `thirdparty/rexglue-sdk`): `src/core/threading_posix.cpp` (blocking
`WaitMultiple`: each waiting thread puts a small waiter, a condvar plus a generation counter, on every handle;
signaling a handle wakes it) and `src/core/timer_queue.cpp` (local condvar `BlockingWaitStrategy`;
the vendored disruptorplus one has swapped `wait_until` args and doesn't compile). Test:
`tests/unit/core/threading_wait_test.cpp` (6 cases, passes; Catch2 submodule isn't checked out, so it was run with a
stand-in macro header).

| Thread / symbol | Baseline | After |
|---|---|---|
| Audio Worker (spin in trylock/clock_gettime/WaitMultiple) | ~1.0 core (~18% of samples) | 0.03 core |
| `TimerQueue::TimerThreadMain` | 4.35% of samples | 0.02% |
| Total samples in 20 s | 188k | 152k |
| Guest F3 FPS at test spot | not recorded | 16 |

FPS is still low and no thread is saturated: 3D Engine ~0.9 cores, GameThread ~0.8, GPU Commands
~0.6, all mostly in `sched_yield` / `FrameLimiterWait_82242628` / `GpuProgressCheck_82B9BF90`.
So it's still waiting on sync, most likely GPU-side (finding #4, unit 1b) and the presenter (#2).
**Unit 1b is now the most useful thing to run** (no rebuild needed).

## A/B results (fill in)

| Date | Flags | Guest FPS (F3) at test spot | Visual issues |
|---|---|---|---|
| 2026-11-13 | (none, after unit 2) | 16 | |
| | `--readback_memexport=false` | | |
| | `--occlusion_query_enable=false` | | |
