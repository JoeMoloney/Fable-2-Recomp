# Town-square draw capture plan

**Scope/result:** offline evidence review only. No game launch, capture trigger,
RenderDoc invocation, source change in the gameplay checkout, Deck change, or
resource extraction was performed. Recommended path: extend the existing bounded
ReXGlue `DrawIdentityCapture` mechanism for one exact draw, rather than repair
RenderDoc first.

## Existing capabilities

### Runtime metadata capture

The retained runtime file
`research/native_shader_catalogue/input/draws-20261027T195223Z.jsonl` is an
existing Deck capture (SHA-256
`a644e1c24cb93e72f95dd85b28ba0e96097f5414e145b8e1f5d15cedae8eabcf`). Its header
reports 2,048 records, one dropped record, capacity truncation, a requested 3 s
window, and only frame 13098/submission 22983. It is a short slice, not a complete
frame capture.

It contains **64** draws with VS `79eac49585797037` + PS `401a01fd4e5f8757`. Of
these, 44 are 192-index, 16-bit triangle lists sharing the captured guest index
base and fetch descriptors 94/95. Their viewport/scissor is 1120×144. The
metadata is sufficient to target a draw family and has per-record ordinals, but
not a RenderDoc event ID or per-record timestamp. Among the 64 pair records there
are 20 complete signatures over index/fetch/texture/RT state (largest repeated
group: 18); the common geometry family has three RT-register variants and four
texture-fetch signatures. Thus shader hashes alone do not identify one object or
one complete state.

The experiment that generated the JSONL is on the Deck under
`~/Documents/fable2-native-shadow-live-draw-20261027T182808Z/source/`. Its
`DrawIdentityCapture` helper in
`thirdparty/rexglue-sdk/include/rex/graphics/vulkan/draw_identity_capture.h`
uses environment-selected output/trigger files, a worker thread, a fixed record
buffer, bounded duration/capacity and optional filtering. The capture code is
called from `VulkanCommandProcessor::IssueDraw` immediately before issuing the
draw. The current event filters cover event-349/362-like shapes or all draws, not
the requested shader pair. This code is **not present in the development
branch's SDK checkout**; the branch submodule is uninitialized at gitlink
`521aa5906e8aa138870bf8d36bd602eef590ed55`. Treat the Deck experiment source and
patch as provenance to recover/review, not as committed reusable source.

Current ReXGlue/Xenia code already exposes useful capture points:

- `VulkanCommandProcessor::IssueDraw` in SDK `src/graphics/vulkan/command_processor.cpp`
  has the active shaders, register file, `PrimitiveProcessor::ProcessingResult`,
  translated shader bindings, normalized pipeline setup, viewport/scissor, and
  the final `vkCmdDraw[Indexed]` point. It calls `RequestTextures`, updates
  constants/descriptors, and requests vertex-fetch ranges before the draw.
- `UpdateBindings` packs system, VS/PS float, bool/loop, and fetch constant
  buffers from the register file. `TextureCache::GetSamplerParameters` decodes
  fetch state, `UseSampler` obtains the Vulkan sampler, and texture-cache methods
  provide active image views and `TextureKey` metadata.
- `PrimitiveProcessor::Process` reports guest/host index format, endian, counts,
  buffer type and converted-index handle. `VulkanSharedMemory::RequestRange` and
  `buffer()` expose the shared guest-memory copy used by vertex fetch/index reads.
- `VulkanPipelineCache::GetCurrentStateDescription` creates the effective
  shader-modified pipeline description; `VulkanRenderTargetCache::Update` selects
  the active render-pass/framebuffer path. Existing `DumpRenderTargets` converts
  render-target data to EDRAM for resolves; it is not a per-draw file capture.
- `dump_shaders` can dump shader ucode, while shader-bank/XSH inventories already
  recover the target code: PS hash exactly matches bank slot 343; the VS is a
  verified fetch-patched slot-249 variant. Neither mechanism captures the draw's
  resource payloads.

### Retained offline references

- `renderdoc_captures/good-guest.rdc` exists (300,879,058 bytes; SHA-256
  `01f5dabfaa945d956ac1ded865e12f1edc9879df3cd4e12611ac8a0757500edf`) and has
  been replayed offline with qrenderdoc scripts.
