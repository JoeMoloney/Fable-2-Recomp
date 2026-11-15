# Fable II native renderer — feasibility investigation

Recorded 2026-10-26. Status: agreed research direction, not an approved renderer
rewrite or an established performance improvement.

**Latest update:** user approved the isolated offline prototype and optional local
`bonsai-2:27b` assistance. See `NATIVE_RENDERER_OFFLINE_PROTOTYPE.md` for the current
authorization, selected shader/texture contract and fresh-session handoff. Earlier
read-only/no-agent restrictions below describe the investigation phase; they no
longer prohibit that narrowly scoped offline prototype. Gameplay/Deck protections
and no in-game implementation, commits or cleanup remain in force.

## User decision and boundaries

- **Desktop first (user update 2026-10-26):** target the first working native
  rendering baseline on this desktop, then refine correctness/performance before
  porting or tuning on Deck. Use an isolated research build, settings and saves;
  this does not authorize changing the existing desktop gameplay installation or
  launching/building before the read-only feasibility audit and prototype review.
  Compare against the existing renderer on the same desktop under controlled
  conditions. Desktop results do not establish Deck performance.
- Investigate a game-specific native graphics path, retaining the working CPU
  recompilation/runtime and other useful components of this repository.
- User favours an isolated branch and eventual merge if successful. Recommended:
  a separate worktree after inspecting existing parent/SDK modifications and
  deciding which uncommitted dependencies must be carried over. No research branch
  or worktree has been created yet. Do not assume uncommitted changes follow it.
- Keep the existing Xenos backend as a correctness reference and fallback until
  native coverage is demonstrated. Remove obsolete infrastructure only in a later,
  separately reviewed cleanup; do not discard experiments or evidence.
- **Do not use local models or subagents until the user explicitly requests them
  again.** This supersedes earlier Bonsai delegation preferences for current work.
- Begin with read-only source investigation. No production edits, builds, SDK
  replacement, game launches, capture triggers, commits or pushes implied.
- Deck v2 diagnostic remains staged and on hold. Follow
  `docs/analysis/PERF_INVESTIGATIONS_OCT_NOV_2026.md` if returning to that experiment; user reports
  Deck online, but this session has not independently checked its live state.
- Preserve desktop gameplay, settings, saves and current uncommitted parent/SDK
  work. Research builds/settings/saves must be isolated if later authorized.

## Reference checkouts and initial evidence

User cloned:

- `/home/ol-randals/HDD/git_stuff/skate3recomp`
  — https://github.com/mchughalex/skate3recomp
- `/home/ol-randals/HDD/git_stuff/UnleashedRecomp`
  — https://github.com/hedge-dev/UnleashedRecomp

Initial inspection only; pin exact revisions before detailed findings.

### Skate 3

- `src/skate3_native_render.h` describes link-time weak-symbol guest-function
  overrides gated by a native-renderer cvar.
- `src/skate3_native_scene.h` describes capturing MeshContext streams, walking
  verified game structures on the game thread into NativeFrameScene, and drawing
  into the guest output texture via an SDK native-guest-output hook.
- Thus this is not merely a generic Xbox graphics API replacement: significant
  scene extraction, materials, entity and animation handling are game-specific.
- `.gitmodules` pins `third_party/rexglue-sdk` to the Skate fork
  https://github.com/mchughalex/rexglue-skate3.git (branch skate3-sdk-clean).
  That submodule directory was empty at initial inspection. Inspect its pinned
  commit/backend changes before claiming integration code is reusable. No
  submodules fetched or initialized by this session.
- README documents native D3D12/Vulkan, emulated fallback/live switching, known
  visual issues, and speed/power improvements. These are upstream statements,
  not independently reproduced measurements or Fable II forecasts.
- Initial license filename scan found no LICENSE-named file. Reuse permission is
  unresolved, not proof the project has no license; inspect headers and other
  notices before copying code.

### Unleashed

- README documents a custom renderer, bindless textures, shader specialization,
  parallel transfers and pipeline preparation integrated with asset loading.
