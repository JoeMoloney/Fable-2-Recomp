# Vulkan loader/RADV smoke test

This minimal program is built with the pinned Arch/Clang container and then run
on the CachyOS host against each installed host ICD. It dynamically loads the
host Vulkan loader (`libvulkan.so.1`), so it does not copy or link a desktop ICD
into the test binary. It creates a Vulkan 1.2 instance/device, fills 16 words in
a host-visible buffer with `vkCmdFillBuffer`, uses a transfer-to-host barrier and
fence, maps/invalidate-reads the buffer, and verifies all words.

It also queries the sampled 1D `R8G8B8A8_UNORM` format and descriptor-indexing
features relevant to a variable-size 1D texture descriptor heap. The query does
not enable descriptor-indexing features or instantiate the renderer's descriptor
layout. It is capability evidence, not validation of the proposed binding ABI.

Run locally:

```sh
bash tools/deck-build/vulkan-smoke/run-local.sh
```

The command builds offline in the pinned builder and runs separately with the
desktop NVIDIA and Intel ICD manifests. It writes JSON results and ELF symbol
checks under ignored `out/deck-build/vulkan-smoke/`. It does not contact the
Steam Deck.

After the source is committed and local results reviewed, prepare the test
bundle (still desktop-only):

```sh
bash tools/deck-build/package.sh out/deck-build/vulkan-smoke/vulkan_smoke
```

This writes a checksum-verified archive under ignored `out/artifacts/`. The
procedure below is **for after explicit user approval only**. Until approval, do
not run `scp`, `ssh`, `podman load`, or the test executable on the Deck.

Approved isolated-stage procedure, with values substituted from the generated
archive/checksum filenames:

1. Create a new staging directory under the Deck user's home, not under the game
   or Steam installation. Copy only the archive and its `.sha256` sidecar there.
2. Verify before extraction:

   ```sh
   cd ~/vulkan-smoke-stage
   sha256sum -c fable2-experimental-<commit>.tar.gz.sha256
   tar -tzf fable2-experimental-<commit>.tar.gz
   ```

3. Extract into a new directory. Confirm the embedded manifest's source commit,
   executable SHA-256 and ELF requirements before execution.
4. Only if the user separately approves execution, run the executable once and
   save its JSON stdout/stderr locally in the staging directory. It only creates
   an instance/device and does a tiny buffer fill/readback; no game is launched.
5. Return the JSON and `sha256sum` output to the desktop for comparison. Keep the
   staging directory separate; do not install, replace or alter Steam shortcuts,
   saves, configuration, or production files.

Deck feature-query output is not known until that separately approved transfer
and execution. Host-side `vulkaninfo` feature reports are not a substitute for
RADV behavior on the Deck.
