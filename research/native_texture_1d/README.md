# Synthetic Texture1D descriptor/SharedConstants prototype

This isolated graphics test exercises the proposed native Vulkan ABI without
Fable assets or ReXGlue integration. It uses the existing standalone
`research/native_mesh/context.h`, the reusable `VariableDescriptorArray`, and a
small 1D image helper. The SPIR-V declares runtime image and sampler arrays at
the existing sampler set 3/binding 0 and the new Texture1D set 5/binding 0.
Empty/placeholder set layouts keep the established set numbers 0–3 and reserve
set 4 rather than moving prior bindings.

`SharedConstants1D` preserves the first 288 bytes as an opaque legacy prefix and
adds 16 1D texture indices. The fixture writes the target material's values at
exact byte offsets: InScattering texture index 304, Extinction texture index
308, InScattering sampler index 208, and Extinction sampler index 212. The test
passes the SharedConstants buffer address through the existing 24-byte Xenos
push-constant address structure.

Run the complete standalone offscreen graphics test on desktop:

```sh
bash research/native_texture_1d/run-local.sh
```

The runner builds offline in the pinned Arch container, compiles and validates
the GLSL modules as Vulkan 1.2 SPIR-V, embeds those modules in the executable,
then runs separately against the desktop NVIDIA and Intel ICDs. It renders a
4x1 target, samples two known 1D RGBA8 textures using separate nearest/linear
samplers, copies the float target back, and checks every pixel. Results and
SPIR-V/ELF manifests are in ignored `out/deck-build/native-texture-1d/`.

The synthetic fixture validates descriptor numbering, variable descriptor-set
allocation, constant offsets, synchronization, graphics-pipeline creation and
sampling/readback. It does not claim Xenos `tfetch1D` semantic parity: Xenos LOD,
fetch validity, coordinate denormalization, sampler bias/offset, texture format
conversion, and any console-specific filtering behavior still need validation.
No game capture or renderer integration is involved.

The Deck package is produced only after local NVIDIA and Intel tests pass:

```sh
bash tools/deck-build/package.sh out/deck-build/native-texture-1d/native_texture_1d
```

It remains under ignored `out/artifacts/`. **Do not transfer or execute the Deck
package until the user explicitly approves.**
