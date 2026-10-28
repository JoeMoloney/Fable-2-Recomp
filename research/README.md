# Native Vulkan development research

This tree is source and text evidence only. It intentionally excludes copyrighted
game banks/assets, extracted shader containers, runtime capture payloads, SPIR-V
and other binary fixtures, compiler archives, build caches, and rendered outputs.

- `native_mesh/`: isolated Vulkan geometry/material prototypes; visual fixture
  regression requires private local data not committed here.
- `native_offline/bank.py`: bounds-checked read-only shader-bank parser used by
  the catalogue/research scripts.
- `native_reference/`: hash-pinned XenosRecomp source subset, isolated HLSL
  emitter, reproducible local 1D descriptor correction and text-only result
  summaries. Building requires Clang, fmt and Python; compiling target shaders
  additionally requires local shader-bank/XSH inputs and DXC.
- `native_shader_catalogue/`: catalogue generator, input/output hash manifest,
  derived text catalogue and findings. The original bank/cache/draw-record inputs
  are deliberately not included; the manifest documents their identities.

All renderer integration and runtime Vulkan binding remain future work. In
particular, the proposed 1D descriptor lane/set 5 is not an existing runtime ABI.
