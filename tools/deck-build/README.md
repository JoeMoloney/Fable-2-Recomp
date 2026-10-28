# Desktop / Steam Deck build pipeline

The scripts in this directory establish a pinned Arch build environment and a
small ABI probe. They do not launch, deploy, install, or modify Fable 2. All
builds and packages go under ignored `out/`.

## Image and toolchain

`Containerfile` is pinned to the Arch image digest observed in the retained Deck
build audit and uses the matching 2026-10-18 Arch Archive repository snapshot.
`toolchain.lock` records the audited compiler/CMake/Ninja versions and the
currently inspected Deck host symbol exports. Run `build-image.sh` to create the local cached image and
record its ID/package inventory under `out/deck-build/`. The build wrapper does
not pull or update packages implicitly. If package versions differ from the lock,
stop and reconcile the toolchain rather than proceeding with an unreviewed drift.

The Arch image itself contains glibc 2.44 and GCC runtime 16.2.1 in its pinned
base layer. The retained Deck report says the **host** has glibc 2.43 and
gcc-libs 16.1.1. Therefore an Arch-container-linked executable is not presumed
host-runnable. Keep such artifacts inside the same matching container runtime,
or use a separately validated SteamOS-compatible sysroot/toolchain for native
host binaries. The exact Deck container package manifest and live host ELF
exports remain to be verified.

## ABI probe

```sh
bash tools/deck-build/build-image.sh
bash tools/deck-build/abi-test.sh
```

The test builds a tiny C++ executable and shared library with `-march=x86-64-v2`,
runs it inside the container, and records ELF interpreter, `DT_NEEDED`, and
GLIBC/GLIBCXX/CXXABI version requirements. The target ceilings were read from
the Deck host during the final compatibility check. `abi-test.sh` enforces those
GLIBC/GLIBCXX/CXXABI ceilings. A failure means that ELF is not verified for
direct host use, not that it cannot run inside its matching container.

## Incremental builds and packages

`build.sh` runs CMake/Ninja in the cached image with network disabled, reuses
`out/build/deck-linux-amd64-release`, and defaults parallelism to the minimum of
12 jobs, detected CPU threads, and available RAM at approximately 2 GiB per
worker. Override with `FABLE2_BUILD_JOBS=N` after measuring project memory use.
It is a reusable future entry point; **do not run it for a full Fable build
without separate authorization**.

After an approved build, package only the intended ELF outputs:

```sh
bash tools/deck-build/package.sh out/build/deck-linux-amd64-release/fable_2 \
  out/build/deck-linux-amd64-release/librexgpu-xenos.so
```

The archive includes the source commit, builder image ID, recorded package
inventory, artifact SHA-256s, ELF dependencies and symbol-version requirements.
Packaging refuses a dirty source tree unless `--allow-dirty` is explicitly used.
Packages are written only under ignored `out/artifacts/`.

## Eventual transfer policy

Transfer is not implemented by these scripts. After separate authorization, use a
dedicated staging location on the isolated Deck, verify the archive SHA-256 and
manifest there, and do not copy over an installed executable/plugin. For an
artifact requiring the Arch container's newer glibc/libstdc++, transfer and run
it only through the exact pinned container image on Deck, with the Deck's host
Vulkan loader, ICD and devices passed through. Do not transfer desktop NVIDIA
ICD libraries. A native-host artifact requires symbol-version and dependency
validation against the actual SteamOS runtime first. No game launch is required
for the initial file/ABI checks; interactive rendering validation is a later,
separately approved gate.
