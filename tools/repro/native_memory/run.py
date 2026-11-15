"""Compare a translated copy kernel with the SDK's existing native memcpy body.

Ordinary disjoint RAM only; not a production replacement or full-context proof.
No game, codegen, installed build, SDK mutation, or CPU-feature changes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = ROOT / "thirdparty/rexglue-sdk"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "out/performance").resolve()) or out.exists():
        parser.error("output must be a NEW directory below out/performance")
    out.mkdir(parents=True)
    snapshot = out / "driver-source"
    snapshot.mkdir()
    for p in HERE.iterdir():
        if p.is_file(): shutil.copy2(p, snapshot / p.name)
    original_path = ROOT / "generated/default/fable_2_recomp.215.cpp"
    native_path = SDK / "src/kernel/crt/memory.cpp"
    watched = [original_path, native_path, ROOT / "generated/default/fable_2_pch.h",
               ROOT / "fable_2_manifest.toml", *list((SDK / "include").rglob("*.h"))]
    before = {str(p.relative_to(ROOT)): sha(p) for p in watched}
    (out / "production-before.json").write_text(json.dumps(before, indent=2) + "\n")
    report = {"scope": "ordinary disjoint RAM kernel, not full guest-context equivalence",
              "steps": [], "status": "started"}

    def step(name, command, expected=0, marker=None):
        print(name, flush=True)
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=180)
        (out / f"{name}.stdout").write_text(result.stdout)
        (out / f"{name}.stderr").write_text(result.stderr)
        report["steps"].append({"name": name, "command": list(map(str, command)),
                                "returncode": result.returncode, "expected": expected})
        if result.returncode != expected or (marker and marker not in result.stdout + result.stderr):
            raise RuntimeError(f"{name} did not meet its gate; see retained logs")
        return result

    flags = ["-std=c++23", "-O3", "-DNDEBUG", "-march=x86-64-v2", "-msse4.1",
             "-mcmodel=large", "-fno-strict-aliasing", "-ffp-model=strict"]
    for p in (ROOT / "generated/default", SDK / "include", SDK / "thirdparty/simde",
              SDK / "thirdparty/fmt/include", SDK / "thirdparty/spdlog/include"):
        flags += ["-I", str(p)]
    try:
        text = original_path.read_text()
        start = text.index("DEFINE_REX_FUNC(MemCpy_Aligned_82CA30E8) {")
        end = text.find("\nDEFINE_REX_FUNC(", start + 1)
        original = text[start:end].replace("DEFINE_REX_FUNC(MemCpy_Aligned_82CA30E8)",
                                           "DEFINE_REX_FUNC(copy_translated)", 1)
        text = native_path.read_text()
        start = text.index("static void* native_memcpy(")
        end = text.index("\n}", start) + 2
        native_body = text[start:end]
        report["native_body_verbatim"] = native_body
        (out / "translated.cpp").write_text('#include "fable_2_pch.h"\n' + original)
        (out / "native.cpp").write_text(
            '#include "fable_2_pch.h"\n#include <cstring>\n' + native_body + '\n' +
            'extern "C" __attribute__((noinline)) void copy_native('
            'PPCContext& __restrict ctx, uint8_t* base) {\n'
            '  native_memcpy(REX_RAW_ADDR(ctx.r3.u32), REX_RAW_ADDR(ctx.r4.u32), ctx.r5.u32);\n'
            '}\n')
        sources = [HERE / "harness.cpp", out / "translated.cpp", out / "native.cpp"]
        step("compile", ["clang++", *flags, *sources, "-o", out / "checks"])
        positive = step("correctness", [out / "checks"], marker="PASS native_memory")
        report["correctness_summary"] = positive.stdout.strip()
        step("negative-oracle", [out / "checks", "negative-oracle"], expected=1,
             marker="translated differs from independent byte oracle")
        step("negative-context", [out / "checks", "negative-context"], expected=1,
             marker="native unexpectedly changed context")
        step("negative-noop", [out / "checks", "negative-noop"], expected=1,
             marker="native differs from independent byte oracle")
        step("compile-ubsan", ["clang++", *flags, "-fsanitize=undefined",
                              "-fno-sanitize-recover=all", *sources, "-o", out / "checks-ubsan"])
        step("correctness-ubsan", [out / "checks-ubsan"], marker="PASS native_memory")
        step("benchmark", [out / "checks", "bench"], marker="BENCH length=4096")
        report["status"] = "passed_RAM_kernel_checks_not_production_compatibility"
    except Exception as error:
        report["status"] = "stopped"
        report["error"] = str(error)
        print(error, file=sys.stderr)
    finally:
        changed = [p for p, digest in before.items() if not (ROOT / p).is_file() or sha(ROOT / p) != digest]
        report["production_inputs_unchanged"] = not changed
        report["watched_production_file_count"] = len(before)
        report["changed_watched_production_files"] = changed
        report["artifact_hashes"] = {p.name: sha(p) for p in out.iterdir() if p.is_file()}
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("steps", "artifact_hashes")}, indent=2))
    return 0 if report["status"].startswith("passed") and not changed else 1


if __name__ == "__main__":
    raise SystemExit(main())
