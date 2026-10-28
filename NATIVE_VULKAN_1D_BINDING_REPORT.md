# Native Vulkan 1D texture binding prototype

## Implementation

The prototype is isolated in `research/native_mesh/` and
`research/native_texture_1d/`; it does not modify or connect to live ReXGlue
rendering.

- Added `VariableDescriptorArray`, a reusable variable-count descriptor-array
  helper. The synthetic graphics pipeline preserves the Xenos set numbering:
  sets 0/1/2 are reserved for 2D/3D/cube sampled-image heaps, set 3 remains the
  sampler heap at binding 0, set 4 remains reserved, and the new 1D sampled-image
  heap is set 5/binding 0. Both test heaps use runtime descriptor arrays with two
  populated descriptors; partially-bound and update-after-bind are not enabled.
- Added `Context::image_1d` and optional variable-descriptor-count feature
  enabling. Existing `Context::init()` behavior remains the default; both
  pre-existing native renderer executables still build.
- Added `SharedConstants1D`: 512 bytes total, with the existing 0–287-byte prefix
  unchanged and a 16-entry 1D descriptor-index lane at bytes 288–351. The target
  standard PS register mapping is asserted and consumed: InScattering texture
  index 304 / sampler index 208 (sampler register 4); Extinction texture index
  308 / sampler index 212 (register 5). The shared buffer device address is
  passed in the existing 24-byte three-address push-constant structure.
- The SPIR-V uses separate sampled-image and sampler runtime arrays, an actual
  1D image view, and explicit bindings set 5/binding 0 and set 3/binding 0.
  `spirv-val` plus `verify_spirv_contract.py` check Vulkan 1.2 validity, runtime
  arrays, descriptor decorations, physical-storage-buffer addressing, push
  constant member offsets, and the four shared word indices.

## Synthetic GPU test results

`research/native_texture_1d/run-local.sh` builds with the pinned Arch builder
(Clang 23.1.3, Vulkan loader/glslang 1.4.363.0; glslang reports 16.6.0), validates
the embedded shader modules, and runs against each desktop ICD. Each test uploads
two four-texel `R8G8B8A8_UNORM` 1D textures, samples InScattering with linear
filtering at the midpoint between two texels and Extinction at texel centers
with nearest filtering, renders to a 4x1 `R32G32B32A32_SFLOAT` target, synchronizes,
and reads back all four pixels.

| Device | Result | Maximum pixel error | Key capability results |
|---|---|---:|---|
| NVIDIA GeForce RTX 3090, driver 615.78.08 | PASS | `7.62939e-06` | runtime arrays, variable count, descriptor indexing, dynamic sampled-image indexing and 1D sampled format supported |
| Intel UHD Graphics 630, Mesa 26.2.4 | PASS | `0` | same tested capabilities supported |

Both outputs match the synthetic expected RGBA values. SPIR-V hashes, toolchain,
ELF requirements and complete JSON results are in ignored
`out/deck-build/native-texture-1d/`. The smoke executable's ELF requirements are
GLIBC 2.34, GLIBCXX 3.4.32 and CXXABI 1.3.9, within the previously inspected
SteamOS host exports. The old `native_mesh` and `native_event362` targets also
recompiled successfully after the reusable `Context` changes.

## Steam Deck package

The locally validated `native_texture_1d` executable was packaged, checksum-
verified, and staged for approval-gated execution:

- Archive: `out/artifacts/fable2-experimental-3504dbcdea09.tar.gz`
- SHA-256: `975561d664f904464199e87c47e42913361d0e613af16a7cf91784088f0a9a2d`
- Source commit: `3504dbcdea090fa6a637c8672901086559298c82`, clean at package time.
- Executable SHA-256: `cfc6ae45c4abe3f239c18f9f7ecba09a1ac9eaf34b302885cc46da1c4d40557c`.
- The manifest also records the pinned builder image ID, glslang version, GLSL and
  SPIR-V hashes, ELF dependencies, ABI requirements, and the 288/304/308/208/212
  SharedConstants layout. Sidecar checksum verification passed.

After explicit approval, only the archive and sidecar were transferred to
`~/native-texture-1d-stage/`. Checksum, manifest, ELF dependencies and ABI were
checked before execution. The one Deck run forced the Radeon ICD and disabled
implicit layers; the existing Distrobox was not started.

| Deck RADV result | Value |
|---|---|
| Exit status / stderr | `0` / empty |
| Device | AMD Custom GPU 0932 (RADV VANGOGH), Mesa 26.2.0-devel (`git-5c65d4fc31`), Vulkan API 1.4.350 |
| Descriptor sets | sampler heap set 3/binding 0; 1D image heap set 5/binding 0 |
| Constants | 512 bytes; lane starts 288; InScattering index 304 / sampler 208; Extinction index 308 / sampler 212 |
| Readback | All 16 float components in the 4x1 output exactly match; max error `0` |
| 1D/descriptor features | 1D RGBA8 sampled image; runtime array, variable count, partially-bound, sampled-image nonuniform indexing and update-after-bind supported |
| Update-after-bind limits | 8,388,606 sampled images per stage and per set |

Returned JSON, stderr and exit status are in ignored
`out/deck-build/native-texture-1d/deck/`. JSON SHA-256 is
`dde2f4aa4b6fb7287a20d133b6279e898fb26e65c363a0eba99a5aa92aeb210e`; stderr is
empty (SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`);
exit-status file contains `0` (SHA-256
`9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa`). The Deck
staging directory is left intact for audit; no game or installed files were
modified.

## Semantic and integration limits

This test validates native Vulkan descriptor/constant plumbing and synthetic
sampling, not Xenos shader equivalence. The test uses explicit LOD 0, clamp
addressing, simple nearest/linear filters, RGBA8 UNORM data and no mip chain. The
target PS SPIR-V uses implicit-LOD samples; Xenos register/computed LOD, fetch
validity, coordinate denormalization, bias/offset, mip behavior, format and
swizzle conversion, and exact sampler semantics remain unverified. No texture
descriptor is bound by the game renderer, and this extension is not an existing
runtime ABI yet.

## Next step toward the town-square draw

Implement an explicit runtime binding/population rule for the set-5 1D heap and
SharedConstants lane, then run the exact observed standard-material VS/PS pair
against a complete matching draw fixture: geometry/index payloads, vertex-fetch
descriptors, full constants, texture/mip contents, sampler states, and render
state. Compare against an independent reference. The current metadata/shader
pair alone is not enough for that draw, and this prototype does not claim
town-square rendering parity.
