#!/usr/bin/env python3
"""Differential-test a readable rewrite of a leaf guest function.

  python3 tools/theseus/difftest.py FNV1Hash_821F3C28 [--cases N] [--seed S]

Takes the original body from generated/default (as `original_<name>`), the
rewrite from tools/theseus/rewrites/<name>.cpp (which must define
`rewrite_<name>`), builds tools/theseus/difftest_harness.cpp around them and
runs it. Output goes to out/theseus/difftest/<name>/. Leaf functions only:
the harness has no other guest functions or kernel imports to link against.

Optional input rules: tools/theseus/rewrites/<name>.rules, one register per
line (`r3 ptr`, `r4 ptr_or_null`, `r5 len 256`, `r6 int`, `r7 u32`, `r8 any`,
`r4 slen 64` = -64..64, `r3 ptr_lo` = pointer
always in the low window (0x4000xxxx), never its copy above the sign bit
(0xB000xxxx, a quarter of `ptr` values and random pointers), `r3 str 120` = pointer to a string of
length 0..120 (a quarter at 119..121), `r3 fixed 0x4000C000` = always that value, `fixed_or_null 0x4000C000` = that value or (1 in 8) 0, `#` comments;
`tree <isnil_off> <key_off> [n]` builds a valid MSVC std::map tree each case,
then kinds `tree_cont` / `tree_node` / `tree_key` point into it;
`data <addr> <hex>` fixed bytes (image constants); `ret f1` = float return;
`fval <double>` an edge value for float arguments (nudged by +-2 ulps); `r4 near r3 32` = half the time r3 + 0..32), `r4 mirror r3` = a copy of r3's string in its own buffer, half the time with one byte changed (strcmp-style functions), and `mem32 <addr> <kind> [max]` to set a 32-bit global (e.g. a
table pointer) before each case; `mem32 @r3+12 <kind>` sets a field of an
argument's struct; `mem32 <addr|@rN+off> oneof a b [c d]` = one of those values; `mem32 *0x834BDBC4+16 <kind>` a field of the struct a
global points to; `mem32 @r3+8 near @r3+12 512` = half the time the
other field's value + 0..512 (set the referenced field first); `mem32 @r4+0 nearbits @r3+0` = the
other field's value, half the time with one random bit flipped. Rules may also set r11..r13
(e.g. `r13 ptr_lo` for code that reads the per-processor block through r13). Without it every argument register gets a
random mix and all memory outside the windows is zero.
"""
import argparse
import re
import signal
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import target as target_mod  # noqa: E402

TARGET = target_mod.load()
ROOT = TARGET.root
GEN = TARGET.generated
SDK = TARGET.sdk


def extract(name):
    start = re.compile(r"^DEFINE_REX_FUNC\(" + re.escape(name) + r"\)\s*\{")
    for path in TARGET.partitions():
        lines, body = path.read_text(errors="replace").splitlines(), None
        for i, line in enumerate(lines):
            if start.match(line):
                body = [line]
                for rest in lines[i + 1:]:
                    body.append(rest)
                    if rest.startswith("}"):
                        return path.name, body
    sys.exit(f"{name}: not found in generated code")


KINDS = {"slen": "kSLen", "ptr": "kPtr", "ptr_or_null": "kPtrOrNull", "len": "kLen", "int": "kInt",
         "u32": "kU32", "any": "kAny", "tree_cont": "kTreeCont", "tree_node": "kTreeNode",
         "tree_key": "kTreeKey", "fixed": "kFixed", "str": "kStr", "ptr_lo": "kPtrLo",
         "fixed_or_null": "kFixedOrNull"}