- The retained RenderDoc-derived event-19831 fixture has real raw geometry but is
  a different shader pair and a geometry-only comparison. Event-349/362 fixtures
  contain material resources/constants but are also not established as the
  target pair. Existing evidence explicitly says `good-guest.rdc` is not proven
  to contain this town-square pair.
- The current runtime metadata stream has target-pair descriptors, not source
  bytes. It carries `descriptor_sets` and `host_pipeline` as process pointers;
  these are ephemeral handles, not stable resource identities or generations.

## Resource recovery status

| Required draw input | Already recoverable | Missing for a complete fixture |
|---|---|---|
| Shader code | Target PS exact bank slot 343; runtime VS bytes in retained XSH; translated SPIR-V candidates validated | Captured record itself has hashes/dword counts, not shader bytes or translation/modification identity; re-read and hash exact code in the capture manifest |
| Geometry/index | Runtime record gives index base/count/format/endian and fetch slots 94/95 words; `IssueDraw` has the primitive processor's host conversion metadata | Actual index bytes, fetch-backed vertex bytes, normalized host buffers, and per-range resource generations |
| Constants | `IssueDraw` has register maps and `UpdateBindings` constructs the shader-visible uniform data | Per-draw raw used registers and exact packed UBO bytes; previous event-362 constants are not this draw |
| Textures/mips | Runtime record includes ten used fetch constants (six raw words each); texture cache resolves `TextureKey`, image views and host formats after `RequestTextures` | Guest source bytes and/or exact host image mip/layer payloads, dimensions/formats/swizzles/endian, and resource identity/generation |
| Samplers | Raw texture fetch state is present; `GetSamplerParameters` and `UseSampler` already compute/acquire normalized sampling state | Per-draw normalized sampler parameters and mapping from each fetch slot to sampler/resource descriptor; opaque descriptor pointers are insufficient |
| Pipeline/targets | 23 render-target/state registers plus viewport/scissor; current pipeline and render-pass descriptions are available in SDK | Stable serialized pipeline description, target path (host RT vs interlock EDRAM), attachment pre-state and post-draw reference pixels/depth |
| Provenance | Shader identities, one-frame/submission, capture ordinal, bank/cache hashes are known | Game/app + SDK source revisions, selector, exact resource generation IDs, complete payload sizes/SHA-256s, status/drop counts |

The SDK's shared-memory page validity/watch mechanism and texture cache's
`outdated_mask` track coherence/invalidation, but neither the retained draw JSONL
nor inspected resource keys supplies a monotonic per-resource generation ID for
an offline bundle. The capture layer should assign capture-local IDs and bind
them to guest address/range plus a generation incremented on resource creation,
replacement, CPU invalidation or GPU write. Do not use Vulkan handles or guest
addresses alone as identity.

## Bounded capture design

1. Recover the existing `DrawIdentityCapture` source/patch into an **isolated SDK
   worktree** at the recorded gitlink; do not edit the original gameplay checkout
   or the Deck experiment tree. Keep its trigger-file, worker, fixed-buffer and
   default-disabled behavior.
2. Add an exact selector for VS/PS hashes
   `79eac49585797037` / `401a01fd4e5f8757`, primitive/index count/format, used
   fetch slots 94/95, texture mask and a selected full RT/texture state signature
   from the retained log. The 192-index candidate has index base `0x1beb1200`,
   vfetch words `94:[0x1be827c3,0x10000322]`,
   `95:[0x1be6ee43,0x100003ea]`, and texture mask `25335`. Do not silently broaden
   the selector if addresses/state differ; fail with `selector_not_found` and
   review a new candidate. Arm a first-match, one-record capture and immediately
   disarm after that draw.