- Root `COPYING` contains GPLv3. Check file-specific/dependency terms and our
  distribution obligations before code reuse; do not assume all code shares one
  license or that architectural study requires copying code.
- Related shader converter https://github.com/hedge-dev/XenosRecomp converts
  Xenos shaders to HLSL/DXIL/SPIR-V. Its upstream README explicitly lists memory
  export as unimplemented and other game-specific/incomplete features. This was
  checked online, not verified against a pinned local converter implementation.

## Fable II context / avoid false conclusions

- Current CPU code is statically recompiled; graphics still use a Xenia-derived
  Xenos backend translating to Vulkan. Native Vulkan alone is not the proposed
  improvement; bypassing unnecessary guest GPU machinery is the opportunity.
- Measured fallback CPU waits (~35–39 ms) are waits for queued GPU work, not
  shader execution time or guaranteed recoverable frame time.
- GPU consumers of measured memexport output were identified; guest PPC CPU
  readers and freshness requirements remain unresolved. No skipped readbacks,
  stale results, fake query results or speculative synchronization changes.
- Sonic's shader converter is not a drop-in memexport solution. A Fable-specific
  implementation/replacement must preserve all observable dependencies.

## Investigation work packages

1. **Baseline and licenses:** record parent/SDK/reference revisions and dirty
   state; map SDK dependency differences and reuse permissions. Do not modify
   existing checkouts to obtain a cleaner baseline.
2. **Reference architecture:** trace a frame in each project from guest hooks
   through resources/shaders, command submission, synchronization and presentation.
   Identify exact native/emulated boundary, fallback limitations, and generic
   versus game-specific code. Cite paths/symbols/revisions for claims.
3. **Fable boundary map:** identify resource creation/update/destruction,
   draw submission, render-target changes, material/shader binding, streaming,
   lifetime, memexport, adaptive tessellation and CPU-visible GPU results.
   Distinguish verified boundaries from hypotheses and missing information.
4. **Architecture comparison:** compare graphics API/device interception,
   engine-scene extraction, and targeted native paths. Include mixed-path
   ownership/synchronization costs, not just expected advantages.
5. **Prototype proposal, not automatic implementation:** select one representative
   Fable operation that genuinely bypasses a Xenos path; specify correctness,
   resource lifetime and synchronization requirements, visual comparison,
   CPU/GPU/frame-pacing metrics and isolated fallback/test procedure. A triangle
   proves plumbing only, not migration feasibility.

## Deliverable and decision gate

Produce a source-backed feasibility report with a reuse matrix, Fable-specific
unknowns, licensing constraints, recommended interception level, staged milestones
and the smallest useful proof of concept. Do not promise an effort estimate or
speedup without evidence.

Proceed to implementation only after reviewing whether the interception boundary
is usable, special GPU behaviour can be preserved, reuse is permissible, and the
proposed test can measure a meaningful bottleneck. Later prototype results must
separate visual correctness, dependency correctness, performance and coverage.

Potential migration milestones (subject to findings): integration/resources and
shaders; representative rendering; broader scene coverage; effects/UI; parity,
streaming and performance validation; eventual separately reviewed legacy cleanup.

## Next action

Initial bounded source audit completed: see `NATIVE_RENDERER_SOURCE_AUDIT.md`
for exact revisions, frame paths, licensing/reuse limits, important Skate query/
memexport caveats, initial Fable address map and desktop prototype proposal.
Evidence: `out/performance/native-renderer-feasibility-20261026/`.
Follow-up `NATIVE_RENDERER_FABLE_BOUNDARIES.md` records resource/device footholds,
a second draw emitter, converter contracts and a validated limited cache census.
Follow-up resolve/draw/container mapping and selected vertex-shader reconstruction
are recorded in `NATIVE_RENDERER_OFFLINE_PROTOTYPE.md`. The selected offline test
is now approved; full renderer feasibility and in-game publication remain unresolved.
No branch/worktree or prototype has been created yet, and no delegation has run.
