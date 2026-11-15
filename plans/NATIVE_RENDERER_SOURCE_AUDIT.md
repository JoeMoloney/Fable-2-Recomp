# Desktop-first native renderer — initial source audit

2026-10-26. **Result: credible staged migration route; no drop-in renderer and
not yet implementation-ready.** This is a bounded initial audit, not a completed
graphics API inventory, shader compatibility test, build or gameplay result.

Follow-up: `NATIVE_RENDERER_FABLE_BOUNDARIES.md` maps resource references/retirement,
a second draw emitter, shader-selection side effects and cached shader features.
Its findings refine the candidate prototype; do not assume the mip path is covered
by hooking only `0x8221C518` or that raw high boolean indices imply incompatibility.

Policy and overall plan: [NATIVE_RENDERER_FEASIBILITY.md](NATIVE_RENDERER_FEASIBILITY.md).

## Reproducible baseline

| Repository | Examined revision | State at audit start |
|---|---|---|
| Fable II | `fb9d2fb9554bff5a207015f4cac197272cabc497` | `master`, existing modified/untracked work preserved |
| Local Fable SDK | `521aa5906e8aa138870bf8d36bd602eef590ed55` | `fable2`, fmt submodule has untracked content |
| Skate | `f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd` | `main`, clean, SDK uninitialized |
| Skate SDK gitlink | `7eb0faf7787f5e01333c228b8e3f03c32f7295ea` | inspected selected pinned files over HTTPS, not checked out |
| Unleashed | `cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c` | `main`, clean, submodules uninitialized |
| Unleashed XenosRecomp gitlink | `990d03b28a27b50277ee5d8d942e1c5f873869d1` | pinned README inspected over HTTPS |

Local SDK is **not the same revision as the staged Deck diagnostic SDK**. Do not
import Deck source/artifacts wholesale or infer desktop state from Deck manifests.

Evidence archive: `out/performance/native-renderer-feasibility-20261026/`:

- `manifest.json`: revisions, status, submodule state, source copies and SHA-256s.
- `fable-tracked.diff`, `sdk-tracked.diff`: tracked diffs at the initial checkpoint.
- `local/`: selected reference and current implementation sources.
- `remote/`: selected Skate SDK files at its exact gitlink and pinned converter
  README. No clone initialization/fetch/pull or source-tree modification required.
- This is **not a complete backup**: untracked files, game artifacts and saves
  were not archived. Full exact backups remain a gate before implementation.

