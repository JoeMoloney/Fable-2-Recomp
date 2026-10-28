# Draw-capture infrastructure recovery and bounded selector

## Result

Recovered the existing opt-in `DrawIdentityCapture` source and its SDK hook,
added a fail-closed selector for the measured town-square draw family, and added
an offline versioned capture-bundle writer with content-addressed SHA-256 blobs.
All source/patch/test material is tracked on `feature/native-vulkan-renderer`;
there is no SDK-only commit and the parent SDK gitlink remains unchanged.

No game build or launch, capture trigger, RenderDoc operation, Deck deployment or
production source change was performed. A temporary detached SDK worktree at the
recorded base was used to apply/test the patch and then removed.

## Recovered runtime instrumentation and target evidence

- SDK base/gitlink: `521aa5906e8aa138870bf8d36bd602eef590ed55`.
- Recovered Deck experiment source:
  `~/Documents/fable2-native-shadow-live-draw-20261027T182808Z/source/thirdparty/rexglue-sdk/`.
  The original header SHA-256 is
  `95187fbb01ee7989f06802c01013b14f2bdc6ae413d2c16036fd5fb78f4a7a66`; its
  source/test provenance is in `research/native_capture/provenance.json`.
- The existing observer is environment-opt-in, trigger-file armed, bounded, and
  uses a worker to write JSONL after collection. The recovered original used a
  large inline 4,096-record array; the tracked patch reserves a bounded vector
  only after the trigger. Its `IssueDraw`
  hook records metadata only and performs no guest-memory reads or Vulkan copies.
  Its prior selector handled `all` and event-349/362 signatures, not the target
  pair. It serialized host pipeline/descriptor handles as process pointers, not
  stable resource identities.
- Retained draw input SHA-256:
  `a644e1c24cb93e72f95dd85b28ba0e96097f5414e145b8e1f5d15cedae8eabcf`. Header:
  2,048 records, one dropped, capacity-truncated, one frame/submission. There
  are 64 target-pair rows; 44 use 192 16-bit triangle-list indices and share
  vfetch slots 94/95. Twenty full index/fetch/texture/RT signatures occur, with
  the largest identical group repeated 18 times. Therefore shader hash alone,
  and even the common geometry signature, does not identify an object instance.
- The new selector requires VS/PS hashes, primitive/index format/count/base,
  vfetch 94/95 words, texture mask, and a state fingerprint over all used fetch
  words, RT registers, viewport and scissor. The selected retained state has
  FNV-1a-64 signature `2cbe39913b642d9e`; FNV is only a selector fingerprint,
  not an integrity hash. The collector now has separate capture-record ordinal
  and run-local draw ordinal counters; the latter advances across all draws
  while armed, before selector filtering.
  It stops on the first exact match (`max_records=1`, `one_shot_complete`). The
  selected sample's full signature is repeated 18 times, so the run-local draw
  ordinal is retained and no scene-object name is inferred.

## What is and is not recoverable

| Input | Existing evidence/capability | Still missing |
|---|---|---|
| Shaders | PS `401a01fd4e5f8757` matches bank slot 343; VS `79eac49585797037` is available in retained XSH and verified as a fetch-patched slot-249 variant | New one-draw bundle should re-record/hash exact bytes and shader modifications/provenance |
| Geometry/index | JSONL has index address/count/format/endian and fetch descriptors; SDK `PrimitiveProcessor::Process` exposes the host index-buffer type/handle | Raw index bytes, used vertex-fetch bytes, and converted host-index bytes |
| Constants | SDK `RegisterFile`, shader constant maps, `UpdateSystemConstantValues` and `UpdateBindings` expose source registers and packed UBO construction | Per-draw raw used registers and exact shader-visible constant-buffer bytes |
| Textures/samplers | JSONL has raw six-word fetch descriptors; `VulkanTextureCache::RequestTextures`, `GetSamplerParameters`, `UseSampler`, `TextureKey`, and `VulkanTexture::image()` expose resolved state/resources | Image mip/layer payloads, format/swizzle/endian metadata, normalized sampler parameters tied to resource generations |
| Targets/pipeline | JSONL records 23 selected RT/state registers plus viewport/scissor; SDK exposes effective `PipelineDescription`, render-pass key and target path | Complete pre-draw attachments/depth, exact host/EDRAM state and post-draw reference pixels |
| Identity/provenance | Shader hash, frame/submission and capture ordinal are retained | Resource generation IDs, stable capture-local resource IDs and per-blob SHA-256/size manifest |