def rules_inc(path):
    """Turn a .rules file into the C++ kRules table for the harness."""
    rows, mems, tree, datas, fret, fvals = [], [], "{0u, 0u, 0u}", [], "false", []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        if words[0] == "data":
            if len(words) != 3 or not re.fullmatch(r"(?:[0-9a-fA-F]{2})+", words[2]):
                sys.exit(f"{path}:{n}: expected 'data <addr> <hex bytes>'")
            datas.append(f'{{{int(words[1], 0):#x}u, "{words[2]}"}}')
            continue
        if words[0] == "fval":
            if len(words) != 2:
                sys.exit(f"{path}:{n}: expected 'fval <double>' (decimal or 0x hex bits)")
            if words[1].lower().startswith("0x"):
                fvals.append(f"std::bit_cast<double>(uint64_t({words[1]}ull))")
            else:
                fvals.append(repr(float(words[1])))
            continue
        if words[0] == "ret":
            if words[1:] != ["f1"]:
                sys.exit(f"{path}:{n}: only 'ret f1' is supported")
            fret = "true"
            continue
        if words[0] == "tree":
            if len(words) not in (3, 4):
                sys.exit(f"{path}:{n}: expected 'tree <isnil_off> <key_off> [max_nodes]'")
            isnil, key = int(words[1], 0), int(words[2], 0)
            maxn = int(words[3], 0) if len(words) > 3 else 24
            if not (isnil < 32 and key + 4 <= 32 and 12 <= min(isnil, key) and 0 < maxn <= 255):
                sys.exit(f"{path}:{n}: fields must sit in 12..31 of a 32-byte node, max_nodes 1..255")
            tree = f"{{{isnil}u, {key}u, {maxn}u}}"
            continue
        if words[0] == "mem32" and len(words) == 4 and words[2] == "nearbits":
            rel = re.fullmatch(r"@r([3-9]|1[0-3])\+(\w+)", words[1])
            ref = re.fullmatch(r"@r([3-9]|1[0-3])\+(\w+)", words[3])
            if not rel or not ref:
                sys.exit(f"{path}:{n}: expected 'mem32 @rN+off nearbits @rM+off'")
            mems.append(f"{{{int(rel.group(2), 0):#x}u, kNearBits, 0u, {rel.group(1)}, 0u, "
                        f"{ref.group(1)}, {int(ref.group(2), 0):#x}u}}")
            continue
        if words[0] == "mem32" and len(words) == 5 and words[2] == "near":
            rel = re.fullmatch(r"@r([3-9]|1[0-3])\+(\w+)", words[1])
            ref = re.fullmatch(r"@r([3-9]|1[0-3])\+(\w+)", words[3])
            if not rel or not ref:
                sys.exit(f"{path}:{n}: expected 'mem32 @rN+off near @rM+off <max>'")
            mems.append(f"{{{int(rel.group(2), 0):#x}u, kNear, {int(words[4], 0)}u, {rel.group(1)}, 0u, "
                        f"{ref.group(1)}, {int(ref.group(2), 0):#x}u}}")
            continue
        if words[0] == "mem32" and len(words) >= 4 and words[2] == "oneof":
            vals = [int(w, 0) for w in words[3:]]
            if not 1 < len(vals) <= 4:
                sys.exit(f"{path}:{n}: oneof takes 2 to 4 values")
            rel = re.fullmatch(r"@r([3-9]|1[0-3])\+(\w+)", words[1])
            addr, reg = (int(rel.group(2), 0), rel.group(1)) if rel else (int(words[1], 0), 0)
            alts = ", ".join(f"{v:#x}u" for v in vals)
            mems.append(f"{{{addr:#x}u, kOneOf, 0u, {reg}, 0u, 0, 0u, {{{alts}}}, {len(vals)}u}}")
            continue
        if words[0] == "mem32":
            if len(words) < 3 or words[2] not in KINDS:
                sys.exit(f"{path}:{n}: expected 'mem32 <addr> <{'|'.join(KINDS)}> [max]'")
            maxv = int(words[3], 0) if len(words) > 3 else 0
            rel = re.fullmatch(r"@r([3-9]|1[0-3])\+(\w+)", words[1])
            der = re.fullmatch(r"\*(0x[0-9a-fA-F]+)\+(\w+)", words[1])
            if rel:
                mems.append(f"{{{int(rel.group(2), 0):#x}u, {KINDS[words[2]]}, {maxv}u, {rel.group(1)}}}")
            elif der:
                mems.append(f"{{{int(der.group(2), 0):#x}u, {KINDS[words[2]]}, {maxv}u, 0, {int(der.group(1), 16):#x}u}}")
            else:
                mems.append(f"{{{int(words[1], 0):#x}u, {KINDS[words[2]]}, {maxv}u}}")
            continue
        if len(words) == 3 and words[1] == "mirror":
            base = re.fullmatch(r"r([3-9]|1[0-3])", words[2])
            if not re.fullmatch(r"r([3-9]|1[0-3])", words[0]) or not base:
                sys.exit(f"{path}:{n}: expected 'rN mirror rM' (rM: a str rule)")
            rows.append(f"{{{words[0][1:]}, kMirror, 0u, {base.group(1)}}}")
            continue
        if len(words) == 4 and words[1] == "near":
            base = re.fullmatch(r"r(\d+)", words[2])
            if not re.fullmatch(r"r([3-9]|1[0-3])", words[0]) or not base or not 3 <= int(base.group(1)) <= 13:
                sys.exit(f"{path}:{n}: expected 'rN near rM <max>'")
            rows.append(f"{{{words[0][1:]}, kNear, {int(words[3], 0)}u, {base.group(1)}}}")
            continue
        m = re.fullmatch(r"r(\d+)", words[0])
        if not m or not 3 <= int(m.group(1)) <= 13 or len(words) < 2 or words[1] not in KINDS:
            sys.exit(f"{path}:{n}: expected 'r3..r13 <{'|'.join(KINDS)}> [max]'")
        if words[1] == "str" and (len(words) < 3 or not 1 < int(words[2], 0) < 0x8000):
            sys.exit(f"{path}:{n}: str needs a max length between 2 and 0x7FFF")
        if words[1] in ("len", "slen") and (len(words) < 3 or not 0 < int(words[2], 0) <= 0x8000):
            sys.exit(f"{path}:{n}: len needs a max between 1 and 0x8000 (the pointer room)")
        maxv = int(words[2], 0) if len(words) > 2 else 0
        rows.append(f"{{{m.group(1)}, {KINDS[words[1]]}, {maxv}u}}")
    rows = rows or ["{0, kAny, 0u}"]
    mems = mems or ["{0u, kAny, 0u}"]
    return ("const ArgRule kRules[] = {" + ", ".join(rows) + "};\n"
            "const MemRule kMemRules[] = {" + ", ".join(mems) + "};\n"
            f"const TreeRule kTreeRule = {tree};\n"
            "const DataRule kDataRules[] = {" + (", ".join(datas) or '{0u, ""}') + "};\n"
            f"constexpr bool kFloatReturn = {fret};\n"
            "const double kFloatValues[] = {" + (", ".join(fvals) or "0.0") + "};\n")