3. Split capture collection across existing lifetime points:
   - At `IssueDraw`, after translation, texture/render-target update,
     `UpdateBindings`, and vertex residency requests, snapshot shader identity,
     selector state, all used fetch constants, register/pipeline state, source
     revision and resource-generation IDs.
   - At constant-buffer writes in `UpdateBindings`, copy the exact packed bytes
     and record the register map/source values.
   - At the selected draw, record source index/vertex ranges and the actual host
     index-buffer type/format. Copy the exact guest/shared-memory bytes or
     converted host data into bounded staging buffers, preserving both forms
     where conversion changes semantics.
   - Add a texture-cache capture API for the used bindings: `TextureKey`, host
     format/swizzle, per-mip/layer dimensions and bytes, plus normalized sampler
     parameters. Record generation IDs and fetch-slot-to-resource relationships.
   - After the draw, snapshot the active color/depth target path and pixels. For
     host render targets, copy selected attachment regions; for interlock EDRAM,
     capture the corresponding EDRAM span and decode it with the same format,
     pitch, MSAA and address rules. Include before and after target state when
     blending/depth depends on prior contents.
4. Bound the run to one matching draw, one output directory, fixed shader/fetch
   counts and a configurable aggregate payload cap (initial proposal: 256 MiB).
   Resource copy commands are queued only after a match; wait for the relevant
   submission fence only for this explicit capture. A worker serializes after
   completion. If any payload exceeds the cap, is incomplete, or cannot be
   synchronized, mark the capture failed and do not emit a “complete” fixture.
5. Write a manifest plus content-addressed blobs with schema/version, selector,
   source/game/SDK revisions, frame/submission/draw ordinal, shader hashes and
   bytes, resource IDs/generations, formats/layouts, constants and sampler state,
   target state, per-blob sizes/SHA-256, aggregate size, and explicit
   `complete`/drop/error status. A verifier must reject missing blobs, hash or
   size mismatch, selector ambiguity, resource-generation ambiguity, truncation,
   and a record count other than one.

Disabled path must perform no allocations, file I/O, GPU copies, fences, descriptor
updates, or resource reads. It should be a single cached disabled/armed check at
the draw hook. The old metadata collector's `descriptor_values_valid` masks and
JSON pointer handles are diagnostic only, not capture payloads.

## RenderDoc versus direct capture

**Recommend direct, opt-in capture at `VulkanCommandProcessor::IssueDraw`.** There
is already a successful bounded metadata-only capture path and a precise draw
hook with post-translation state. Extending it to payloads and an original-Xenia
output snapshot is more direct than repairing RenderDoc before capturing the
inputs. RenderDoc remains useful as a cross-check, not the first dependency.

Evidence against choosing RenderDoc repair first: the 2026-10-27 town-square
launcher attempt crashed in host `librenderdoc.so` inside the Distrobox; the
recorded stack repeatedly entered RenderDoc, but the core was root-only and could
not be symbolized. Cause is unproven. Existing `good-guest.rdc` replays offline,
but it is not proven to contain this exact pair. Repair would require core access
and a new same-scene capture, while direct draw metadata capture has already run.

Reference strategy: capture the **original Xenia backend's** selected-draw output
in the same one-shot run (before/after attachment/EDRAM snapshot), then compare
native replay against that baseline. This is independent of the new native path,
though it is not a second graphics implementation. A same-event RenderDoc replay
would be a useful independent cross-check if the startup path is later repaired;
existing unrelated events/screenshots are not a golden reference.

## Recommended next engineering assignment

Recover and rebase the Deck experiment's existing `DrawIdentityCapture` patch
into an isolated SDK worktree, then implement the target selector and capture
manifest/blob writer with synthetic payload fixtures. Do not build, stage or launch
the game for that assignment.

**Acceptance test:** feeding the retained JSONL to the selector unit test identifies
the 192-index target family and its full state signature; one-shot mode emits
exactly one complete record; synthetic index/vertex/constants/texture/target blobs
round-trip with matching SHA-256s and generations; disabled mode creates no files
and records nothing; size cap, changed-generation, missing-resource and truncation
fixtures fail closed. Only after this source-level acceptance passes should a
separate authorization be requested for one live target capture. A successful
bundle must contain all required resources plus an original-backend reference
before beginning native draw reconstruction.

## Scope and preservation

All current work was read-only desktop analysis plus this report. The development
worktree and original `Fable-2-Recomp`/SDK checkout were not modified. No game
launch, Deck deployment, RenderDoc investigation, capture trigger, build, or
production change occurred.