Ghidra confirmed `default.xex`, PowerPC big-endian, four-byte pointers, SHA-256
`88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
Read-only decompilation; no names, signatures or annotations modified.

## 1. Skate: engine reconstruction plus an SDK-hosted native renderer

Observed source path (paths relative to Skate unless marked SDK):

1. `src/skate3_native_render.cpp:395–405`: mesh hook calls the original function,
   then captures dynamic state. Comments explain why transforms/constants are not
   valid at entry. This ordering is a correctness requirement, not optional polish.
2. `:412–429`: sorted scene-list capture, with game-specific occlusion filtering
   and restoration around the original dispatcher.
3. `:434–439`: guest Swap override calls OnFrameEnd before forwarding to original.
   `:444–448` records texture registration; later hooks capture animation palettes.
4. `src/skate3_native_scene.h:3–8` and `src/skate3_native_scene.cpp:10445–10451`:
   guest structures become a host-owned scene published under a mutex/shared_ptr.
   The header's DrawItem includes game-specific meshes, materials and entities.
5. `src/skate3_native_scene_gpu.cpp:11576–11584` registers RenderScene and an
   emulated-output postprocessor. `:6010–6041` retires cached native resources
   through Device::DestroyDeferred rather than freeing GPU-used objects directly.
6. Pinned SDK `include/rex/graphics/native_guest_renderer.h:15–43`: callback gets
   backend-neutral device, frame-scoped command recorder and presenter output
   texture. It must return that texture in the required guest-output state.
7. SDK `src/graphics/vulkan/command_processor.cpp:2846–2875`: sets up the RHI,
   invokes native rendering, ends/submits recording and hands output to presenter;
   false/yield follows an emulated fallback path (wide-output has extra rules).
8. SDK `native_rhi_vulkan.cpp:1265–1281`: submission counters are inherited from
   the existing command processor; retired resources are drained against completed
   submissions. `include/rex/graphics/native_rhi.h:523–547` defines lifetime APIs.

### Critical correction to the README-level impression

**This native path does not mean every Xenos operation has disappeared.**

- SDK `command_processor.cpp:4269` explicitly exempts memexport-capable draws
  from native draw suppression.
- `src/graphics/native_guest_renderer.cpp:162–181` selects suppressed passes
  using **Skate-specific surface pitches**; some composition passes still execute.
- Draw and resolve suppression must agree (`command_processor.cpp:4122–4129`).
  Otherwise stale/uninitialized EDRAM can overwrite resources still used by native
  rendering. Replacing the final image alone does not remove underlying work.
- `command_processor.cpp:903–945` substitutes configured fake occlusion counts
  while native suppression is active. This is real executable logic, despite
  broader comments elsewhere saying queries are unaffected.

**Do not port the fake-query policy, pitch-based suppression, depth-only skipping,
scene caching heuristics or game-specific lifetime assumptions to Fable.** The
existing no-fake-results/no-stale-results constraints remain in force. A native
query replacement must implement the relevant semantics, not invent completion.

Inference: the output-hook/RHI architecture is a useful integration model. A
wholesale Skate SDK transplant is neither necessary nor justified. In particular,
it does not establish a solution to Fable's measured memexport readback wait.

## 2. Unleashed: device/API replacement with native resource ownership

Paths below relative to Unleashed:

- `UnleashedRecomp/gpu/video.cpp:7798–7852`: hooks device creation, resource
  destruction/locking, texture/buffer creation, presentation, targets, clear,
  viewport, textures, draw calls, declarations, shaders, streams and indices.
  These are **Sonic guest addresses**, not transferable Fable addresses.
- `gpu/video.h:34–66`: reverse-engineered GuestDevice layout, including dirty
  flags, constants, viewport and function tables. `:81–116` defines replacement
  resource metadata and guest-endian refcounts. These layouts are not a console-
  wide ABI that we can assume Fable shares.
- `video.cpp:4653–4679`: draw hook flushes guest state into a host command queue;
  render-thread handler builds host state and calls drawIndexedInstanced.
- `:5249–5300`: render worker dispatches native state/resource/draw commands.
- `:2149–2160`: destruction is queued and placed in frame retirement storage.
  `:2180–2205`: texture unlock queues upload, transitions image, copies data.
- `:2791–2854`: presentation drains pending operations, waits for command-list
  execution, presents with semaphore, rotates frames and waits on frame fences
  before resetting frame-local allocators. Native rendering still needs waits.
- `:5046–5095`: shaders are matched to preconverted cache entries by hash, with
  Sonic-specific shader substitutions. This is not arbitrary Fable shader support.
- `kernel/function.h`: guest argument translation is coupled to its PPC context,
  memory mapping and ABI conventions; cannot drop into ReXGlue unadapted.

Pinned XenosRecomp README explicitly documents unsupported memory export,
mini-fetch/binding features, dynamic register indexing and integer constants;
reflection and vertex semantic assumptions also depend on Unleashed. An instruction
coverage audit is required for Fable. README limitations are documented upstream
scope, not a newly executed converter test or an exhaustive code audit.

## 3. Licensing / reuse matrix

This is engineering triage, not a legal opinion. Verify provenance/terms for each
selected file and dependency before incorporating code.

| Component | Evidence | Current reuse position |
|---|---|---|
| Fable app | root LICENSE, MIT-style | keep existing implementation and notices |
| Fable SDK | root LICENSE, BSD-3-Clause-style, Xenia attribution | keep current fork; retain notices |
| Skate SDK RHI/output integration | pinned root LICENSE has same BSD-style terms | candidate for selective adaptation after per-file/dependency review |
| Skate app scene/material/shader code | tracked-source text scan found no license notice; no LICENSE file in initial scan | no copying until permission is established; architectural study can continue |
| Unleashed renderer code | root COPYING GPLv3 | not an MIT-only drop-in; incorporating code requires reviewing applicable GPL distribution/source obligations |
| XenosRecomp / plume | their code/dependency licensing not yet audited | no assumption that parent license settles dependency terms |

No upstream application code has been incorporated into production.

## 4. Fable: verified footholds, not a complete native boundary

Address-based decompilation was reconciled with generated source. Generated
partition locations are lookup aids only and may change on regeneration.

| Guest address | Evidence and limits |
|---|---|
| `0x8222EA68` | measured export setup caller from prior Deck capture; generated `.206.cpp:1679–1715` loads logical ID 313, descriptor into **v1**, calls setter, stream helper, state flush, draw emitter. Not yet a named scene subsystem. |
| `0x82210418` | `.34.cpp:947` onward / decompile: maps logical IDs through table pointer `DAT_834BDBC4`, stores vector constant into global device `DAT_8336019C`, updates dirty mask. Decompiler's one-int prototype omits live vector input; do not reuse it as a complete ABI. |
| `0x82220D38` → `0x821B6DA0` | `.143.cpp:1153` / `.141.cpp:309`: wrapper passes underlying resource to helper which updates address/size, dirty bits and stride-like state. Candidate stream-binding boundary; exact layout, units, ownership and all modes still need proof. |
| `0x8221C518` | `.262.cpp:1070` onward / decompile: flushes multiple dirty-state banks then emits `0xC0012201` draw packets. Opcode 0x22 matches PM4_DRAW_INDX in SDK `include/rex/graphics/xenos.h:1584`. Strong candidate draw/state boundary, not all draws or a complete native draw ABI. |
| `0x821FC4A0` | `.95.cpp:762` onward / decompile: texture mip-generation loop, temporary target, draw through `0x821FA0D0`, resolve through `0x822326B0`, stops generating when a dimension is <=16. Existing morph investigation supplies useful correctness evidence. |

Current backend: `thirdparty/rexglue-sdk/src/graphics/vulkan/command_processor.cpp`:
IssueSwap at :2331, presenter refresh at :2504, IssueDraw at :3683, memexport
range validation and readback later in IssueDraw. No existing NativeGuest output
API was found in the local SDK source search. Those integration changes would
need selective adaptation, not just enabling a flag.

### Still missing before choosing a full migration architecture

- Verified resource creation/destruction/lock/update and texture aliasing map.
- Other draw variants, prebuilt command buffers and paths bypassing normal setters.
- Render-target/depth/resolve/presentation entry points with ABI coverage.
- Fable material/scene layouts if attempting Skate-style scene extraction.
- Shader instruction/format/constant coverage and full tessellation requirements.
- Memexport reader/lifetime/publication requirements, including guest CPU reads.

The existing realtime morph hook shows a game-specific alternative texture path,
not a general permission to skip publication. See the **later** sections of
`linux-vulkan-realtime-morph-investigation.md` for the exponent-adjustment fix;
its early failed experiments are not the current final explanation.

## 5. Provisional architectural recommendation

**Keep the current app/runtime and SDK fork. Investigate a Fable-specific
device/state adapter, with a small native integration surface, before deciding
whether engine-scene extraction is worthwhile.** Confidence: provisional.

Reasons: Fable already exposes a dirty-state/draw-emission cluster analogous in
purpose (not proven layout) to the boundary Unleashed replaces. We have much less
verified evidence for a stable whole-scene extraction interface. Skate's output
callback and submission-aware RHI demonstrate coexistence plumbing without
requiring us to adopt its engine model or query policies.

| Route | Benefit | Main unresolved cost |
|---|---|---|
| Device/state replacement | potentially retains game pass order/material logic | complete state/resource ABI; command-buffer bypasses; shader and special GPU support |
| Scene reconstruction | highest freedom to restructure rendering | scene, animation, materials, effects and special-pass reverse engineering |
| Selected native operations first | bounded validation and reversible integration | sharing resources correctly; may leave dominant waits untouched |

Do not copy Unleashed's implementation merely to avoid interface design, nor
replace our SDK with Skate's older fork. Local SDK CMake declares 0.10.0;
Skate README describes its fork as based on 0.8.0. Compatibility is unproven.

## 6. Desktop prototype proposal and gates

**Next read-only work:** expand the Fable device/resource ABI map around the
verified draw cluster, inspect callers/command-buffer variants, and inventory
representative shaders. Record sources/instructions, not guessed function names.
No further user action is needed for this source work.

Candidate first semantic experiment: one verified Fable texture blit/mip-generation
operation from `0x821FC4A0`'s path, rather than an entire scene or memexport rewrite.
This is a candidate, not a selected implementation or demonstrated speed win.
It exercises texture reads, target writes, layouts and downstream visibility using
existing imagery/mip evidence. It does **not** demonstrate scene rendering or fix
the Deck bottleneck by itself. Confirm exact formats/filtering/exponent/endian/
tiling/mip rules and object lifetimes before selecting it.

Proposed validation progression:

1. Isolated offline native rendering of representative inputs to a private image;
   compare pixels/mips with reference output and expected format behaviour.
2. Isolated desktop integration retaining all original rendering initially;
   measure instrumentation/duplicate-work overhead separately. This is not a bypass.
3. Only after identifying every side effect, replace that exact operation under an
   opt-in flag, publish its output correctly for original consumers, and verify
   omitted command work plus complete downstream correctness. No suppression by
   generic resolution, guessed pitch or shader hash alone.
4. Compare reference/native on same desktop: output correctness, frame pacing,
   CPU work, GPU timestamps, transfers and synchronization. Loading/streaming,
   camera movement, resize, closure and resource retirement need coverage.

If this operation cannot be isolated without first implementing a broad resource
adapter, stop and choose a better representative draw; do not force the candidate.
Synthetic tests or a triangle only prove their own scope. Preserve genuine query,
readback and fence semantics throughout. Failures after partial native writes
cannot safely fall back unless required resource/state restoration is defined.

### Workspace gate before any prototype build

- Full exact backups and independent game-closure checks per existing constraints.
- Dedicated branch/worktree, reviewed selection of current uncommitted changes,
  separate SDK source/output and generated code. A second CMake output directory
  alone is insufficient: root CMake :157–164 and generated/rexglue.cmake :117–120
  write generated files/stamps **in the source tree**.
- Separate settings/cache/save roots; verify content staging does not mutate
  shared assets. No hard links for mutable game data or saves.
- Audit SDK/app post-build staging: root CMake :112–119 copies plugin/runtime.
  Do not reuse desktop gameplay SDK outputs or run wrappers as research tooling.
- Establish actual desktop runtime/artifact/config baseline before comparing it.
  Source HEAD alone does not identify the installed/running build.

## Checkpoint

No branch/worktree, source implementation changes, build, game launch, Deck SSH,
trigger, shader-cache deletion, save operation, commit or push performed. Only
research evidence and documentation were written. No local models/subagents used.
Next phase remains source mapping, not production implementation. Licensing and
ABI unknowns do not block that read-only continuation.
