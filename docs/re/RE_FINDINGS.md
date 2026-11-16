# Reconciled reverse-engineering findings

Audit date: 2026-10-24. Target SHA-256:
`88c4ef2e18e65409444d1b068eff921d1f7e180a5ae64edc64ba6b0872372662`.
Source baseline `eb3293c`; generated code is local, ignored build output.
Addresses are the durable keys; partition filenames below are audit locators.
This is a focused reconciliation, not a validation of every manifest annotation.

## Utility cluster: earlier names withdrawn

Live Ghidra decompilation and generated PPC comments agree on these observations.
Ghidra names below deliberately describe mechanisms; the manifest is not renamed
by this migration. Signatures and ABI types are not inferred from decompiler text.

| Address | Repository symbol | Reconciled Ghidra name | Observed behavior |
|---|---|---|---|
| `0x82170CC8` | `ProcessAndAbort_82170CC8` | `CopyByteStringChecked_82170CC8` | Copies bytes into caller-provided destination with a capacity bound; returns zero on success; invalid/exhausted cases store error 22/34 through `0x82CAB770`, invoke `0x82CAB630`, and clear destination where shown. No allocation/free established. |
| `0x82CAB770` | `sub_82CAB770` | `GetStateFieldOrFallback_82CAB770` | Calls `0x82CAC520`; returns low-32-bit nonzero result +8, otherwise address `0x832EF0F8`. Not the claimed count-times-two allocator. TLS connection is supported by the surrounding code, but this name does not assert an exact CRT identity. |
| `0x82CAB630` | `sub_82CAB630` | `InvokeCallbackOrTrap_82CAB630` | Calls pointer at `0x834F74C0` if set; otherwise calls `0x82CB6A90(2)` and traps. Not a free-forwarder. |

Locators: `fable_2_recomp.3.cpp`, `.173.cpp`, `.268.cpp` under `generated/default/`.
Actual body sizes are 224, 56 and 68 bytes respectively. The former annotations
asserted 20/16-byte allocation/free bodies inconsistent with these functions.
Memory 15 is superseded; its historical xref counts were not independently
recounted and must not be promoted to current measurements.

## Dispatch, notification and scan corrections

- `0x82242ED0`: 109,936 bytes / 27,484 PPC instructions in
  `fable_2_recomp.279.cpp`. Complete generated-body scan found zero
  `REX_CALL_INDIRECT_FUNC` occurrences; dominant instructions are floating-point
  loads/stores/multiply/multiply-add. It calls `0x82242E58` and save/restore
  helpers. This undermines the proposed callback-table scanner/main-loop role;
  its exact mathematical purpose is not newly established.
- `0x8225DC40`: 744-byte body, three calls to `0x82242ED0` and six to
  `0x8225DF28` plus save/restore. Call counts survive; assigning the three calls
  to update/render/flush phases was speculation (memories 26/30).
- `0x8229A9D8`, `PollAndProcessOsNotifications_8229A9D8`: the readable override
  explicitly invokes `XNotifyGetNext` at guest call instruction `0x8229AA20`
  (return `0x8229AA24`, target thunk `0x832B222C`). Source:
  `src/core/hotfunc/os_notifications/PollAndProcessOsNotifications_8229A9D8.cpp`.
  This replaces the inference that `0x8220BA6C` is the notification-pump call.
- `src/core/hotfunc/frame/ProcessGameFrame_82276C30.cpp` documents and implements
  frame processing and calls the notification helpers. This does not newly prove
  every top-level lifecycle/callback-table relation.
- Missing direct Ghidra xrefs do not prove indirect execution, absence of code
  calls, or a main-loop architecture. Historical slot bytes/xrefs in memories
  25/27/40 remain observations, not proof of `[function, object]` pipeline pairs.
- Memory 42's scope correction stands: nine sampled functions out of 565 are
  not an exhaustive negative; a static `.data` scan does not prove register-held
  addressing or exclude runtime/other-section references. Memory 39's stored
  hash is malformed: do not silently relabel that scan as a newly verified run.