def write_original(name, out):
    """Extract the original body as original_<name> into out/original.cpp; returns the body lines."""
    src_file, body = extract(name)
    text = "\n".join(body)
    if re.search(r"^\s*\w+\(ctx, base\);", text, re.M) or "REX_CALL_INDIRECT_FUNC" in text:
        sys.exit(f"{name} calls other functions; only leaf functions are supported")
    out.mkdir(parents=True, exist_ok=True)
    head = f'// Extracted from generated/default/{src_file}\n#include "fable_2_pch.h"\n'
    (out / "original.cpp").write_text(head + "\n".join([f'extern "C" REX_FUNC(original_{name}) {{', *body[1:]]) + "\n")
    return src_file, body


def run_limited(cmd, cpu_seconds, env=None, wall_factor=20, min_wall=600):
    """Run a test binary with a CPU-time limit instead of a wall-clock one, so a busy
    machine (a game in the background) can't turn a slow run into a "hang".
    Returns (status, returncode, stdout, stderr); status is "ok", "cpu" (used up its
    CPU seconds: a real hang) or "wall" (the wall-clock backstop fired: inconclusive)."""
    import resource
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    try:
        lim = max(1, int(cpu_seconds))
        resource.prlimit(proc.pid, resource.RLIMIT_CPU, (lim, lim + 5))
    except (ProcessLookupError, ValueError):
        pass  # already finished
    try:
        out, err = proc.communicate(timeout=max(min_wall, cpu_seconds * wall_factor))
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
        return "wall", proc.returncode, out, err
    # SIGXCPU at the soft limit, SIGKILL at the hard one.
    if proc.returncode in (-signal.SIGXCPU, -signal.SIGKILL):
        return "cpu", proc.returncode, out, err
    return "ok", proc.returncode, out, err


