"""Experimental native Linux build/run workflow; never writes the source dump."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "thirdparty/rexglue-sdk"
BUILD = ROOT / "out/build/linux-amd64-release"
DEPENDENCIES = (
    "cli11 FFmpeg tomlplusplus simde xxHash spdlog fmt utfcpp imgui sdl3 "
    "vulkan-headers vulkan-memory-allocator spirv-headers spirv-tools glslang "
    "o1heap inja"
).split()


def run(command, cwd=ROOT):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, check=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_content(root, catalogue):
    xex = root / "default.xex"
    sha = digest(xex)
    version = next((v for v in catalogue["versions"]
                    if v["hash"] == sha and v["compatible"]), None)
    if version is None or xex.stat().st_size != version["xexSize"]:
        raise ValueError("XEX is not a supported catalogue revision")
    for name in version["requiredFiles"]:
        if not (root / name).is_file():
            raise ValueError(f"Required content missing: {name}")
    for name in version["rejectFiles"]:
        if (root / name).exists():
            raise ValueError(f"Conflicting content: {name}")
    # A locale's own required directory must not reject that same locale.
    for name in version["rejectDirectories"]:
        required_here = any(Path(p).is_relative_to(name)
                            for p in version["requiredFiles"])
        if not required_here and (root / name).exists():
            raise ValueError(f"Conflicting localization: {name}")
    return version


def reject_symlinks(root):
    if root.is_symlink() or any(p.is_symlink() for p in root.rglob("*")):
        raise ValueError(f"Refusing symlinked content: {root}")


def stage_content(source, destination, catalogue):
    source = source.resolve(strict=True)
    destination = destination.resolve(strict=True)
    if source.is_relative_to(destination) or destination.is_relative_to(source):
        raise ValueError("Source dump and checkout must be separate, non-nested trees")
    reject_symlinks(source / "data")
    version = verify_content(source, catalogue)
    target_xex = destination / "default.xex"
    if target_xex.is_symlink():
        raise ValueError("Codegen XEX must be a local copy, not a symlink")
    if target_xex.exists() and digest(target_xex) != version["hash"]:
        raise ValueError("Existing checkout XEX differs; refusing to overwrite it")
    target_data = destination / "data"
    if target_data.exists() or target_data.is_symlink():
        reject_symlinks(target_data)
        for file in (source / "data").rglob("*"):
            if not file.is_file():
                continue
            relative = file.relative_to(source / "data")
            target = target_data / relative
            # Runtime/Lua staging legitimately updates the local manifest.
            if not target.is_file() or (str(relative) != "dir.manifest" and
                                       target.stat().st_size != file.stat().st_size):
                raise ValueError(f"Existing staging differs at {relative}; repair manually")
    else:
        # Publish only a completed copy; an interrupted copy cannot be mistaken
        # for a complete staging tree. Never use hard links to the original.
        with tempfile.TemporaryDirectory(prefix=".linux-stage-", dir=destination) as tmp:
            staged = Path(tmp) / "data"
            shutil.copytree(source / "data", staged)
            staged.rename(target_data)
    if not target_xex.exists():
        with tempfile.TemporaryDirectory(prefix=".linux-stage-", dir=destination) as tmp:
            staged = Path(tmp) / "default.xex"
            shutil.copy2(source / "default.xex", staged)
            staged.rename(target_xex)
    return version


def prepare_sdk():
    pin = subprocess.check_output(
        ["git", "ls-tree", "HEAD", "thirdparty/rexglue-sdk"], cwd=ROOT, text=True
    ).split()[2]
    if not (SDK / ".git").exists():
        run(["git", "submodule", "update", "--init", "thirdparty/rexglue-sdk"])
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SDK, text=True).strip()
    if head != pin:
        raise ValueError("SDK checkout differs from parent pin; refusing to reset it")
    status = subprocess.check_output(["git", "submodule", "status"], cwd=SDK, text=True)
    if any(line.startswith(("+", "U")) for line in status.splitlines()):
        raise ValueError("SDK dependency revisions differ from pins; refusing to reset them")
    run(["git", "submodule", "update", "--init", "--jobs", "6", "--",
         *[f"thirdparty/{name}" for name in DEPENDENCIES]], cwd=SDK)
    patch = ROOT / "thirdparty/rexglue-sdk-linux-vendored-mspack.patch"
    if subprocess.run(["git", "apply", "--reverse", "--check", str(patch)],
                      cwd=SDK, capture_output=True).returncode == 0:
        print("SDK dependency-check patch already applied")
    else:
        run(["git", "apply", "--check", patch], cwd=SDK)
        run(["git", "apply", patch], cwd=SDK)


def build(args):
    catalogue = json.loads((ROOT / "constants/game_versions.json").read_text())
    if (ROOT / "default.xex").is_symlink():
        raise ValueError("Use stage first: codegen rejects XEX symlinks outside game_root")
    reject_symlinks(ROOT / "data")
    verify_content(ROOT, catalogue)
    prepare_sdk()
    configure = ["cmake", "--preset", "linux-amd64-release",
                 f"-DCMAKE_C_COMPILER={args.cc}", f"-DCMAKE_CXX_COMPILER={args.cxx}",
                 "-DCMAKE_C_FLAGS=-march=x86-64-v2", "-DCMAKE_CXX_FLAGS=-march=x86-64-v2",
                 f"-DREXSDK_DIR={SDK}", "-DREXGLUE_USE_VULKAN=ON",
                 "-DREXGLUE_ENABLE_TRACY=OFF", "-DFABLE2_CODEGEN_TOOL="]
    run(configure)
    run(["cmake", "--build", BUILD, "--target", "fable_2_codegen", "-j", args.jobs])
    # Fresh codegen creates sources.cmake; configure again to add recomp objects.
    run(configure)
    run(["cmake", "--build", BUILD, "--target", "fable_2", "-j", args.jobs])


def launch(args):
    executable = BUILD / "fable_2"
    if not executable.is_file():
        raise ValueError("Build the Linux executable first")
    if (ROOT / "default.xex").is_symlink():
        raise ValueError("Use a staged local XEX copy")
    reject_symlinks(ROOT / "data")
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = args.backend
    extra = args.extra[1:] if args.extra[:1] == ["--"] else args.extra
    command = [str(executable), f"--game_data_root={ROOT}", "--gpu_plugin=xenos",
               f"--vulkan_device={args.gpu}", "--window_width=1280", "--window_height=720",
               "--fullscreen=false", "--mouse_look=false", "--frame_limit=30",
               f"--async_shader_compilation={'true' if args.async_shaders else 'false'}", *extra]
    logs = ROOT / "out/linux-logs"
    logs.mkdir(parents=True, exist_ok=True)
    path = logs / f"console-{datetime.now():%Y%m%d-%H%M%S-%f}.log"
    print(f"Console log: {path}\nRuntime logs: {BUILD / 'logs'}", flush=True)
    # No timeout. Closing the game ends the wrapper; keep saves in the build dir.
    with path.open("x") as stream:
        return subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                              stderr=subprocess.STDOUT).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    stage = commands.add_parser("stage", help="Copy/validate your supported extracted dump")
    stage.add_argument("game_root", type=Path)
    compile_parser = commands.add_parser("build", help="Initialize pinned SDK and build Release")
    compile_parser.add_argument("--jobs", type=int, default=6)
    compile_parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    compile_parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    start = commands.add_parser("run", help="Launch experimental synchronous-shader baseline")
    start.add_argument("--backend", choices=("wayland", "x11"), default="wayland")
    start.add_argument("--gpu", type=int, default=0)
    start.add_argument("--async-shaders", action="store_true", help="Diagnostic only; may white-screen")
    start.add_argument("extra", nargs=argparse.REMAINDER, help="Extra game args after --")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("This helper targets Linux only")
    try:
        if args.command == "stage":
            version = stage_content(args.game_root, ROOT, json.loads(
                (ROOT / "constants/game_versions.json").read_text()))
            print(f"Staged {version['name']}; original dump unchanged")
        elif args.command == "build":
            if args.jobs < 1:
                parser.error("--jobs must be positive")
            build(args)
        else:
            return launch(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Linux workflow failed: {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
