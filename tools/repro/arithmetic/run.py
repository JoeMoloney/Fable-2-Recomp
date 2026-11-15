"""Offline arithmetic lowering experiment; never codegens or launches the game."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = ROOT / "thirdparty/rexglue-sdk"
SAMPLES = (
    ("norm", "generated/default/fable_2_recomp.231.cpp", "sub_82CF8DF0"),
    ("geometry", "generated/default/fable_2_recomp.68.cpp", "sub_82FD2088"),
    ("filter", "src/core/hotfunc/unsorted/InterpolateFloatArray_82C62548.cpp",
     "InterpolateFloatArray_82C62548"),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_generated(text, name):
    start = text.index(f"DEFINE_REX_FUNC({name}) {{")
    end = text.find("\nDEFINE_REX_FUNC(", start + 1)
    return text[start:end if end >= 0 else len(text)].rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--assembly-only", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "out/performance").resolve()) or out.exists():
        parser.error("output must be a NEW directory below out/performance")
    out.mkdir(parents=True)
    snapshot = out / "driver-source"
    snapshot.mkdir()
    for path in HERE.iterdir():
        if path.is_file():
            shutil.copy2(path, snapshot / path.name)
    paths = [ROOT / relative for _, relative, _ in SAMPLES]
    paths += [ROOT / "fable_2_manifest.toml", ROOT / "generated/default/fable_2_pch.h",
              ROOT / "build-release-linux_amd64.sh", SDK / "CMakeLists.txt",
              SDK / "src/codegen/builders/floating_point.cpp"]
    paths += list((SDK / "include").rglob("*.h"))
    before = {str(p.relative_to(ROOT)): sha(p) for p in paths}
    (out / "production-before.json").write_text(json.dumps(before, indent=2) + "\n")
    report = {"scope": "unchanged current C++ bodies; baseline ISA versus AVX+FMA; no fast-math",
              "steps": [], "status": "started", "samples": SAMPLES,
              "compiler": subprocess.check_output(["clang++", "--version"], text=True)}

    def step(name, command, expected=0, marker=None):
        print(name, flush=True)
        result = subprocess.run(list(map(str, command)), text=True, capture_output=True, timeout=180)
        (out / f"{name}.stdout").write_text(result.stdout)
        (out / f"{name}.stderr").write_text(result.stderr)
        report["steps"].append({"name": name, "command": list(map(str, command)),
                                "returncode": result.returncode, "expected": expected})
        if result.returncode != expected or (marker and marker not in result.stdout + result.stderr):
            raise RuntimeError(f"{name} did not meet its exit/result gate; see retained logs")
        return result

    flags = ["-std=c++23", "-O3", "-DNDEBUG", "-march=x86-64-v2", "-msse4.1",
             "-mcmodel=large", "-fno-strict-aliasing", "-ffp-model=strict"]
    for path in (ROOT / "generated/default", SDK / "include", SDK / "thirdparty/simde",
                 SDK / "thirdparty/fmt/include", SDK / "thirdparty/spdlog/include"):
        flags += ["-I", str(path)]
    try:
        for variant in ("base", "fma"):
            source = '#include "fable_2_pch.h"\n\n'
            for short, relative, name in SAMPLES:
                text = (ROOT / relative).read_text()
                symbol = f"arith_{variant}_{short}"
                if short == "filter":
                    # Keep the active strong override, not just the unused weak
                    # generated copy. Only its exported name is changed.
                    text = text.replace(f'void {name}(', f'void {symbol}(', 1)
                else:
                    text = extract_generated(text, name).replace(
                        f"DEFINE_REX_FUNC({name})", f"DEFINE_REX_FUNC({symbol})", 1)
                source += text + "\n"
            source += (f'extern "C" __attribute__((noinline)) double arith_{variant}_primitive('
                       'double a, double b, double c) { return std::fma(a, b, c); }\n')
            path = out / f"{variant}.cpp"
            path.write_text(source)
            extra = ["-mavx", "-mfma"] if variant == "fma" else []
            step(f"assembly-{variant}", ["clang++", *flags, *extra, "-S", "-masm=intel",
                                         path, "-o", out / f"{variant}.s"])
            step(f"object-{variant}", ["clang++", *flags, *extra, "-c", path,
                                       "-o", out / f"{variant}.o"])
        report["assembly_observations"] = {}
        for variant in ("base", "fma"):
            assembly = (out / f"{variant}.s").read_text()
            report["assembly_observations"][variant] = {
                "fma_library_symbol_lines": [line.strip() for line in assembly.splitlines()
                                             if re.search(r"\bfma@(?:GOT|PLT)", line)],
                "hardware_fma_instruction_lines": [line.strip() for line in assembly.splitlines()
                                                    if re.match(r"\s+vf[nm]*[am][a-z0-9]*\s", line)],
            }
        if args.assembly_only:
            report["status"] = "assembly_only_no_numerical_or_performance_conclusion"
        else:
            step("compile-tests", ["clang++", *flags, HERE / "harness.cpp", out / "base.o",
                                    out / "fma.o", "-ldl", "-o", out / "checks"])
            positive = step("correctness", [out / "checks"], marker="PASS arithmetic")
            report["correctness_summary"] = positive.stdout.strip()
            step("negative-result", [out / "checks", "negative-result"], expected=1,
                 marker="deliberate output corruption detected")
            step("negative-unfused", [out / "checks", "negative-unfused"], expected=1,
                 marker="unfused arithmetic rejected")
            step("benchmark", [out / "checks", "bench"], marker="BENCH")
            report["status"] = "passed_bounded_arithmetic_checks_not_game_compatibility"
    except Exception as error:
        report["status"] = "stopped"
        report["error"] = str(error)
        print(error, file=sys.stderr)
    finally:
        changed = [p for p, digest in before.items() if not (ROOT / p).is_file() or sha(ROOT / p) != digest]
        report["changed_watched_production_files"] = changed
        report["production_inputs_unchanged"] = not changed
        report["watched_production_file_count"] = len(before)
        report["artifact_hashes"] = {p.name: sha(p) for p in out.iterdir() if p.is_file()}
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("steps", "artifact_hashes",
                                                                "compiler", "samples")}, indent=2))
    return 0 if report["status"] != "stopped" and not changed else 1


if __name__ == "__main__":
    raise SystemExit(main())
