# Desktop performance — multi-session plan (START HERE)

Started 2026-11-13. Supersedes the older Steam Deck / offline perf handoffs as the
*active* plan. Those stay as evidence; don't re-read them unless a step below says so.

**Goal (revised 2026-11-13):** a **solid 30 FPS** on the desktop (i9-10900K, RTX 3090, CachyOS).
Fable II is locked to 30 FPS by design (community high-FPS/tick patches exist but aren't
perfect), so once 30 holds at the worst views the bottlenecks are gone and this effort is **done**.
60 FPS, the Steam Deck port and Theseus rewrites are separate, later projects.

**STATUS: DONE (2026-11-14, unit 4h).** All three "Done when" points met; see "Unit 4h results".

**Done when:** (1) F3 guest FPS holds 30 at 2–3 chosen worst views (incl. the view that gave 16 in
`out/perf/unit4e-212915/`), (2) with guest vblank pacing back ON (commit `ac6ba00` turned it off)
and game speed looks right (animations, NPC movement, day/night), (3) no recurring hitches while walking.

**Working style (keeps usage down):** one focused unit per session. The user drives
the game and runs A/B launches; Claude reads numbers, edits code and rebuilds.
Each session updates the status table below and nothing else.

## Session hand-off rule

At the end of every session Claude updates the status table and replaces the
**Next session prompt** below. The user pastes it into a fresh session.

### Next session prompt

```
Fable II desktop performance: plans/PERF_SESSIONS.md is DONE (solid 30 FPS with guest vblank
pacing on, unit 4h). Read ONLY its "Unit 4h results" section. Only reopen this plan if a new
recurring hitch or a sustained < 30 view turns up; then the next candidate is unit 5 (persistent
VkPipelineCache / background pipeline compile) or 4g3. Otherwise start a separate plan (60 FPS,
Steam Deck port, Theseus rewrites or native renderer). Explain simply (I'm a Java dev); store
findings in the memory MCP (Fable II = projectId 3; reusable = GLOBAL).
```

## How to run things