def build(name, out, rewrite_srcs, defines=(), rules=None):
    """Compile the harness + original + rewrite source(s) into out/difftest.

    rules: a .rules path (default rewrites/<name>.rules if it exists, None = none).
    defines: extra -D names (e.g. MUTANTS, NO_TRAP_COUNT). Exits on build errors.
    """
    rules = rules if rules is not None else HERE / "rewrites" / f"{name}.rules"
    extra = []
    if rules and Path(rules).exists():
        (out / "difftest_rules.inc").write_text(rules_inc(Path(rules)))
        extra = ["-DHAS_RULES", "-I", str(out)]
    flags = [*extra, "-std=c++23", "-O2", "-march=x86-64-v2", "-msse4.1", "-fno-strict-aliasing",
             "-ffp-model=strict", "-DFMT_HEADER_ONLY", f"-DORIGINAL=original_{name}",
             f"-DREWRITE=rewrite_{name}", *(f"-D{d}" for d in defines)]
    for inc in (GEN, SDK / "include", SDK / "thirdparty/simde", SDK / "thirdparty/fmt/include",
                SDK / "thirdparty/spdlog/include"):
        flags += ["-I", str(inc)]
    exe = out / "difftest"
    build = subprocess.run(["clang++", *flags, str(HERE / "difftest_harness.cpp"), str(out / "original.cpp"),
                            *map(str, rewrite_srcs), "-o", str(exe)], capture_output=True, text=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stderr[-4000:])
        sys.exit(f"build failed (see {out / 'build.log'})")
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--cases", type=int, default=200000)
    ap.add_argument("--seed", default="0x9E3779B97F4A7C15")
    ap.add_argument("--rewrite", type=Path, help="test this file instead of rewrites/<name>.cpp (e.g. a deliberately broken copy)")
    ap.add_argument("--out-suffix", default="")
    ap.add_argument("--timeout", type=float, default=600.0,
                    help="CPU seconds before the run counts as hung (FAIL); 200k cases normally take well under a minute. "
                         "CPU time, not wall time, so a busy machine doesn't cause false hangs")
    args = ap.parse_args()

    rewrite = args.rewrite.resolve() if args.rewrite else HERE / "rewrites" / f"{args.name}.cpp"
    out = TARGET.out / "difftest" / (args.name + args.out_suffix)
    write_original(args.name, out)
    if not rewrite.exists():
        sys.exit(f"missing rewrite: {rewrite}")
    exe = build(args.name, out, [rewrite])
    status, code, stdout, stderr = run_limited([str(exe), str(args.cases), args.seed], args.timeout)
    if status != "ok":
        if status == "cpu":
            tail = (f"no result after {args.timeout:.0f} CPU seconds: the original or the rewrite hangs on some input\n"
                    "(often an input rule that lets a loop run across a huge range; check the .rules file)\n"
                    "RESULT FAIL (timeout)\n")
        else:
            tail = ("the wall-clock backstop fired before the CPU limit: the machine is too busy to tell\n"
                    "RESULT INCONCLUSIVE (rerun)\n")
        text = stdout + tail
        (out / "result.txt").write_text(text)
        print(text, end="")
        sys.exit(1 if status == "cpu" else 3)
    if code < 0:  # killed by a signal: the harness itself crashed (output may be lost)
        stdout += f"the test binary crashed (signal {-code})\nRESULT FAIL (crash, signal {-code})\n"
        code = 1
    run = subprocess.CompletedProcess([], code, stdout, stderr)
    (out / "result.txt").write_text(run.stdout + run.stderr)
    print(run.stdout + run.stderr, end="")
    sys.exit(run.returncode)


if __name__ == "__main__":
    main()
