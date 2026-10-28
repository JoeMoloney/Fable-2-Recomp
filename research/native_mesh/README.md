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