The shared-memory API tracks page validity and CPU/GPU invalidation watches, and
the texture cache tracks outdated base/mip flags, but no capture-local monotonic
resource-generation ID was found. A capture must add one; Vulkan handles or guest
addresses alone are not durable identities.

## Patch and validation

- `research/native_capture/patches/rexglue-draw-identity.patch` (SHA-256
  `7ee7833a833a6986ac16ee2fc8f3d1d1f27f88d48823ed8baad83bc2ad4ef1c1`) adds the
  recovered header and minimal integration at
  `VulkanCommandProcessor::IssueDraw`. It applies cleanly to the pinned SDK base.
  `apply-sdk-patch.sh` refuses a different/dirty SDK worktree. No gitlink update
  was made.
- The metadata hook is disabled unless log and trigger paths are configured. The
  disabled `IssueDraw` path is one active-state atomic check; record storage is
  not allocated until the trigger. It makes no locks, file writes, guest reads,
  or GPU commands while disabled. A unit test checks disabled capacity is zero;
  no timing benchmark was run.
- `town_square_standard_material_v1` reads expected VS hash, PS hash and state
  fingerprint from environment, verifies the exact structural selector, and
  captures one match. Unit tests check mismatch rejection, one-shot finalization,
  draw ordinal and no drops.
- `research/native_capture/bundle.py` writes schema-1 manifests and
  `blobs/<sha256>.bin`, deduplicates identical bytes, and records resource ID,
  generation, guest address, kind and metadata. It fails closed on non-complete
  or multi-draw input, drops, missing required resource kinds, duplicate IDs,
  invalid generations, unsafe paths, oversized payloads, and hash/size mismatch.
- Validation: clean isolated SDK-worktree patch application; CTest **2/2** C++
  capture-filter/selector tests; Python **8/8** synthetic selector/bundle tests;
  offline selector run against the retained trace found 18 exact-state matches
  (first sample ordinal 1641). A Clang 23.1.3 `-fsyntax-only` check of the
  patched `VulkanCommandProcessor.cpp` passed with 22 existing SDK/header
  warnings and no errors. No full SDK or Fable build was run.

## RenderDoc vs direct capture / reference

**Direct runtime capture is the recommended next path.** The metadata observer
has already run successfully and its hook is at the exact Vulkan draw boundary.
The existing `good-guest.rdc` is replayable offline but is not proven to contain
this shader pair. The retained Deck RenderDoc launch crashed recursively in
`librenderdoc.so`; its core was root-only and could not be symbolized. Repairing
that path is uncertain and still would not provide missing live payloads without
a matching capture.

For an eventual independent native comparison, capture the original Xenia
backend's target output at the selected draw: pre-draw color/depth inputs and
post-draw output from the active host-render-target path, or the corresponding
EDRAM span plus format/pitch/MSAA metadata for the interlock path. This is an
oracle independent of the new native implementation, though not an independent
graphics implementation. A matching RenderDoc replay can later cross-check it.

## GPU synchronization/readback gate

Payload extraction is **not implemented**. Before doing it, map the selected
resource paths and layouts. Vertex/index ranges reside in shared memory and may
be GPU-written; texture images may be in shader-read layouts and carry mips,
layers and host swizzles; render targets may be Vulkan images or interlock EDRAM
storage. The one-shot capture must enqueue bounded transfer copies in correct
queue order, transition/restore image layouts, fence the capture submission,
invalidate noncoherent readback memory, and only then let the worker hash/write
blobs. It must not snapshot a reused address after its resource generation
changes. If any required payload is unavailable or exceeds the byte cap, mark the
bundle incomplete and do not present it as a usable fixture.

## Next engineering assignment / acceptance

Implement the one-shot GPU payload readback and pre/post target snapshot on the
isolated SDK patch, reusing the selector and bundle writer. Acceptance: a
synthetic test proves ordering/layout restoration and all bundle hashes; an
offline verifier proves exactly one complete, generation-tagged target record;
then, after separate authorization, one bounded live capture produces all
required blobs and a reproducible original-backend reference. No native draw or
runtime renderer integration should start before that fixture verifies complete.
