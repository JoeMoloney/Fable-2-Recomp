# Bounded draw-capture infrastructure

This source-only tooling recovers the existing opt-in `DrawIdentityCapture`
observer from the Deck experiment, adds an exact target selector/one-shot stop,
and provides an offline content-addressed bundle writer. The original
`Fable-2-Recomp` SDK checkout and submodule gitlink are not modified.

## Recovered source and SDK patch

- `upstream/draw_identity_capture.h` and `upstream/tests/draw_identity_*` are the exact
  source files read from
  `fable2-native-shadow-live-draw-20261027T182808Z`; their original SHA-256s and
  the pinned SDK base are in `provenance.json`.
- `patches/rexglue-draw-identity.patch` is the minimal integration patch for SDK
  commit `521aa5906e8aa138870bf8d36bd602eef590ed55`: add the capture header,
  initialize it from explicit environment opt-in, and call the metadata observer
  at `VulkanCommandProcessor::IssueDraw` before the Vulkan draw command.
- `apply-sdk-patch.sh` refuses a wrong-base or dirty SDK tree. Apply/test only in
  a separate SDK worktree, never the original gameplay checkout.

The patch is pinned to SDK base `521aa5906e8aa138870bf8d36bd602eef590ed55` and
hash-recorded in `provenance.json`. It includes only the capture header and its
small `VulkanCommandProcessor::IssueDraw` integration; unrelated Deck experiment
timing/provenance patches were excluded. No SDK gitlink change or SDK-only commit
was made.

The `town_square_standard_material_v1` selector requires the exact VS/PS pair,
192-index geometry, guest index descriptor, vertex-fetch 94/95 words, texture
mask, and a state signature over the full fetch/RT/viewport/scissor state. It
captures the first matching draw only and reports its run-local ordinal. The
retained sample has 18 identical full signatures in one short window, so this is
not an object-name or stable cross-run draw ID.

## Offline selection and payload-bundle tests

`selector-standard-material-v1.json` is a compact selector fixture derived from
the retained draw-identity JSONL; the JSONL itself is not copied here.
`selector.py` can be run against that local input when available. `bundle.py`
creates/verifies one-draw, versioned manifests and SHA-256-addressed blobs from
synthetic or captured payloads. It rejects dropped/incomplete captures,
duplicate resource identities, invalid generations, missing/changed blobs,
multi-draw bundles, and payloads over configured byte limits.

`payload_snapshot.py` provides a bounded, thread-safe handoff for immutable
snapshots from a runtime provider to `bundle.py`: it eagerly copies borrowed
bytes, retains generation, guest/host representation, endian mode and source
register indices, rejects missing required kinds, and orders resources
deterministically. It does **not** synchronize Vulkan memory or discover live
resources; its synthetic tests are not proof of GPU readback correctness.

```sh
python3 -m unittest discover -s research/native_capture/tests -v
python3 research/native_capture/selector.py \
  /path/to/retained/draws-20261027T195223Z.jsonl
```

## Capture boundary and limitations

The SDK hook currently captures **metadata only**. The payload handoff validates
and retains bytes after a provider supplies them, but no SDK provider is wired
to index/vertex memory or shader-visible constant buffers. A Vulkan-safe
implementation must snapshot at the selected draw's resource-use point,
synchronize GPU-written ranges, and attach allocator-backed resource
generations; CPU guest RAM alone is not evidence of Vulkan shared-memory
contents.

Subsequent texture phase: enumerate translated fetches/subresources, pin the
texture-cache generation, copy each required mip/layer (and tiled/swizzled
guest representation where applicable), preserve format/swizzle/plane
metadata, and fail if any requested subresource is unavailable. Render-target
phase: capture pre/post draw color and depth attachments through explicit
transfer-compatible staging and record format, layout, sample count, resolve
state, viewport and clear/load semantics. EDRAM phase: define an explicit
guest-tile/format mapping and capture relevant EDRAM-backed color/depth tiles
plus resolve state; do not substitute host render-target pixels for guest EDRAM
semantics. Each phase needs independent synchronization and round-trip fixtures
before its resource kinds become required in a bundle.
