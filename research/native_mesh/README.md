# Offline native Vulkan renderer prototype

This is a standalone, source-only Vulkan renderer prototype. It is not wired into
Fable-2-Recomp, the SDK plugin, or a game installation. It contains a measured
geometry-only path (`render_mesh.cpp`) and an experimental captured-material path
(`render_event362.cpp`) sharing low-level Vulkan resource/submission helpers
(`context.h`). The material path is incomplete: prior local measurements reported
coverage parity but not color parity. See the feasibility and shader notes under
`research/native_shader_catalogue/` before treating it as a renderer milestone.

## Build

Requires CMake, a C++17 compiler, the Vulkan loader/development package, and
`glslangValidator`. The project headers use the repository's `thirdparty/vulkan`
headers. It is deliberately independent of the game/plugin root build:

```sh
cmake -S research/native_mesh -B /tmp/native-mesh-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/native-mesh-build --parallel
```

The ignored local fixtures and captured payloads used for visual regression are
not part of this branch. No extracted game assets, captures, SPIR-V binaries,
build outputs, or rendered images are included. Do not run these binaries against
a game; they accept offline fixture files only.

## Town-square Event 34258 native replay (provisional)

The existing `native_event362` path was extended to load an unrebased sparse
512 MiB shared-memory snapshot, bind captured pixel-image slots, apply captured
blend factors, and load/read back an R16G16B16A16_SFLOAT target. Using the real
Event 34258 SPIR-V, 19,152 index bytes, captured constants, five textures, 17
fetch ranges and color/depth pre-state, the CachyOS RTX 3090 replay completed.
Its fixture is `/tmp/opencode/native-event34258-fixture-v2/`; outputs are under
`/tmp/opencode/native-event34258-run2/` and `run3/`. Native output hashes match
across runs (`8e25dd5e...f0cb2b`). The vertex/pixel modules are the exact
RenderDoc-captured Vulkan SPIR-V (SHA-256 `43f9dd90...11bbcce0` and
`d2b6f4d2...554782faf`), not independently converted guest Xenos shaders.

This is not yet an MSAA parity claim: the captured target is 2x MSAA, while the
standalone path currently renders single-sample from the extracted sample-0
pre-state. The sample-0 reference changed 26 pixels; native changed 27, with 8
reference-only and 9 native-only coverage pixels. The full-frame RGB MAE is
`6.55e-6`, but the small changed region and sample-count mismatch mean this is
only a first replay milestone, not a validated visible-material reconstruction.
Per-sample color/depth exports from the existing RDC are at
`/tmp/opencode/town-square-event34258-msaa-20261028-152702/`. Sample-0 reference
changes 26 color pixels; sample 1 changes 28. The pre-draw samples differ at
137,971 color pixels and 806,394 depth pixels, proving sample 0 cannot stand in
for the complete 2x pre-state. Next work must seed both color/depth samples and
read back both native samples before attributing residual color differences.
