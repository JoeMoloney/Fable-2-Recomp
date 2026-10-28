# Desktop-to-Steam-Deck build pipeline report

**Scope:** established and tested a pinned desktop Arch build container and a
minimal C++ ABI probe on the CachyOS desktop. No full Fable 2 build, game launch,
Deck deployment, installation change, merge, or push was performed. All build
outputs are under ignored `out/`.

## Verified environment

- Desktop: CachyOS x86-64, Linux `7.3.0-3-cachyos`; Intel Core i9-10900K,
  10 physical cores / 20 threads; host glibc 2.44; GCC/libstdc++ 16.2.1; Podman
  rootless OCI runtime available. Required x86-64-v2 CPU flags were observed:
  SSE3/SSSE3, SSE4.1/4.2, CMPXCHG16B (`cx16`), POPCNT and LAHF/SAHF.
- The retained Deck build report identifies the base as
  `docker.io/library/archlinux@sha256:dfb01267da058e5d08ffdc543bf484c9b404655b0bac1d0201d972bf950cdfd0`,
  image config `c22c3194d74cdee63a036f904175005556c8b5c3044ed268e059438e2bc60bfd`.
  The exact digest was pulled and inspected on desktop. It identifies Arch build
  `20261018.0.612668`, created 2026-10-21, `linux/amd64`. A rootless Podman
  builder was built from that immutable base using the matching 2026-10-18 Arch
  Archive snapshot.
- Builder image: `localhost/fable2-deck-builder:arch-20261018`, local image ID
  `0b5bcd01baa0a4f1fb101c0132e71cc970dab68dfc87a65fe9a3a02a2b32f03e`.
  Re-running the image build used cached layers. Installed package versions
  matched the retained Deck handoff: Clang `23.1.3-3`, CMake `4.4.4-1`, Ninja
  `1.13.2-3`; lld `23.1.3-3`. The pinned base has glibc
  `2.44+r50+g1848099f063e-1` and gcc-libs `16.2.1+r23+gd564253eb6c8-1`.
  The package inventory is recorded locally in ignored
  `out/deck-build/container-manifest.txt`.
- Read-only SSH to `deck@192.168.1.54` succeeded. The SteamOS host reports glibc
  `2.43+r37+gfdf10644d6ee-1`, gcc-libs `16.1.1+r346+g4e03491b401d-4`, Mesa
  `26.1.2.221562.radeonsi_26.1.2-2.1`, Vulkan RADV package
  `26.2.0_devel.222147.steamos_26.05.19-1`, and Vulkan loader/tools
  `1.4.350.1-1`. This confirms the prior retained host-version notes.
- Deck `libstdc++.so.6` resolves to `libstdc++.so.6.0.35`; its ELF version
  definitions export through GLIBCXX `3.4.35` and CXXABI `1.3.17`. Deck libc
  exports through GLIBC `2.43`. The installed dynamic loader path
  `/lib64/ld-linux-x86-64.so.2` and standard dependencies are present.
- The Deck's `fable2-build` Distrobox is **stopped** (`Exited (143)`), based on
  image config `c22c3194...`, matching the exact pinned Arch base digest used on
  desktop. Read-only inspect showed `network=host`, `userns=private`, `/dev` and
  `/sys` mounts, and the host root mounted at `/run/host`; no `LD_LIBRARY_PATH`
  override was present in its configured environment. The container was not
  started. Its mounted host-root means container commands must still avoid
  accidentally resolving host libraries.

## ABI probe results

`tools/deck-build/abi-probe/` builds one executable and one shared C++ library
with `-march=x86-64-v2`. CMake/Ninja compiled both with the pinned container
toolchain. CTest passed **1/1**, the executable ran successfully inside the Arch
container, and it also ran successfully on the CachyOS host. The executable has
`$ORIGIN`-only RUNPATH so its adjacent shared library is found without a build
directory path.

ELF inspection (`readelf`, plus `ldd -v` inside the builder) verified:

| Object | ELF / interpreter | Direct dependencies | Maximum requested versions |
|---|---|---|---|
| ABI executable | ELF64 x86-64 PIE; `/lib64/ld-linux-x86-64.so.2` | probe library, `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6` | GLIBC 2.34; GLIBCXX 3.4.32 |
| Probe shared library | ELF64 x86-64 shared object | `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6` | GLIBC 2.14; GLIBCXX 3.4; CXXABI 1.3.9 |

The builder's transitive runtime libraries require at most GLIBC 2.38 in the
captured `ldd -v` output. The live Deck exports GLIBC 2.43, GLIBCXX 3.4.35 and
CXXABI 1.3.17, so the probe's GLIBC 2.34, GLIBCXX 3.4.32 and CXXABI 1.3.9
requirements fit the **observed** Deck host symbol versions. Its direct system
dependencies are installed on Deck, and the probe shared library is packaged
beside the executable with `$ORIGIN` RUNPATH. Thus the ABI probe is a strong
candidate for direct SteamOS-host execution. It was not copied to or executed on
Deck, so actual loader resolution remains untested. Do not generalize this result
to a future Fable binary without inspecting its complete ELF dependency closure.

