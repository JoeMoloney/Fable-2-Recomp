# Source-only tests

From the repository root:

```cmd
python tools/generate_game_versions.py --check
python -m unittest discover -s tests -p test_*.py
dotnet run --project tests/Fable2.Launcher.ConfigTests -c Release
tests\run_native_tests.cmd
```

Python tests also cover Debug/Release SDK staging with synthetic
files, including failure when Debug DLLs or import libraries are missing.
The native runner needs a VS x64 developer shell with Clang and the patched
SDK source. An optional first argument selects that source tree. It checks
audio pacing, host FPS limiting (including 144), FPS metering and the version
catalogue/content guards in compatible and both single-edition configurations.
Test executables go under `out/tests`; no game files or audio/GPU device are
needed. Original XEX paths can optionally be passed to the native verifier,
and a dump path to the launcher tests, for local checks. Never commit these inputs.

## Linux diagnostic read check

After initializing the SDK dependencies, from the repository root:

```sh
mkdir -p out
clang++ -std=c++23 -march=x86-64-v2 \
  -I src/diagnostics -I thirdparty/rexglue-sdk/include \
  -I thirdparty/rexglue-sdk/thirdparty/simde \
  -I thirdparty/rexglue-sdk/thirdparty/simde/simde \
  -I thirdparty/rexglue-sdk/thirdparty/fmt/include \
  -I thirdparty/rexglue-sdk/thirdparty/spdlog/include \
  tests/native/linux_ui_input_read.cpp -o out/linux_ui_input_read_test
out/linux_ui_input_read_test
```

Checks a valid read plus protected, partially accessible and unmapped reads
without installing signal handlers or needing game content.
