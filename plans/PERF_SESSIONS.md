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
(don't re-read older handoffs unless it says so). Unit 4 found the limiter: the 3D Engine
thread (MainRenderLoop_82B9CD68) is saturated, and ~31% of it is write-watch page faults
(~550 SIGSEGV/s). On Linux each fault calls memory::QueryProtect, which re-reads and
sscanf-parses /proc/self/maps (2164 lines, ~0.5 ms) while holding the global critical
region lock (thirdparty/rexglue-sdk/src/system/mmio_handler.cpp:411,
src/core/memory_posix.cpp:174). Do unit 4b: make that fault path cheap. Read the
QueryProtect callers and the access_violation_callback (PhysicalHeap::TriggerCallbacks)
first, then replace the /proc/self/maps lookup with our own protection tracking (or prove
the recheck can use the heap's page table). Keep behaviour identical; unit-test the
lookup; rebuild; then tell me when to launch tools/perf/run-profile.sh and run
`sudo tools/perf/waitprof.sh unit4b`. Compare against out/perf/unit4-210222 (18 FPS).
Keep it focused, explain simply (I'm a Java dev). At the end: update the status
table + this prompt, and store findings/lessons in the memory MCP (Fable II =
projectId 3; reusable lessons GLOBAL).
```

## How to run things

| Task | Command |
|---|---|
| Full Release build (~5 min, first time) | `bash build-release-linux_amd64.sh` (edit `-j4` to `-j12` locally if wanted) |
| Rebuild after an SDK runtime/GPU change only | `cmake --build out/build/linux-amd64-release --target fable_2 -j12` |
| Launch with MangoHud overlay | `tools/perf/run-profile.sh [extra --flags]` |
| 20 s CPU profile of the running game | `tools/perf/capture.sh <label>` → `out/perf/<label>-HHMMSS/` |
| Wait profile (per-thread user/kernel CPU, R/S state, ctx switches; with sudo also perf sched, SIGSEGV rate, kernel-inclusive profile) | `sudo tools/perf/waitprof.sh <label>` → `<dir>/waitprof.txt`, `signals.txt`, `perf-kernel.data` |
| Read a profile | `perf report -i <dir>/perf.data --no-children -g none --sort comm,dso,sym --stdio` |

- Test save: copied from `../Fable-2-Recomp-main/out/preserved-build-data-20261107/tested-tmp-release/saves`
  into `out/build/linux-amd64-release/saves` (frame drops are bad at that location).
  A full rebuild with `cmake --fresh` keeps `saves/`, but check after any `rm` of the build dir.
- `generated/` backup: `out/backup/generated-20261113.tar` (codegen reported "0 written", so it was unchanged).
- perf needs `--user-callchains` (perf_event_paranoid=2). LBR call graphs work.
- MangoHud's FPS is the host present rate. Since unit 3 that's capped (~60 with an ImGui
  overlay open), but it's still not the game's frame rate. Use the in-game F3 guest meter for that.

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
| 1b | ✅ 2026-11-13 (see A/B table: no real gain). **Flag A/B (no Claude needed):** run `tools/perf/run-profile.sh` 3×: no flags / `--readback_memexport=false` / `--occlusion_query_enable=false`. Same spot, note the F3 guest FPS and any visual breakage. Write the results in the table below. | user | ⏳ |
| 2 | Replace polling `WaitMultiple` (and the timer queue if it's the same pattern) with a blocking implementation: a shared waiter condvar/futex registered on each handle. Unit-test wait-any/wait-all/timeout, rebuild, re-profile. | Claude + user test | ✅ 2026-11-13 — see "Unit 2 results" |
| 2b | (small, optional) Alertable waits / `AlertableSleep` still wake every 1 ms (`kAlertablePollSlice`) to check for queued APCs. Make `QueueUserCallback` wake the thread's waiter instead. Only do this if a profile shows it. | Claude | ⏳ |
| 3 | Cap the presenter: present only when there's a new guest frame and/or at the display refresh (FIFO/MAILBOX). Re-measure with MangoHud, which then becomes meaningful. | Claude + user test | ✅ 2026-11-13 — see "Unit 3 results" |
| 4 | **Revised after 1b:** off-CPU/wait profile + trace what `GpuProgressCheck_82B9BF90` / `FrameLimiterWait_82242628` wait for and who signals it. | Claude + user test | ✅ 2026-11-13 — see "Unit 4 results" |
| 4b | Make the write-watch fault path cheap: stop re-reading `/proc/self/maps` in `memory::QueryProtect` (Linux) on every SIGSEGV, under the global lock. | Claude + user test | ⏳ next |
| 4c | (later) Fewer faults per frame (~30/frame), and stop `FrameLimiterWait`'s `sched_yield` spin burning a core (matters on the Deck: block on the counter write instead). | Claude | ⏳ |
| 4d | Re-measure after 4b (`sudo tools/perf/waitprof.sh`). Expected: limiter moves to GPU Commands (~32 ms of work/frame, i.e. ~30 FPS cap; estimate). | user + Claude | ⏳ |
| 4e | Make GPU Commands cheaper per draw: SDK host code, readable C++. `IssueDraw` is ~65% of the thread (UpdateBindings, texture cache lookups, register writes, SharedMemory::RequestRanges). | Claude + user test | ⏳ |
| 4f | (riskier, only if still capped ~30 after 4e) Let 3D Engine and GPU Commands overlap: allow 2 frames in flight instead of the game's "wait until the GPU thread finished the previous frame" (`FrameLimiterWait_82242628` / `BeginFrame_82B9BA58` deadline = counter+2). It's one well-understood wait, but it changes the game's own sync, so check for visual/timing breakage. | Claude + user test | ⏳ |
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

## Unit 3 results (2026-11-13, `out/perf/unit3-ui-tick-204712/`)

Cause: an open ImGui dialog calls `RequestUIPaintFromUIThread()` every frame. The only
limiter, `WaitForUITickFromUIThread` (DXGI vblank thread), was `#if REX_PLATFORM_WIN32`,
and the Vulkan swapchain prefers IMMEDIATE, so on Linux the UI thread presented as fast as it could.
Fix (SDK `src/ui/presenter.cpp` + `include/rex/ui/presenter.h`): non-Win32 branch of
`WaitForUITickFromUIThread`/`ForceUIThreadPaintTick`, a condvar `wait_until` the next tick at
new hot-reload cvar `--present_ui_max_fps` (default 60, 0 = uncapped). Guest frames still
force an immediate tick. Present mode not changed (FIFO would block the guest output thread);
MAILBOX can be A/B'd with `--vulkan_allow_present_mode_immediate=false`.

| Metric | Unit 2 | Unit 3 |
|---|---|---|
| Main `fable_2` (presenter/UI) thread samples | ~27.8k (18.3%) | ~1.8k (1.4%) |
| Total samples in 20 s | 152k | 129k |
| Guest F3 FPS at test spot | 16 | 16 |

The host spin is gone, but guest FPS didn't change: the bottleneck is GPU-side sync (finding #4).
GPU Commands / GameThread / 3D Engine are still unsaturated (`GpuProgressCheck`, `FrameLimiterWait`,
`sched_yield`). **Run unit 1b next.** MangoHud FPS now shows the capped present rate.

## Unit 4 results (2026-11-13, `out/perf/unit4-210222/`, F3 = 18 FPS)

**What the waits poll (static).** `FrameLimiterWait_82242628` (called by the **3D Engine** thread,
`MainRenderLoop_82B9CD68`, not GameThread) spins on `GpuProgressCheck_82B9BF90` until
`*(*(video+10896)) >= *(video+10908)`. `video+10896` points to a guest "GPU progress" counter
(seen `0xFFC83000`); `+10908` is the deadline. `BeginFrame_82B9BA58` resets the counter to
deadline-2; only the **GPU Commands** thread writes it (`EVENT_WRITE_SHD`, value = host vblank
count, ~1000 Hz with `--guest_vblank_pacing=false`). So it means "wait until the GPU thread
has executed the previous frame's commands": at most about one frame in flight.

**Live (waitprof, user+kernel CPU):**

| Thread | Cores | User | Kernel | State R | Notes |
|---|---|---|---|---|---|
| 3D Engine | 0.99 | 0.43 | 0.56 | 100% | **saturated** |
| GameThread | 0.84 | 0.47 | 0.37 | 84% | game's own fixed-step loop; kernel = `MaybeYield` sched_yield spin, harmless |
| GPU Commands | 0.59 | 0.55 | 0.04 | 59% | **idle 41%**, sleeping on `write_ptr_index_event_` (starved of commands) |

3D Engine time split (kernel-inclusive profile): ~50% spinning in `FrameLimiterWait` (sched_yield),
**~31% write-watch fault handling**, ~19% real work. 551 SIGSEGV/s ≈ 30 faults per frame at
~0.5 ms each (about 17 ms of a 55 ms frame). Each fault: `MMIOHandler::ExceptionCallback` →
`memory::QueryProtect` → `FindEntryForAddress` opens and `sscanf`s `/proc/self/maps`
(2164 lines) **while holding the global critical region lock**, which GPU Commands also needs
(`SharedMemory::RequestRange`).

**Conclusion:** the frame is a ping-pong between 3D Engine (produce) and GPU Commands (consume).
Neither the GPU nor a fence is slow. The producer's critical path is padded by the
`/proc/self/maps` fault path. Fix it first (unit 4b).

**Decided (2026-11-13):** don't split the 3D Engine guest thread across cores. Only ~19% of it is
real work, and the guest code shares state without locks, so threading it means
reverse-engineering the render loop first (Theseus readability work, much later if ever).
Order: 4b fault path → 4d re-measure → 4e GPU Commands per-draw cost → 4f two frames in flight → Theseus. The unit 3 user-only profile had pointed
at GPU Commands (0.92 "cores" from `cycles:u`) only because kernel time wasn't sampled, and
`capture.sh`'s per-thread snapshot had a parsing bug (comm names with spaces → 0); both fixed/avoided now.

## A/B results (fill in)

| Date | Flags | Guest FPS (F3) at test spot | Visual issues |
|---|---|---|---|
| 2026-11-13 | (none, after unit 2) | 16 | |
| 2026-11-13 | (none, after unit 3) | 16 | |
| 2026-11-13 | `--readback_memexport=false` (after unit 3) | 17 | none noted; MangoHud 70 but feels like 17 |
| 2026-11-13 | `--occlusion_query_enable=false` (after unit 3) | 17 | none noted; MangoHud 70 but feels like 17 |
| 2026-11-13 | (none, unit 4 wait profile run) | 18 | |

Conclusion: neither GPU readback nor occlusion queries limit the desktop (+1 FPS, within noise).
MangoHud's ~70 is host presents: about 60 capped UI repaints plus forced presents for the ~17 guest
frames. It's not the game's frame rate.
