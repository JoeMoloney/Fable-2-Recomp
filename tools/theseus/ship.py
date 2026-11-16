#!/usr/bin/env python3
"""Ship a gated Theseus batch into the game (plans/THESEUS_BATCH.md, unit B4).

  python3 tools/theseus/ship.py 1             # write the override wrappers for batch 1
  python3 tools/theseus/ship.py 1 --build     # ... then an incremental Release build + symbol check
  python3 tools/theseus/ship.py 1 --confirm   # after the in-game A/B: ledger rows -> shipped

A wrapper is src/core/hotfunc/theseus/<name>.cpp: it #includes the tested rewrite
(so the game runs exactly the code the gate checked) and THESEUS_OVERRIDE(<name>)
from theseus_switch.h (--theseus_rewrites=false runs the recompiled originals: A/B
without a rebuild). The CMake glob picks new files up on the next build.

Only ledger rows of that batch with status `gated` and a passing
out/theseus/batch/<name>/gate.json are shipped; a function that already has a
hand-written override elsewhere in src/core/hotfunc/ is refused.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import batch_pool  # noqa: E402
import gate  # noqa: E402

ROOT = gate.ROOT
HOTFUNC = ROOT / "src" / "core" / "hotfunc"
THESEUS_DIR = HOTFUNC / "theseus"
BUILD_DIR = gate.BUILD_DIR
EXE = BUILD_DIR / "fable_2"


def wrapper_text(name):
    # Same text the gate compiled (override check), with the include relative to the repo.
    return gate.override_wrapper(name, f"../../../../tools/theseus/rewrites/{name}.cpp")


def other_override(name):
    """A hand-written override of the same guest function outside theseus/?"""
    pat = re.compile(r"\b" + re.escape(name) + r"\b")
    for p in HOTFUNC.rglob("*.cpp"):
        if THESEUS_DIR in p.parents:
            continue
        if pat.search(p.read_text(errors="replace")):
            return p
    return None


def batch_rows(batch):
    return [r for r in batch_pool.read_ledger().values() if r["batch"] == str(batch)]


def write_wrappers(rows):
    written, skipped = [], []
    for r in sorted(rows, key=lambda r: r["name"]):
        name = r["name"]
        gj = gate.TARGET.out / "batch" / name / "gate.json"
        if r["status"] not in ("gated", "shipped"):
            skipped.append((name, f"status {r['status']}"))
            continue
        if not gj.exists() or not json.loads(gj.read_text()).get("passed"):
            skipped.append((name, "no passing gate.json (rerun gate.py)"))
            continue
        other = other_override(name)
        if other:
            skipped.append((name, f"already overridden in {other.relative_to(ROOT)}"))
            continue
        path = THESEUS_DIR / f"{name}.cpp"
        text = wrapper_text(name)
        if not path.exists() or path.read_text() != text:
            path.write_text(text)
        written.append(name)
    return written, skipped


def build_and_check(names):
    print(f"incremental Release build in {BUILD_DIR} ...", flush=True)
    r = subprocess.run(["cmake", "--build", str(BUILD_DIR), "--target", "fable_2", "-j4"],
                       capture_output=True, text=True)
    log = gate.TARGET.out / "batch" / "ship_build.log"
    log.write_text(r.stdout + r.stderr)
    if r.returncode:
        print(r.stdout[-3000:] + r.stderr[-3000:])
        sys.exit(f"build failed (log: {log})")
    syms = subprocess.run(["nm", "--defined-only", str(EXE)], capture_output=True, text=True).stdout
    strong = {l.split()[-1] for l in syms.splitlines() if re.match(r"^\S+ T ", l)}
    missing = [n for n in names if n not in strong or f"rewrite_{n}" not in strong]
    if missing:
        sys.exit("not in the binary as strong symbols: " + ", ".join(missing))
    print(f"build ok; all {len(names)} overrides are strong symbols ({EXE.relative_to(ROOT)})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("batch")
    ap.add_argument("--build", action="store_true", help="incremental Release build + nm check")
    ap.add_argument("--confirm", action="store_true", help="after the in-game A/B: mark the batch shipped")
    args = ap.parse_args()

    rows = batch_rows(args.batch)
    if not rows:
        sys.exit(f"no ledger rows in batch {args.batch}")
    if args.confirm:
        ledger = batch_pool.read_ledger()
        n = 0
        for r in rows:
            if (THESEUS_DIR / f"{r['name']}.cpp").exists() and r["status"] == "gated":
                ledger[r["name"]]["status"] = "shipped"
                n += 1
        batch_pool.write_ledger(ledger)
        print(f"batch {args.batch}: {n} rows -> shipped")
        return
    written, skipped = write_wrappers(rows)
    print(f"batch {args.batch}: {len(written)} wrappers in {THESEUS_DIR.relative_to(ROOT)}")
    for name, why in skipped:
        print(f"  skipped {name}: {why}")
    if args.build and written:
        build_and_check(written)


if __name__ == "__main__":
    main()