## Bootstrap: retain structure, withdraw unjustified role claims

`0x822EA8C0` is a 104-byte bootstrap body (`fable_2_recomp.177.cpp`), not itself
a loop. That does not exclude downstream or threaded engine execution.

- `0x82CA34B0` stores incoming values at object +84/+88 and forwards the second
  argument to a lower-level operation (`fable_2_recomp.266.cpp`). This audit
  does not establish its full contract; the `0x40000` argument in the bootstrap
  is **not proof of a temporary GPU heap allocation**.
- `0x82196C58`, named `MutexLock_WaitInfinite`, sets r5 to zero and tails to
  `KeWaitForSingleObject_Guest` (`fable_2_recomp.237.cpp`). Earlier "init heap"
  interpretation is unsupported; the name alone does not prove object type.
- `0x82CBBF60`, `ProcessAndCheckSuccess_82CBBF60`, invokes a callback through
  `*(0x832EFEF0)+4`, tests signed result, calls `0x82CC1C38` on negative, and
  returns zero/one (`fable_2_recomp.122.cpp`). It is not thereby proved to be
  heap release, GPU ownership or engine handoff. Memory 23's definitive title
  exceeded its evidence.
- `0x82CC1BC0` check/failure observations are retained via memories 21/54 and
  later header-probe evidence. Calling the whole path shutdown, describing its
  creator as a debug-monitor-buffer registration, or declaring the engine-entry
  chain exhausted goes beyond those observations (memory 24).

## Original startup/heap evidence worth retaining

| Address | Agreement with local generated code | Historical evidence |
|---|---|---|
| `0x82CC16C0` | 104 bytes; lock `0x832EFEA0`, list head `0x832EFEBC`, snapshot next at node+0, callback node+8, return PC `0x82CC1710` | 54/55/66/71; generated `.121.cpp` |
| `0x82CC1728` | 108 bytes; synchronized registration/unlink routine present | 60/67/70/71; generated `.10.cpp` |
| `0x82CBE238` | 448 bytes; NT allocation/free calls present | 81; generated `.282.cpp` |
| `0x82CBE3F8` | 288 bytes; descriptor allocator call present | 82; generated `.280.cpp` |
| `0x82CBE800` | 308 bytes; save/restore dependencies present | 111/113; generated `.126.cpp` |
| `0x82CBEFC8` | 360 bytes; NT allocation, range/free-helper calls present | 110/114; generated `.12.cpp` |

These are structural matches, not a byte-for-byte revalidation of every historical
fixture, a rerun of its tests, or proof the SDK inherits prototype failure policies.
The old sources/results remain outside Git. Preserve the original passing and
failed attempts and their hash manifests before considering test ports.

## Runtime and assets

The SDK already implements areas still pending in the old prototype:

- `src/system/xthread.cpp`: allocates TLS/PCR, gives PCR to ThreadState, stores
  TLS pointer at PCR+0 and current guest thread in PCR. r13 is not direct TLS.
- `src/kernel/xboxkrnl/xboxkrnl_threading.cpp`: process type uses PCR DPC state
  or current guest thread, not a universal constant.
- `src/kernel/xboxkrnl/xboxkrnl_rtl.cpp`: 28-byte critical-section initialization.
- `src/system/kernel_state.cpp`: module-handle export and executable TLS setup.

Paths above are inside `thirdparty/rexglue-sdk/`. These source observations do not
certify every SDK lifecycle, scheduler or original-console ABI detail.

`docs/re/FABLE2_LUA_API.md` describes an analysis of 1,139 decompiled scripts, native
registration and image strings. In particular `gamescripts*.bnk`/`guiscripts.bnk`
are script banks; memory 18's blanket Wwise/FMOD audio-bank classification is not
supported. Do not infer a bank's purpose or middleware from `.bnk` alone.
The catalog's completeness counts and all script registrations were not rerun
during this migration. Other salvaged factory/property leads (memory 19) remain
unverified rather than silently becoming repository facts.

