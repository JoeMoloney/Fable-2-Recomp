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

## Remote-control JSON portability check

```sh
python -m unittest discover -s tests -p 'test_remote_json.py' -v
```

Compiles and runs the actual `json_min` namespace extracted from
`src/input/remote_control_server.h` with available `clang++` and `g++` compilers
in C++23 mode. Each unavailable compiler is reported as a skip. Covers recursive
objects/arrays, lookup/defaults/conversions, copy/move/destruction, ordering,
escaping and basic malformed-input rejection. It needs a C++23 standard library,
but no SDK, server, GPU, game content or save files. This is not a full JSON
conformance, remote-protocol, socket or screenshot test.
