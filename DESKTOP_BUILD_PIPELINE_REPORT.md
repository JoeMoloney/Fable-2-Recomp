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
- The retained Deck audit reports host SteamOS glibc 2.43 and gcc-libs
  `16.1.1+r346`, and a stopped `fable2-build` Distrobox based on this Arch image.
  These are **historical retained observations**, not a live recheck: read-only
  `ssh deck` failed because the hostname did not resolve during this task.

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

The builder’s transitive runtime libraries request at most GLIBC 2.38 in the
captured `ldd -v` output. These ABI requirements are below the retained Deck host
glibc 2.43 observation and below the GLIBCXX 3.4.36 requirement seen in the
retained existing Deck-compatible executable. This is encouraging evidence for
the probe, **not a complete Deck compatibility certification**: the Deck was
unreachable, its exact `libstdc++.so.6` exports were not inspected, and its
CXXABI export set is unknown. Do not infer all future Fable binaries will have
the same low symbol requirements.

The container’s own glibc 2.44 and gcc-libs 16.2.1 are newer than the reported
Deck host. Thus container compilation alone does not prove a binary can run
directly on the Deck host. Keep host-linked artifacts below verified host symbol
ceilings, or run them inside the exact container runtime on Deck after separately
validating that runtime setup.

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
The packaging script was then run on the clean source commit
`e304f8dd492e3512fde022d939c007b2e9142091` using the ABI probe pair. It produced
`out/artifacts/fable2-experimental-e304f8dd492e.tar.gz`; its checksum verified as
`ef2b28b475b85f300d16bc38406d7488a0c8f91b32b4cbd650161fe6dc9589ea`. The
embedded manifest records `source_dirty: false`, the source commit, local builder
image ID, package inventory, per-ELF SHA-256, `DT_NEEDED`, `$ORIGIN` RUNPATH and
symbol requirements. The archive and manifest are ignored build artifacts, not
Git-tracked deliverables.

## Deck transfer and Vulkan/RADV boundary

No transfer is authorized or implemented here. For a later approved experiment,
copy only a checksum-verified archive into a new, dedicated staging directory on
the Deck; verify its `.sha256` and inspect the archive/manifest before extracting.
Do not overwrite an installed executable or plugin. If the artifact needs the
container’s newer userspace, separately export/load the exact builder image and
run from a matching Deck container, not from the host filesystem runtime. Any
execution or Vulkan test still requires separate authorization.

The ABI probe does not load Vulkan. The retained audit reports Deck RADV on the
AMD Van Gogh APU, while the desktop offers NVIDIA and Intel implementations; no
RADV device, host-loader pass-through, Vulkan extension/feature set, WSI, shader
pipeline, or rendering behavior was tested here. A pinned build image must use
the Deck host’s Vulkan loader/ICD/device interfaces at runtime and must not carry
desktop NVIDIA driver libraries. Vulkan/RADV runtime parity remains a distinct
validation gate after static ABI checks.

## Remaining blockers and next smallest step

- Obtain a read-only Deck connection and record the exact `fable2-build` image ID,
  installed package manifest, host `libstdc++.so.6`/CXXABI exports and RADV
  loader/ICD metadata. Current SSH alias resolution blocks those checks.
- Validate package creation with the already-built ABI probe after committing
  this pipeline, and inspect the resulting archive/manifest/checksum. Do not use
  a Fable executable or deploy it.
- The build image’s package set is fixed by the immutable archive snapshot and
  enforced tool versions, but a byte-for-byte export/import test of the derived
  builder image on Deck has not occurred.
- The next **smallest validation step** is read-only Deck access to compare the
  ABI probe’s GLIBC/GLIBCXX/CXXABI requirements and transitive dependencies
  against the actual Deck host/runtime, without executing or installing it.