## Theseus pilot: functions identified by verified rewrites (2026-11-15/16)

Each name below is backed by a readable rewrite in `tools/theseus/rewrites/`
that matches the recompiled original on 200,000 random inputs (ABI level:
r3, non-volatile registers, memory, FPU mode, trap count) plus negative
controls. The behaviour is proven; the *role* names for #18–20 rest on the
caller evidence given. Applied in Ghidra 2026-11-16 (revert list
`docs/re/NAMES_GHIDRA_REVERT_THESEUS.tsv`); the manifest and generated code keep
the old names.

| # | Current name | Identified as | Proposed name | Evidence beyond the test |
|---|---|---|---|---|
| 1 | `FNV1Hash_821F3C28` | FNV-1 32-bit hash | (keep) | constants 0x811C9DC5 / 0x01000193 |
| 2 | `HashCstring_82188CF0` | x65599 string hash (signed chars, max 100) | (keep) | |
| 3 | `StrCmpIgnoreCase_82CA6320` | `_stricmp` (ASCII) | (keep) | |
| 4 | `StrCmp_Signed_8226D7A8` | `strcmp` on signed chars, -1/0/1 | (keep) | |
| 5 | `StringCopyN_822CD828` | `strncpy` | `strncpy` | |
| 6 | `FindSubstring_821E6388` | `strstr` | `strstr` | |
| 7 | `FindChar_82CA3980` | `strchr` | `strchr` | |
| 8 | `StrLenUTF16_8217AB30` | `wcslen` (16-bit) | `wcslen` | |
| 9 | `MemSet_SdkRuntime_82CA3190` | `memset` (unrolled CRT version) | `memset` | |
| 10 | `Ctype_Pred_Bit2_82CA6B10` | `isdigit` via the ctype table at 0x832EF350 | `isdigit` | |
| 11 | `FindChainRoot_82A596F0` | checked `std::map`/`set` iterator `++` | `MapIterator_Increment` | MSVC `_Tree` node layout, debug traps |
| 12 | `FindTreeLeaf_825CB448` | `std::map<uint32_t, 4-byte>::find` | `MapU32_Find` | |
| 13 | `GrowAndZeroBuffer_82BC55B8` | Lua 5.1 `lua_settop` | `lua_settop` | Lua state layout (top +8, base +12) |
| 14 | `NormalizeVector_82260BC8` | normalise vec4 by xyz length if >= min; returns length | `NormalizeVec3InPlace` | |
| 15 | `ComputeWrapped_8222C3E8` | `floor` (double) | `floor` | |
| 16 | `ComputeSpecialFunction_82239E88` | `cos` (double, Cody-Waite + sine series) | `cos` | constant table 0x832EEA60 |
| 17 | `ProcessAndProcessAndProcess619_82210418` | set a float4 shader constant by logical id | `SetShaderConstantById` | ID table `*0x834BDBC4`, device `*0x8336019C`, see PERF_INVESTIGATIONS_OCT_NOV_2026 |
| 18 | `ProcessAndProcessAndProcess1728_82D4F620` | Havok `hkClass::hkClass` (reflection) | `hkClass_Construct` | 323 static-init callers; first builds class "hkClass" (strings at 0x820246FC: "hkClass", "describedVersion", "items") |
| 19 | `sub_82F6C2D0` | Scaleform GFx `GASFnCall::Arg(n)` | `GFx_GASFnCall_Arg` | callers in GFx code; 0x82F11560 is AS2 `Math.abs`. Ghidra's `std_vector_helper_82F6C2D0` (stl_shape rule) is wrong |
| 20 | `sub_821E3E10` | open-addressing `uint32 -> uint32` hash map find (0 = absent) | `HashMapU32_Find` | 0x8217E6D0 looks up `*0x83496BD0` with an FNV-1 id |