The builder's own glibc 2.44 and gcc-libs 16.2.1 are newer than the live Deck
host. The ABI probe happens to need older symbols and appears host-compatible;
other desktop-built artifacts may not. Package metadata now records the observed
host exports (GLIBC 2.43, GLIBCXX 3.4.35, CXXABI 1.3.17), and the ABI test checks
all three. An artifact exceeding those exports requires a compatible sysroot or
execution inside the matching Arch container, whose Deck-side execution has not
been tested.

## Scripts and artifacts

- `tools/deck-build/Containerfile`, `toolchain.lock`, `build-image.sh`: immutable
  base digest, date-pinned Arch package repositories, exact tool version checks,
  and local image/package inventory. The scripts do not silently follow a new
  `archlinux:latest`.
- `tools/deck-build/abi-test.sh`, `abi-probe/`, `inspect_elf.py`: executable and
  shared-library probe, runtime test, ELF dependencies, interpreter, runpath,
  and GLIBC/GLIBCXX/CXXABI requirement inspection.
- `tools/deck-build/build.sh`: reusable network-disabled CMake/Ninja build entry
  point with `-march=x86-64-v2`, persistent ignored output for incremental
  compilation, and a RAM/CPU-aware default capped at 12 workers. The full-project
  build wrapper was **not run** under this assignment.
- `tools/deck-build/package.sh`: creates a deterministic archive under ignored
  `out/artifacts/`, with source commit, builder image identity, package/toolchain
  inventory, ELF dependency/version data, per-file SHA-256 and an archive checksum.
  It refuses a dirty tree unless explicitly overridden.
- `tools/deck-build/README.md`: build use, isolation and future transfer guidance.

The ABI executable/library and their readelf/CTest/`ldd -v` logs are local
ignored test outputs under `out/deck-build/abi-probe/`; they are not committed.
The first clean-tree packaging run on source commit
`e304f8dd492e3512fde022d939c007b2e9142091` produced
`out/artifacts/fable2-experimental-e304f8dd492e.tar.gz` with checksum
`ef2b28b475b85f300d16bc38406d7488a0c8f91b32b4cbd650161fe6dc9589ea`. The embedded
manifest records `source_dirty: false`, source commit, builder image ID, package
inventory, per-ELF SHA-256, `DT_NEEDED`, `$ORIGIN` RUNPATH and symbol requirements.
A second package build with a distinct output tag is being used to verify byte-
for-byte reproducibility; both archives remain ignored build outputs.

## Deck transfer and Vulkan/RADV boundary

No transfer is authorized or implemented here. For a later approved experiment,
copy only a checksum-verified archive into a new, dedicated staging directory on
the Deck; verify its `.sha256` and inspect the archive/manifest before extracting.
Do not overwrite an installed executable or plugin. If the artifact needs the
container’s newer userspace, separately export/load the exact builder image and
run from a matching Deck container, not from the host filesystem runtime. Any
execution or Vulkan test still requires separate authorization.

The ABI probe does not load Vulkan. Read-only Deck `vulkaninfo --summary` reported
loader 1.4.350, RADV driver 26.1.99 / Mesa `26.2.0-devel` on AMD Custom GPU 0932
(RADV VANGOGH), and enumerated the device without a game. The active ICD JSON
`/usr/share/vulkan/icd.d/radeon_icd.x86_64.json` loads `/usr/lib/libvulkan_radeon.so`
and advertises API 1.4.350. Installed loader and RADV libraries resolve through
`ldconfig`. `vulkaninfo` also reported a duplicate 32-bit RenderDoc layer and an
LSFG layer `vkGetInstanceProcAddr` initialization error; it skipped the broken
layer and still enumerated RADV. These are relevant host-layer warnings, not a
render test. The stopped container's `/dev` and host-root mounts suggest device
visibility but do not establish Vulkan access from inside that container.

No Vulkan rendering, WSI, shader-pipeline, extension-feature parity, or RADV
execution test was performed. A Deck runtime must use the host Vulkan loader/ICD
and must not carry desktop NVIDIA driver libraries. Vulkan/RADV renderer
compatibility remains unverified.

## Remaining blockers and next smallest step

- The desktop ABI probe's symbol requirements fit the live Deck host exports,
  but direct Deck execution was intentionally not attempted. No full Fable binary
  has been built or ABI-audited.
- The Arch builder image digest matches the stopped Deck Distrobox base, but the
  derived desktop builder image was not exported/imported and run on Deck.
- Package reproducibility remains under test via two output tags with normalized
  archive paths/timestamps; no artifact is deployed.
- The next **smallest validation step** is to compare both package SHA-256 values
  from clean builds; a later separately authorized step can run only the harmless
  ABI probe on Deck, followed by isolated RADV smoke testing.
