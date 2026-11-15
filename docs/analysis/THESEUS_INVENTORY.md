# Function inventory and first differential test (Ship of Theseus groundwork)

2026-11-15. Offline analysis of the recompiled code. No game launch, no Ghidra
changes, no source changes. Tools in `tools/theseus/`; plan in
[plans/THESEUS_PILOT.md](../../plans/THESEUS_PILOT.md).

## Inventory (`tools/theseus/inventory.py`)

Scans `generated/default/*.cpp` (11 s) and writes `out/theseus/functions.jsonl`
(one record per function: size, callers, callees, leaf, call-graph level, kernel
imports, floating point/vector use, hooks, hotfunc override) and `summary.json`.
Rerun after codegen changes. All counts are static call *sites*, not run-time frequency.

| | Count |
|---|---:|
| Guest functions | **60,753** (4.5 M PowerPC instructions) |
| With any name | 5,286 |
| … of which generic auto-names (`ProcessAndProcessAndProcess1728_…`, `IndirectCall_…`) | 3,537 |
| **Meaningfully named** | **1,749, holding ~2% of all instructions** |
| Leaf functions (no direct, indirect or kernel calls) | 9,504 (1,163 named) |
| Small leaves (6–80 instructions, no locks/timers/hooks): pilot pool | 4,931 (771 named) |
| No direct callers | 28,207 (mostly reached via function pointers / vtables: 29,395 indirect call sites) |
| Use floating point / vector (VMX) | 11,502 / 5,031 |
| Call kernel imports | 1,001 |
| Hand-written hotfunc overrides | 20 |

Sizes: 9,712 tiny (≤ 8 instructions), 19,644 small (≤ 32), 22,506 medium (≤ 128),
8,102 large (≤ 512), 789 huge. The largest is `sub_82242ED0` (27,484 instructions,
the floating-point routine from `docs/re/RE_FINDINGS.md`).

Most-called functions (by distinct callers) are the memory and refcount
helpers: `DestructRefCounted_82214F08` (5,636 callers), `ConstructRefCounted_8222CF18`
(5,200), `Free_SizeBucketed_Thunk_8221BE68` (4,498), `Allocate_OrDbgBreak_8221F388`
(3,725), `FNV1Hash_821F3C28` (2,167). The most-used kernel calls are critical
sections and spin locks.

**What this means:** naming, not rewriting, is the biggest gap: 98% of the code
has no meaningful name. Naming is safe to run as a batch job (it changes no
behaviour); rewriting must be proven function by function. The naming work is
planned in [plans/RE_NAMING_PASS.md](../../plans/RE_NAMING_PASS.md).

Names without an address suffix (e.g. `FrontMenu_EnsureElement`) take their
address from the manifest; the 237 still without one are register save/restore helpers.

## Differential test (`tools/theseus/difftest.py`)

Runs the original recompiled body and a readable rewrite on the same random CPU
states and guest memory (a lazily allocated 4 GB mapping, so any 32-bit address
is valid; argument registers mostly point into a 64 KB window of random bytes),
then compares:

- **ABI level** (what callers may rely on): r3 low 32 bits, r1/r2, r13–r31,
  f14–f31, cr2–cr4, lr, compared memory. **Pass/fail is decided here.**
- **Strict**: every byte of the CPU state. Scratch registers usually differ in a
  readable rewrite; that's expected and safe if callers never read them.
- The upper 32 bits of r3 are reported separately.

`tools/theseus/caller_usage.py <name>` checks the callers' side: what each call
site does with r3 afterwards, and whether any reads a scratch register before
writing it.

### First function: `FNV1Hash_821F3C28`

Identified as **FNV-1 32-bit** (prime `0x01000193`, multiply then xor) over a
zero-terminated string; r3 = string, r4 = starting hash. Readable version:
`tools/theseus/rewrites/FNV1Hash_821F3C28.cpp`.

| Run | Result |
|---|---|
| Plain 32-bit rewrite, 200,000 cases | ABI pass; **r3 upper 32 bits differed in 47%** (PowerPC `mullw` leaves a 64-bit product) |
| Callers (2,630 call sites) | 0 read a scratch register first. r3: 1,662 pass it back to their own caller, 807 copy all 64 bits, 98 read the low 32, 33 overwrite it, 30 unclear. Upper-bit use **can't be ruled out** one level up. |
| Rewrite reproducing the full 64-bit r3 (one small `Mullw` helper), 200,000 cases | **ABI pass, r3 identical in all 64 bits**; only scratch r7–r11/cr6 differ |
| Negative control (FNV-1a order, a classic mistake), 20,000 cases | **FAIL caught on case 0** (6,093 mismatches; the rest were empty strings) |

### Lessons for the pilot

1. **Contract:** match the full 64-bit return register and everything the calling
   convention protects; allow scratch registers to differ. This needs
   `caller_usage.py` to show 0 scratch reads for each function rewritten.
2. Upper register bits are a real trap. A natural rewrite truncates them, and a
   one-level caller scan can't prove they're unused. Reproducing them is cheap.
3. The random-input approach works for pure functions like hashes. Functions that
   take lengths or write memory need per-function input rules (which registers
   are pointers/lengths), and the harness only compares the data and stack windows.
   Detecting writes elsewhere (comparing every touched page) is a planned improvement.
4. Building and running one test takes about 1 minute (mostly compiling); 200,000 cases
   run in seconds.
