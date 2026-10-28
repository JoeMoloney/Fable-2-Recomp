# Town-Square GPU Payload Capture — Phase 1 Report

> **Current first-real-payload status:** no live constant payload was captured.
> The staged phase-1 bridge remains packaging/test code only; the SDK
> `UpdateBindings` path has not been modified, so acceptance criteria for a real
> payload are not met.

## Outcome

Phase 1 adds a thread-safe, bounded payload snapshot handoff in
`research/native_capture/payload_snapshot.py`, connected to the existing
content-addressed manifest/blob writer. It copies borrowed payload bytes at
handoff time and preserves resource ID, generation, guest address, source
register indices, representation and endian metadata. Output resource ordering
is deterministic; missing required kinds, duplicate identities, invalid
representations and byte-limit violations fail closed.

**Live SDK extraction is not yet implemented.** The runtime SDK patch still
captures metadata only. In particular, no claim is made that this phase has
recovered actual town-square index/vertex bytes or shader-visible constants.
The offline bridge and tests establish the safe packaging boundary, not a GPU
readback implementation. See Risks below.

## Payload contract

- Index and vertex payloads must identify guest address, byte length, resource
  generation and guest endian mode; retain original guest bytes unchanged.
- Shader constants must be the exact packed bytes bound to the draw, not merely
  an assumed register range. Include the source register indices/map and an
  explicit `host_shader_visible` representation marker.
- Snapshot providers must hold the relevant resource lifetime/synchronization
  until the byte copy is complete. The bridge immediately owns its copy.
- `bundle.py` remains the durable writer/verifier and supplies SHA-256 blob
  names, manifest validation and byte caps.

## Validation

The offline tests cover source-buffer lifetime after handoff, endian and source
register metadata retention, concurrent additions, stable output independent of
insertion order, missing required resources, sealed snapshots, invalid
representation and byte-limit rejection. The existing SDK selector/filter
tests run against a separate worktree based on pinned SDK revision
`521aa5906e8aa138870bf8d36bd602eef590ed55` with the existing metadata patch
applied. No game build or launch was performed.

Validation on the desktop Linux host:

- `bash research/native_capture/run-tests.sh /tmp/opencode/rexglue-sdk-payload-phase1`:
  C++ CTest **2/2 passed**; Python unittest **12/12 passed**. This uses a
  desktop CMake/Ninja test build against the isolated SDK worktree. It is not a
  full application build.
- `python3 -m py_compile research/native_capture/payload_snapshot.py
  research/native_capture/tests/test_payload_snapshot.py`: passed.
- Existing SDK checkout remains at the pinned base and unchanged (apart from
  its pre-existing `? thirdparty/fmt` status); the patch-applied worktree is
  separate. No gameplay checkout files were changed.

## Remaining risks and implementation blockers

1. **Guest memory versus Vulkan backing:** `memory_.TranslatePhysical` exposes
   the guest CPU backing, but Vulkan shared memory may contain newer GPU-written
   data. A safe runtime provider must use the SDK's range tracking/barriers and
   perform a bounded staging readback where the range is GPU-authoritative.
2. **Index transformation:** preserve raw guest index data separately from any
   host-converted index stream. Primitive conversion, endian swaps, reset
   indices, auto-index and tessellation can make host index bytes differ.
3. **Vertex fetch bounds:** fetch descriptors provide address and size in
   dwords, but the capture must validate physical bounds, capture exactly the
   shader-used descriptor ranges and identify changes/reuse by allocator
   generation, not address alone.
4. **Exact constants:** `UpdateBindings` packs system, sparse vertex/pixel float,
   bool/loop and fetch constants into Vulkan uniform buffers. The actual mapped
   bytes and their packed order must be captured after successful update and
   tied to the selected draw. Reading registers alone is insufficient evidence
   of the shader-visible buffer contents.
5. **Resource generations:** the current SDK abstractions do not expose a
   universally stable generation in the metadata hook. Until such generation
   is sourced from each allocator/cache, captures must fail closed rather than
   manufacture durable resource identity from address alone.

## Later phases

- **Textures:** snapshot translated shader-used fetches, every required mip,
  layer/face and plane, cache generation, format, tiling/swizzle and guest plus
  host representation. Use cache-aware staging/readback and reject incomplete
  subresources.
- **Render targets:** capture pre- and post-draw color/depth contents using
  explicit transfer staging, with attachment identity/generation, format,
  layout, sample count, viewport/scissor and resolve/load semantics.
- **EDRAM:** specify guest tile addressing and format conversion, capture the
  affected color/depth tiles and resolve state, and keep guest EDRAM semantics
  distinct from host render-target images.

All phases remain isolated SDK patches and desktop/offline validation only;
there is no Steam Deck deployment, production installation change, or Fable 2
launch authorized by this work.

## First-real-payload attempt and readiness

- Reviewed the staged phase-1 changes. They add a generic Python snapshot
  abstraction rather than direct C++ extraction and do not satisfy the new
  requirement to hook `UpdateBindings`. Their commit attempt was denied; they
  remain staged.
- Attempted a Release build from a clean detached application worktree at
  `fb9d2fb` using the pinned `localhost/fable2-deck-builder:arch-20261018`
  environment. The SDK and nested submodules were initialized at the pinned
  gitlink. CMake configuration then failed because the pinned image lacks the
  required `x11-xcb` and `wayland-client` development pkg-config modules. No
  executable was produced. This environment gap needs resolution in the pinned
  builder configuration before a complete experimental binary can be built.
- No SDK `UpdateBindings` extractor, payload-to-bundle runtime connection, or
  isolated Deck installation was produced. A Deck installation is not safe to
  prepare without a successfully built and dependency-audited artifact.
- No game launch, RenderDoc use, production-installation change, or save access
  occurred. The next gate is: add/test the direct packed-byte hook in an
  isolated SDK patch, repair the pinned builder's missing desktop development
  dependencies, build and audit the executable, then prepare an isolated
  non-RenderDoc install. The live test still requires explicit user approval.