| Task | Command |
|---|---|
| Full Release build (~5 min, first time) | `bash build-release-linux_amd64.sh` (edit `-j4` to `-j12` locally if wanted) |
| Rebuild after an SDK runtime/GPU change only | `cmake --build out/build/linux-amd64-release --target fable_2 -j12` |
| **After a GPU-plugin-only change** (`src/graphics/**`): the build does NOT re-stage the plugin unless `fable_2` relinks. Copy it by hand and check md5 | `cp thirdparty/rexglue-sdk/out/linux-amd64/librexgpu-xenos.so out/build/linux-amd64-release/` |
| Launch with MangoHud overlay | `tools/perf/run-profile.sh [extra --flags]` |
| 20 s CPU profile of the running game | `tools/perf/capture.sh <label>` → `out/perf/<label>-HHMMSS/` |
| Wait profile (per-thread user/kernel CPU, R/S state, ctx switches; with sudo also perf sched, SIGSEGV rate, kernel-inclusive profile) | `sudo tools/perf/waitprof.sh <label>` → `<dir>/waitprof.txt`, `signals.txt`, `perf-kernel.data` |
| CPU→GPU upload / write-fault stats (unit 4g) | add `--gpu_shared_memory_stats=true`; lines `shared_memory_stats` every 60 frames in the newest `out/build/linux-amd64-release/logs/fable_2_NNN.log` |
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
| 4b | Make the write-watch fault path cheap: stop re-reading `/proc/self/maps` in `memory::QueryProtect` (Linux) on every SIGSEGV, under the global lock. | Claude + user test | ✅ 2026-11-13 — see "Unit 4b results" (18 → 25 FPS) |
| 4c | (parked; not needed for 30) Fewer faults per frame, block `FrameLimiterWait` instead of `sched_yield` spin (Deck power). | Claude | ⏸ |
| 4d | Re-measure after 4b (`sudo tools/perf/waitprof.sh`). Expected: limiter moves to GPU Commands (~32 ms of work/frame, i.e. ~30 FPS cap; estimate). | user + Claude | ✅ 2026-11-13 — done as part of 4b: confirmed, ~30 ms GPU Commands work/frame |
| 4e | Make GPU Commands cheaper per draw: batch type-0 register writes. | Claude + user test | ✅ 2026-11-13 — see "Unit 4e results" (A/B 21 → 27 FPS) |
| 4f | Let 3D Engine and GPU Commands overlap. Found: the turn-taking is the game's own design (not fixable host-side without risking data races). Did the one safe host overlap: async Vulkan submit, cvar `--vulkan_async_submit`. | Claude + user test | ✅ 2026-11-13 — see "Unit 4f results" (−1.2 ms/frame critical path, 23 → 24 FPS, no visual issues) |
| 4v | Measure the worst view after 4g ("angle 2", see A/B table). | user + Claude | ✅ 2026-11-13 — see "Unit 4v + 4g2 results": 26 FPS = ~21 ms engine wait + ~15 ms engine work |
| 4g | Stop needless re-uploads / re-protection. Found the real cause with new upload stats: SDK cvar `clear_memory_page_state` (default true) marked all CPU-uploaded pages stale every frame → ~21 MB + ~1,130 mprotect ranges per frame. Fable now seeds it **false**. | Claude + user test | ✅ 2026-11-13 — see "Unit 4g results" (24 → **31 FPS** at the test spot) |
| 4g-b | Cache/skip per-draw `HasNonDefaultValue` lookup + new cvar **`--gpu_write_fault_widen_kb`** (SDK default 256 = old behaviour). Also found+fixed a /dev/shm leak (SIGBUS crash). Fable seeds **16**. | Claude + user test | ✅ 2026-11-14 — see "Unit 4g-b results" (26 → 32 from the lookup fix, → **35–40 FPS** at angle 2 with 16 KB; uploads 2,750 → ~650 KB/frame) |
| 4g2 | Skip rewriting texture descriptor sets when unchanged since the previous draw (cvar `--vulkan_reuse_texture_bindings`, default off). | Claude + user test | ❌ 2026-11-13 — no gain (26 vs 26 FPS); see "Unit 4v + 4g2 results". Not seeded. |
| 4g3 | (only if still < 30 after 4g+4g2) Smaller extras: memexport readback (~1–2 ms/frame; `--readback_memexport=false` only gave +1 FPS earlier, so look at the wait itself). Then the deeper option: **Fable-specific "don't watch" knowledge**: if 4g's counts show the same guest ranges re-protected every frame (e.g. static vertex data loaded once), reverse-engineer what they are and stop write-watching them. Needs RE, so higher effort/risk. | Claude + user test | ⏳ |
| 4h | Turn guest vblank pacing back on (revert `ac6ba00` / `--guest_vblank_pacing=true`) so the game's own 30 FPS lock and vblank-based timing work as on the console. Verify F3 flat at 30 and correct game speed. | Claude + user test | ✅ 2026-11-14 — see "Unit 4h results"; **plan DONE** |
| 5 | (only if there's stutter) Pipeline-creation hitches: persistent `VkPipelineCache` + async/background pipeline compile. | Claude + user test | ⏳ |
| — | **Out of scope for this plan:** 2b alertable waits, 4c, Theseus rewrites (6+), Steam Deck port, and a **native renderer** (game render code calling Vulkan directly, no Xenos interpretation; see `plans/NATIVE_RENDERER_*.md`), which is the ultimate fix but a separate, much later project. | | ⏸ |

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

## Unit 4b results (2026-11-13, `out/perf/unit4b-211720/`, F3 = 25 FPS, was 18)

Fix (SDK `src/core/memory_posix.cpp`): `FindEntryForAddress` (used by `QueryProtect` and
`Protect`'s old-access query) now asks the kernel with the `PROCMAP_QUERY` ioctl
(Linux 6.11+) on a /proc/self/maps fd opened once. That's one syscall for one address, and it reads the same
kernel state as the text file, so behaviour is unchanged. If the kernel doesn't support it (ENOTTY/EINVAL), it falls back permanently
to the old text scan, which matters for older Steam Deck kernels. Struct/ioctl number are declared locally (no new header dependency).
Microbenchmark: ~0.2 µs per lookup vs ~500 µs. Test: `tests/unit/memory/query_protect_test.cpp`
(5 cases, pass; run with the Catch2 stand-in header again).

| Metric | Unit 4 | Unit 4b |
|---|---|---|
| Guest F3 FPS | 18 | **25** |
| `MMIOHandler::ExceptionCallback` (incl., % of all samples) | 10.7% | 0.5% |
| `QueryProtect` / `FindEntryForAddress` | 10.35% | 0.02% |
| SIGSEGV/s | 551 (~30/frame) | 802 (~32/frame, more frames) |
| 3D Engine cores (R%) | 0.99 (100%) | 0.99 (100%), ~73% of it `FrameLimiterWait` spin |
| GPU Commands cores (R%) | 0.59 (59%) | 0.76 (77%) |
| GameThread cores | 0.84 | 0.99 (its own `Yield` spin, harmless) |

Read: GPU Commands now does ~30 ms of host work per 40 ms frame, and the 3D Engine mostly waits
for it. Even with perfect overlap (4f) the cap would be about 33 FPS, so 60 FPS needs the GPU Commands per-frame
cost cut roughly in half. **Next: 4e** (WriteRegister, UpdateBindings, texture-cache hash lookups,
ExecutePacket are the top self-time symbols).

## Unit 4e results (2026-11-13, A/B at the same spot: `out/perf/unit4e-old-215256/` vs `out/perf/unit4e-new-215041/`)

Fix (SDK `src/graphics/command_processor.cpp`, `ExecutePacketType0`): consecutive type-0 register
writes now go through `WriteRegisterRangeFromRing` → the backend's batched `WriteRegistersFromMem`
(one copy + one dirty-bit update for whole shader-constant ranges; anything else falls back to the
same per-register `WriteRegister`). "Write one register N times" mode unchanged. Same thing upstream
Xenia does. No unit test (needs a Vulkan device); equivalence checked by reading both paths.
Old plugin binary kept for future A/Bs: `out/perf/unit4e-old-215256/librexgpu-xenos.so.old`.

| Metric | Old plugin | New plugin |
|---|---|---|
| Guest F3 FPS (same spot/view) | 21 | **27** |
| Type-0 packet path (incl., % of GPU Commands) | 14.8% | 5.1% |
| GPU Commands cores | 0.75 | 0.73 |
| GPU Commands host work per frame | ~36 ms | ~27 ms |
| SIGSEGV per frame | ~34 | ~35 |

New GPU Commands split (inside `IssueDraw`, ~75% of the thread): `RequestRange` 18%
(of which `UploadRanges` 14%, mutex 2%), `UpdateBindings` 12%, `RequestTextures` 9%,
`GetSamplerParameters` 6%, `RenderTargetCache::Update` 5.5%, memexport checks 3%.

**Lessons:** (1) the GPU backend is a runtime-loaded plugin; a plugin-only rebuild does not re-stage
it next to `fable_2`, and the first 4e run (`out/perf/unit4e-212915/`, 16 FPS) silently tested the
old code. Always md5-check. (2) FPS at "the test spot" swings 16–25 with position/view, so judge
changes only by same-spot A/B runs, not against an older session's number.

## Unit 4f results (2026-11-13, `out/perf/unit4f-off-221952/` vs `out/perf/unit4f-on-222221/`)

**Step 1: serialization confirmed.** `perf sched` lost ~70% of events (the 3D Engine's
`sched_yield` spin floods it), so the evidence is a 1 ms timeline built from the sampled
profile (`perf script` → classify each 3D Engine sample as in/out of `FrameLimiterWait`, each
GPU Commands sample as working/swap). At the 4e spot: 3D Engine works ~9–10 ms, kicks the whole
frame at the end, then waits ~25 ms; GPU Commands is idle ~8 ms while the engine works, then
translates ~25 ms; the engine wakes ~0.5–1.5 ms after GPU Commands' `IssueSwap`/`EndSubmission`.
Frame ≈ 10 + 25 + ~2 = 37 ms.

**Step 2: what holds the producer back.** `MainRenderLoop_82B9CD68` calls
`FrameLimiterWait(video, now = video[14940], 3)` after the frame's swap call (`ProcessAndProcessAndProcess1256_82BA8630`),
then stores `video[14940] = deadline (video[10908])`. The wait ends when the GPU-written counter
(`*video[10896]`, written by `EVENT_WRITE_SHD` with the vblank counter) reaches it, which in practice means
**"GPU Commands has processed the swap of the frame I just submitted"**. This is the game's design:
on a 360 the GPU front end consumed commands almost instantly, so 10 ms CPU + GPU time fit in 33 ms.
Releasing the wait early (host lying about the counter, or the hotfunc waiting on an older target) would
let the game overwrite buffers GPU Commands hasn't read yet → rejected. (Correction: `sub_82B9BA58`, called
"BeginFrame" in the unit 4 notes, is one-time ring-buffer setup (`VdInitializeRingBuffer`), not per frame.)
The 3D Engine's own ~10 ms is ~88% real guest code, ~8% write-watch faults, so there's little to trim there.

**Fix (the one safe host-side overlap):** SDK `src/graphics/vulkan/command_processor.cpp` +
`include/rex/graphics/vulkan/{command_processor,deferred_command_buffer}.h`, hot-reload cvar
**`--vulkan_async_submit`** (default false). `EndSubmission` still does all bookkeeping on GPU Commands,
but the deferred-command replay into the real `VkCommandBuffer` + `vkQueueSubmit` run on a new
**"GPU Submit"** thread (one worker, FIFO, streams swapped not copied). Fence polling only looks at fences
the worker already submitted; blocking fence waits, sparse binds, toggling the cvar off, and swaps (the
presenter submits right after) wait for the worker first. Off by default and in RenderDoc captures.

| Metric (same spot; user looked around in the "on" run) | off | on |
|---|---|---|
| Guest F3 FPS | 23 | 24 |
| `EndSubmission` samples while 3D Engine waits (critical path) | 569 | 2 |
| 3D Engine wait per frame (median) = GPU Commands kick→swap | 30.2 ms | **29.0 ms** |
| 3D Engine work per frame (median; view-dependent) | 12.1 ms | 14.4 ms |
| GPU Commands cores (+ GPU Submit) | 0.73 | 0.66 (+0.12) |
| Visual issues | none | none |

Gain is small (~1.2 ms/frame, est. ~2 ms before; the swap's own replay stays synchronous). Keep it on for
future runs. **Remaining gap:** at 22–24 FPS views the frame is ~12–14 ms engine + ~29–30 ms GPU Commands;
30 FPS needs GPU Commands kick→swap ≈ 19–21 ms, i.e. ~30% cheaper → unit 4g.

**Method (reuse):** `perf script -i perf-kernel.data -F tid,time,ip,sym --tid <gpu>,<engine>`, then per
engine sample "in FrameLimiterWait/GpuProgressCheck or not" gives wait/work runs per frame; GPU samples during
engine-wait runs are the critical path. Scripts: `tools/perf/frames.py` (per-frame wait/work medians) and `tools/perf/critpath.py` (GPU Commands work split by engine phase). Get tids from waitprof.txt.

## Unit 4g results (2026-11-13, `out/perf/unit4g-clear-on-225131/` vs `out/perf/unit4g-clear-off-225417/`)

**Step 1, measure.** New diagnostic cvar **`--gpu_shared_memory_stats`** (SDK `src/graphics/shared_memory.cpp`,
hot-reload, default off): every 60 guest frames logs per-frame upload KB/ranges, % also uploaded last frame,
write faults (KB written vs KB invalidated after widening) and the top 8 upload addresses, to
`out/build/linux-amd64-release/logs/fable_2_NNN.log` (not stdout). Hooked from `VulkanCommandProcessor::IssueSwap`.
Copies of the logs: `out/perf/fable_2_018.log` (A), `fable_2_020.log` (B), `unit4g-stats-game.log` (first look).

**What it showed.** The plan's idea (merge adjacent ranges, skip already-protected pages) was already
implemented in `PhysicalHeap::EnableAccessCallbacks`. The real problem: ~21 MB/frame uploaded, 94% the same
pages as last frame, but write faults only invalidated ~8 MB. The rest came from
`SharedMemory::SetSystemPageBlocksValidWithGpuDataWritten()`, called at every frame end when SDK cvar
`clear_memory_page_state` is true (SDK default; added generically in SDK commit `0bd3521`, described as
"minor CPU overhead"). It drops the valid bit of every CPU-uploaded page, so the next frame re-copies and
re-`mprotect`s everything it touches. It's a safety net for host code writing guest memory through the
unprotected mapping without a fault; Fable showed no stale data without it.

**Fix:** `src/core/fable_2_app.h` `OnPostSetup()` seeds `clear_memory_page_state=false` (only if still at the
SDK default, so `--clear_memory_page_state=true` / fable_2.toml can still A/B it). No SDK default changed.

| Per frame (same spot/view) | A: on (old default) | B: off (new default) |
|---|---|---|
| Uploaded | 21 MB (94% same as last frame) | **6.6 MB** (55%) |
| Upload ranges (≈ re-protect calls ×3 heaps) | ~1,130 | ~700 |
| Write faults (KB written → KB invalidated) | 34 (135 KB → 8 MB) | 70 (280 KB → 16 MB) |
| GPU Commands kick→swap (frames.py engine wait med) | 27.0 ms | **18.7 ms** |
| 3D Engine work med | 11.4 ms | 11.0 ms |
| Frame period med (frames.py) | 41.0 ms (24.4 FPS) | **32.4 ms (30.9 FPS)** |
| Guest F3 FPS | 24 | **31** |
| Memexport readback on critical path (critpath.py samples) | 292 | 21 |
| Visual issues | — | none (walked around; some hitching while moving, settled) |

A slightly different angle in the same scene still showed **24 FPS** → measure worst views next (4v).
Hitching while moving is not yet explained (streaming, pipeline compiles → unit 5, or the fault widening).

**Lessons:** (1) count before optimizing: the sampled profile pointed at mprotect, but the counter showed the
mprotects were caused by a per-frame "forget everything" flag, not by the protect code. (2) Check SDK "safety
net" cvar defaults against upstream behaviour; one generic default cost ~8 ms/frame here.

## Unit 4v + 4g2 results (2026-11-13, `out/perf/4v-angle2-230821/`, `out/perf/4g2-off-232329/` vs `out/perf/4g2-on-232552/`)

**4v ("angle 2", same scene as the 4g test spot, the angle that showed 24 after 4g):** F3 26.
frames.py: period med 38.7 ms, engine **wait 21.4 ms** (GPU Commands), engine **work 15.1 ms** (game code).
vs 4g spot: wait 18.7 / work 11.0. Uploads only ~4.9 MB/frame (60% repeat), ~53 faults/frame,
212 KB written -> **12.4 MB invalidated** per frame. Lionhead logo flickered at boot (pre-existing, not a regression).

**4g2 (SDK `src/graphics/vulkan/command_processor.cpp` `UpdateBindings` + `include/rex/graphics/vulkan/command_processor.h`):**
new hot-reload cvar `--vulkan_reuse_texture_bindings` (default off) keeps the per-stage texture/sampler
descriptor set if the image views/samplers/layout equal the last written ones (resolves the upstream TODO).
Also applies the computed-but-unused `descriptor_sets_kept` after a pipeline-layout change (as upstream
Xenia; no effect when the cvar is off). Old plugin: `out/perf/4v-angle2-230821/librexgpu-xenos.so.pre4g2`.

| Same spot/angle 2 | off | on |
|---|---|---|
| Guest F3 FPS | 26 | 26 |
| frames.py period / engine wait / work (med) | 38.9 / 21.0 / 15.4 ms | 39.7 / 21.9 / 15.6 ms |
| `vkUpdateDescriptorSets` samples (UpdateBindings+0x1859, NVIDIA driver) | 432 | 391 |
| Visual issues | none | none reported |

**Why no gain:** Fable changes material on nearly every draw, so consecutive texture sets rarely match;
and the same `vkUpdateDescriptorSets` call also writes the constants set, which changes every draw.
(Making the constants dynamic uniform buffers would avoid that, est. ~1.5 ms/frame, medium effort; parked.)

**Where GPU Commands time goes at angle 2 (inclusive, % of IssueDraw ≈ 21 ms/frame):** `SharedMemory::RequestRanges`
~28% (UploadRanges + `EnableAccessCallbacks`/mprotect) → **4g-b**; UpdateBindings ~18% (half driver
descriptor writes); RequestTextures ~14%; PrimitiveProcessor ~13%; type-0 regs ~8%; RenderTargetCache ~7%;
GetSamplerParameters ~7%; `cvar::HasNonDefaultValue` ~4% (string lookup per draw from
`IsReadbackMemexportEnabled`, cheap fix).

**Lessons:** (1) before building a "same as last time?" cache, measure the hit rate (one counter) first.
(2) `[unknown]` frames under a host function are the NVIDIA driver; find the call with
`perf script -F ip,sym,symoff` + `objdump` at that offset.

## Unit 4g-b results (2026-11-14, DONE; `out/perf/4gb-256-235522/`, `4gb-64-235817/`, `4gb-16-000054/`)

SDK changes (all built + staged, plugin md5 `a2345b0a…`; old plugin `out/perf/4gb-pre/librexgpu-xenos.so.pre4gb`):
1. `src/graphics/{vulkan,d3d12}/command_processor.cpp`: memexport readback check now tests
   `!memexport_ranges_.empty()` **before** `IsReadbackMemexportEnabled` (whose `HasNonDefaultValue` = lock +
   string map lookup per draw). Same behaviour, lookup only on memexport draws.
2. `src/graphics/shared_memory.cpp` `MemoryInvalidationCallback`: new hot-reload cvar
   **`--gpu_write_fault_widen_kb`** (default 256 = upstream: whole 64-page block). Caps the widened
   invalidation/unprotect to an aligned window of that size (still also clipped by GPU-written pages).
   `PhysicalHeap::TriggerCallbacks` unprotects the min over callbacks, so this also narrows the unprotect.
3. `src/core/memory_posix.cpp` `CreateFileMappingHandle`: `shm_unlink` right after create (fd keeps it
   alive; only user is xmemory.cpp, never re-opened by name). **Bug found:** every killed/crashed run leaked
   its 4.8 GB sparse `/dev/shm/xenia_memory_*` file (45 files, 13 of 16 GB used) → touching new guest pages
   gave **SIGBUS** (crash during save load in the first 4 KB run; core dump: `seh_posix` re-raise on top of
   guest `sub_82B2BCC8`). Not caused by the widening. Fix built, **not yet verified** (check `ls /dev/shm`
   after a run; a reboot clears the old files anyway since /dev/shm is RAM).

A/B at angle 2 (standing still, `--vulkan_async_submit=true --gpu_shared_memory_stats=true`; logs 026/027/028):

| widen_kb | F3 | period / engine wait / work med (ms) | faults/frame | KB invalidated | KB uploaded | Visual |
|---|---|---|---|---|---|---|
| 256 (default) | 32 | 31.4 / 17.6 / 11.5 | ~39 | ~8,400 | ~2,750 | none |
| 64 | 33 | 30.0 / 16.3 / 11.4 | ~60 | ~3,450 | ~1,680 | **black screen with light fragments** (one run) |
| 16 | **35** | **28.2 / 14.8 / 11.2** | ~77 | ~1,150 | ~760 | none |
| 4 | — | — | — | — | — | crashed (SIGBUS from the /dev/shm leak, not this cvar) |

Reads: 2× faults cost nothing visible on the 3D Engine (work 11.5 → 11.2 ms; faults are cheap since 4b), while
GPU Commands saves ~3 ms/frame. Run A (256) at 32 vs 26 last session = fix (1) + slightly different view.
The 64 KB black screen is unexplained: narrowing can't leave stale data in theory (pages not invalidated stay
protected and fault again), and 16 KB was clean, so maybe a one-off; must re-test before seeding.

**Confirmation runs (logs 030/031/032):** 16 KB again = **40 FPS**, walked ~1 min, felt good, no glitches;
64 KB again = 36 FPS, no black screen (the earlier one was a one-off, cause unknown; watch for it). `/dev/shm` had
no `xenia_memory_*` file even *while* the game ran → leak fix verified. **Seeded 16** in `src/core/fable_2_app.h`
`OnPostSetup` (only when still at SDK default; log line `seeded gpu_write_fault_widen_kb=16 (perf)`). Run with no
flag: "quite consistent", ~40 FPS (60-frame stats every ~1.5 s), ~650 KB uploaded / ~1 MB invalidated per frame.
`--gpu_write_fault_widen_kb=256` restores upstream behaviour for A/B.

**Lessons:** (1) a SIGBUS (not SIGSEGV) in guest code on Linux = shm backing failure; check `df -h /dev/shm` first.
(2) shm objects that nobody re-opens by name should be unlinked right after creation.

## Unit 4h results (2026-11-14) — plan DONE

**Why pacing was off:** `ac6ba00` (run-debug.sh) and `8b79b86` (run-release.sh, copied into run-profile.sh) added
`--guest_vblank_pacing=false` on purpose, to try for 60 FPS. No hang or slowdown. The SDK "GPU VSync" thread
(`thirdparty/rexglue-sdk/src/graphics/graphics_system.cpp`) fires guest vblanks at the video-mode rate (60 Hz) with
pacing on, ~1000 Hz with it off. Fable waits for 2 vblanks per frame → 30 FPS lock with pacing on. Catch: frames are
quantized to whole vblanks (a 34 ms frame waits for the 3rd one → 20 FPS), so it only holds 30 once frames are
< 33 ms, which 4g + 4g-b achieved (~25–28 ms at angle 2).

**Change:** removed `--guest_vblank_pacing=false` from `run-release.sh`, `run-debug.sh` and `tools/perf/run-profile.sh`
(the last is git-ignored, local only). SDK default is already `true`, so no code seed and no rebuild needed. The Windows
launcher already turns pacing on for its 30 FPS setting. `--guest_vblank_pacing=false` still works for 60 FPS experiments.

**Result (user runs, pacing on, `--vulkan_async_submit=true`):** angle 2 + running around: felt good, lowest F3 ~28.
Other worst views (4g test spot + busiest spot) while walking: lowest F3 28, "feels more stable" than unpaced
35–40. Game speed looked normal. The 28s are brief dips (F3 averages over a short window, so one slow streaming frame
pulls it under 30), not a sustained drop to 20, which is what a frame that doesn't fit 2 vblanks would show.

**Done when:** (1) worst views hold 30 except brief dips ✅, (2) pacing on, game speed right ✅, (3) no recurring
hitches reported ✅. If the brief dips ever bother you: unit 5 (pipeline cache / background compile) is the follow-up.

## A/B results (fill in)

| Date | Flags | Guest FPS (F3) at test spot | Visual issues |
|---|---|---|---|
| 2026-11-13 | (none, after unit 2) | 16 | |
| 2026-11-13 | (none, after unit 3) | 16 | |
| 2026-11-13 | `--readback_memexport=false` (after unit 3) | 17 | none noted; MangoHud 70 but feels like 17 |
| 2026-11-13 | `--occlusion_query_enable=false` (after unit 3) | 17 | none noted; MangoHud 70 but feels like 17 |
| 2026-11-13 | (none, unit 4 wait profile run) | 18 | |
| 2026-11-13 | (none, after unit 4b) | 25 | none noted |
| 2026-11-13 | unit 4e run that accidentally used the old plugin (different view) | 16 | |
| 2026-11-13 | A/B same spot: old plugin (4b code) | 21 | |
| 2026-11-13 | A/B same spot: new plugin (unit 4e) | 27 | none noted |
| 2026-11-13 | unit 4f A/B (different view than 4e): `--vulkan_async_submit` off | 23 | none |
| 2026-11-13 | unit 4f A/B: `--vulkan_async_submit=true` (looked around while measuring) | 24 | none |
| 2026-11-13 | unit 4g A/B same spot: `--clear_memory_page_state` on (old default) | 24 | |
| 2026-11-13 | unit 4g A/B same spot: `--clear_memory_page_state=false` (now Fable default) | **31** | none; some hitching while walking, settled |
| 2026-11-13 | after 4g, slightly different angle in the same scene | 24 | |
| 2026-11-13 | **"angle 2"** = 4g test spot, the slightly different angle above; stand still. 4v run, `--vulkan_async_submit=true --gpu_shared_memory_stats=true` | 26 | Lionhead logo flicker at boot (pre-existing) |
| 2026-11-13 | angle 2, unit 4g2 `--vulkan_reuse_texture_bindings` off | 26 | none |
| 2026-11-13 | angle 2, unit 4g2 `--vulkan_reuse_texture_bindings=true` | 26 | none reported |
| 2026-11-14 | angle 2, unit 4g-b `--gpu_write_fault_widen_kb=256` (default; includes cvar-lookup fix) | 32 | none |
| 2026-11-14 | angle 2, `--gpu_write_fault_widen_kb=64` | 33 | black screen with light fragments (re-test) |
| 2026-11-14 | angle 2, `--gpu_write_fault_widen_kb=16` | **35** | none |
| 2026-11-14 | `--gpu_write_fault_widen_kb=4` | crash at load | SIGBUS = /dev/shm full of leaked files (fixed) |
| 2026-11-14 | angle 2, re-test `--gpu_write_fault_widen_kb=16`, walked ~1 min | **40** | none, felt good |
| 2026-11-14 | angle 2, re-test `--gpu_write_fault_widen_kb=64`, walked | 36 | none (black screen did not repeat) |
| 2026-11-14 | angle 2, no widen flag (Fable now seeds 16) | ~40 | none; "quite consistent" |
| 2026-11-14 | unit 4h: `--guest_vblank_pacing=true`, angle 2 + running around | 30, lowest ~28 | none; felt good, speed normal |
| 2026-11-14 | unit 4h: pacing on (now default), other worst views + walking | 30, lowest 28 | none; "feels more stable" |

Conclusion: neither GPU readback nor occlusion queries limit the desktop (+1 FPS, within noise).
MangoHud's ~70 is host presents: about 60 capped UI repaints plus forced presents for the ~17 guest
frames. It's not the game's frame rate.
