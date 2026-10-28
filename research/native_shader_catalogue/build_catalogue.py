#!/usr/bin/env python3
"""Deterministically join existing Fable shader-bank, XSH cache and draw records."""
from __future__ import annotations

import collections
import csv
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

import xxhash


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GAME = Path("/mnt/HDD/git_stuff/my_git/Fable-2-Recomp")
SURVEY_PATH = ROOT / "research/native_reference/corpus-1/results/survey.json"
INVENTORY_PATH = GAME / "out/performance/native-renderer-feasibility-20261026/phase2/shader-inventory.json"
DRAW_PATH = HERE / "input/draws-20261027T195223Z.jsonl"
DEBUG_BANK_PATH = GAME / "data/Shaders/Shaders.sbk"
RELEASE_BANK_PATH = GAME / "data/Shaders/ShadersRelease.sbk"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dump(name: str, obj) -> None:
    (HERE / name).write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")


def get_bank_parser():
    path = ROOT / "research/native_offline/bank.py"
    spec = importlib.util.spec_from_file_location("fable_bank_parser", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = get_bank_parser()
    debug_raw, release_raw = DEBUG_BANK_PATH.read_bytes(), RELEASE_BANK_PATH.read_bytes()
    debug_bank, release_bank = parser.parse(debug_raw), parser.parse(release_raw)
    survey_raw, inventory_raw = SURVEY_PATH.read_bytes(), INVENTORY_PATH.read_bytes()
    survey, inventory = json.loads(survey_raw), json.loads(inventory_raw)

    # Stage 1 is pixel and stage 2 is vertex in ShaderBankFile v3.
    stage_names = {1: "pixel", 2: "vertex"}
    slot_names: dict[int, set[str]] = collections.defaultdict(set)
    release_ids: dict[int, set[int]] = collections.defaultdict(set)
    for entry in debug_bank["entries"]:
        if isinstance(entry["name_or_id"], str):
            slot_names[entry["slot"]].add(entry["name_or_id"])
    for entry in release_bank["entries"]:
        if isinstance(entry["name_or_id"], int):
            release_ids[entry["slot"]].add(entry["name_or_id"])

    survey_by_slot = {entry["slot"]: entry for entry in survey["shaders"]}
    cache_bytes: dict[tuple[str, int], bytes] = {}
    cache_sources: dict[tuple[str, int], list[str]] = collections.defaultdict(list)
    cache_hashes_by_stage: dict[tuple[str, int], list[dict]] = collections.defaultdict(list)
    cache_inventory = []
    for cache in inventory["caches"]:
        raw = (GAME / cache["source"]).read_bytes()
        if sha(raw) != cache["sha256"]:
            raise ValueError(f"XSH changed since inventory: {cache['source']}")
        if raw[:4] != b"XESH" or int.from_bytes(raw[4:8], "big") != 0x20201219:
            raise ValueError(f"Unexpected XSH header: {cache['source']}")
        parsed_entries = []
        offset = 8
        while offset < len(raw):
            identity, flags = struct.unpack_from("<QI", raw, offset)
            dwords, stage_bit = flags & 0x7FFFFFFF, flags >> 31
            end = offset + 12 + dwords * 4
            if not dwords or end > len(raw):
                raise ValueError(f"Malformed XSH entry: {cache['source']}:{offset}")
            code = raw[offset + 12:end]
            if xxhash.xxh3_64_intdigest(code) != identity:
                raise ValueError(f"XSH identity mismatch: {cache['source']}:{offset}")
            stage = "pixel" if stage_bit else "vertex"
            key = (stage, identity)
            cache_bytes.setdefault(key, code)
            cache_sources[key].append(cache["source"])
            cache_hashes_by_stage[key].append({
                "source": cache["source"],
                "dwords": dwords,
                "bytecode_sha256": sha(code),
            })
            parsed_entries.append({"stage": stage, "hash": f"{identity:016x}",
                                   "dwords": dwords, "bytecode_sha256": sha(code)})
            offset = end
        cache_inventory.append({
            "source": cache["source"], "sha256": sha(raw), "bytes": len(raw),
            "entry_count": len(parsed_entries),
            "entries": parsed_entries,
        })

    bank_by_hash: dict[tuple[str, int], list[dict]] = collections.defaultdict(list)
    bank_code_by_slot = {}
    catalogue_slots = []
    for slot, parsed in sorted(debug_bank["slots"].items()):
        stage = stage_names[parsed["stage"]]
        code = bytes.fromhex(parsed["microcode_hex"])
        if len(code) % 4:
            raise ValueError(f"Non-dword microcode in slot {slot}")
        identity = xxhash.xxh3_64_intdigest(code)
        bank_by_hash[(stage, identity)].append({"slot": slot, "stage": stage})
        bank_code_by_slot[slot] = code
        result = survey_by_slot.get(slot)
        if result is None or result["stage"] != ("vs" if stage == "vertex" else "ps"):
            raise ValueError(f"Survey slot/stage mismatch: {slot}")
        catalogue_slots.append({
            "slot": slot,
            "stage": stage,
            "debug_names": sorted(slot_names[slot]),
            "release_name_hashes_u32": sorted(release_ids[slot]),
            "debug_container_sha256": parsed["sha256"],
            "microcode_sha256": parsed["microcode_sha256"],
            "microcode_xxh3_64": f"{identity:016x}",
            "microcode_bytes": len(code),
            "microcode_dwords": len(code) // 4,
            "vertex_elements": parsed["vertex_elements"],
            "reflected_constants": parsed["constants"],
            "hlsl_emitted": result["emit_exit"] == 0,
            "hlsl_sha256": result.get("hlsl_sha256"),
            "spirv_compiled": result.get("compile_exit") == 0,
            "spirv_validated": result.get("validate_exit") == 0,
            "spirv_sha256": result.get("spirv_sha256"),
            "emitter_exit": result["emit_exit"],
            "compiler_exit": result.get("compile_exit"),
            "validator_exit": result.get("validate_exit"),
        })

    # Detect exact bank shaders, and a conservative vertex-fetch-patch candidate:
    # equal size, with every differing word inside a 3-dword instruction whose
    # bank-source opcode bits identify vfetch. This is evidence for a likely
    # parent, not a semantic identity proof.
    unique_draws: dict[tuple[str, int], dict] = {}
    pair_counts = collections.Counter()
    draw_lines = DRAW_PATH.read_bytes().splitlines()
    parsed_lines = [json.loads(line) for line in draw_lines]
    headers = [item for item in parsed_lines if item.get("type") == "header"]
    draws = [item for item in parsed_lines if item.get("type") == "draw"]
    if len(headers) != 1 or len(draws) != headers[0]["records"]:
        raise ValueError("Draw JSONL header/count mismatch")
    for draw in draws:
        for stage, field in (("vertex", "vs_hash"), ("pixel", "ps_hash")):
            identity = int(draw[field])
            key = (stage, identity)
            row = unique_draws.setdefault(key, {
                "stage": stage,
                "runtime_hash": f"{identity:016x}",
                "zero_hash": identity == 0,
                "draw_count": 0,
                "dword_counts": set(),
                "bank_exact_slots": [],
                "cache_sources": [],
                "cache_bytecode_sha256": [],
                "fetch_patch_parent_candidates": [],
            })
            row["draw_count"] += 1
            row["dword_counts"].add(draw["vs_dwords" if stage == "vertex" else "ps_dwords"])
        pair_counts[(int(draw["vs_hash"]), int(draw["ps_hash"]))] += 1

    for key, row in unique_draws.items():
        stage, identity = key
        exact = bank_by_hash.get(key, []) if identity else []
        row["bank_exact_slots"] = sorted(item["slot"] for item in exact)
        row["cache_sources"] = sorted(set(cache_sources.get(key, [])))
        row["cache_bytecode_sha256"] = sorted({
            item["bytecode_sha256"] for item in cache_hashes_by_stage.get(key, [])
        })
        row["cache_dword_counts"] = sorted({
            item["dwords"] for item in cache_hashes_by_stage.get(key, [])
        })
        if stage == "vertex" and identity and key in cache_bytes and not exact:
            code = cache_bytes[key]
            words = struct.unpack(">" + "I" * (len(code) // 4), code)
            candidates = []
            for slot, bank_code in bank_code_by_slot.items():
                if debug_bank["slots"][slot]["stage"] != 2 or len(bank_code) != len(code):
                    continue
                bank_words = struct.unpack(">" + "I" * (len(code) // 4), bank_code)
                differing = [i for i, (a, b) in enumerate(zip(words, bank_words)) if a != b]
                changed_instructions = sorted({i // 3 for i in differing})
                # Xenos vertex-fetch opcode is 0 in the low five bits.
                if all((bank_words[instruction * 3] & 0x1F) == 0
                       for instruction in changed_instructions):
                    candidates.append({
                        "slot": slot,
                        "debug_names": sorted(slot_names[slot]),
                        "differing_dword_indices": differing,
                        "changed_instruction_indices": changed_instructions,
                    })
            row["fetch_patch_parent_candidates"] = sorted(candidates, key=lambda c: c["slot"])
        row["dword_counts"] = sorted(row["dword_counts"])

    observed_by_slot: dict[int, set[str]] = collections.defaultdict(set)
    for (stage, identity), row in unique_draws.items():
        if identity == 0:
            continue
        for slot in row["bank_exact_slots"]:
            observed_by_slot[slot].add(row["runtime_hash"])
        for candidate in row["fetch_patch_parent_candidates"]:
            observed_by_slot[candidate["slot"]].add(row["runtime_hash"])
    for slot in catalogue_slots:
        slot["observed_runtime_hashes"] = sorted(observed_by_slot[slot["slot"]])

    shader_lookup = {(entry["stage"], entry["slot"]): entry for entry in catalogue_slots}
    pairs = []
    for (vs_hash, ps_hash), count in sorted(pair_counts.items(), key=lambda kv: (-kv[1], kv[0])):
        vs = unique_draws[("vertex", vs_hash)]
        ps = unique_draws[("pixel", ps_hash)]
        vs_slots = vs["bank_exact_slots"]
        vs_match_kind = "exact_bank_microcode" if vs_slots else "unresolved"
        if not vs_slots and vs["fetch_patch_parent_candidates"]:
            vs_slots = [item["slot"] for item in vs["fetch_patch_parent_candidates"]]
            vs_match_kind = "fetch-patch-parent-candidate"
        ps_slots = ps["bank_exact_slots"]
        exact_compilable = [
            {"vs_slot": vs_slot, "ps_slot": ps_slot}
            for vs_slot in vs["bank_exact_slots"]
            for ps_slot in ps_slots
            if shader_lookup[("vertex", vs_slot)]["spirv_validated"]
            and shader_lookup[("pixel", ps_slot)]["spirv_validated"]
        ]
        candidate_compilable = [
            {"vs_slot_candidate": vs_slot, "ps_slot": ps_slot}
            for vs_slot in vs_slots
            for ps_slot in ps_slots
            if shader_lookup[("vertex", vs_slot)]["spirv_validated"]
            and shader_lookup[("pixel", ps_slot)]["spirv_validated"]
        ]
        pairs.append({
            "vs_hash": f"{vs_hash:016x}",
            "ps_hash": f"{ps_hash:016x}",
            "draw_frequency": count,
            "vs_bank_match_kind": vs_match_kind,
            "vs_candidate_slots": vs_slots,
            "ps_exact_bank_slots": ps_slots,
            "exact_pair_has_validated_spirv_both_stages": bool(exact_compilable),
            "exact_compilable_slot_combinations": exact_compilable,
            "candidate_pair_has_validated_spirv_both_stages": bool(candidate_compilable),
            "candidate_compilable_slot_combinations": candidate_compilable,
        })

    unique_rows = []
    for key, row in sorted(unique_draws.items()):
        unique_rows.append(row)
    stage_stats = {}
    for stage in ("vertex", "pixel"):
        rows = [row for row in unique_rows if row["stage"] == stage]
        nonzero = [row for row in rows if not row["zero_hash"]]
        stage_stats[stage] = {
            "draw_records": sum(row["draw_count"] for row in rows),
            "zero_hash_records": sum(row["draw_count"] for row in rows if row["zero_hash"]),
            "unique_nonzero_hashes": len(nonzero),
            "exact_bank_hashes": sum(bool(row["bank_exact_slots"]) for row in nonzero),
            "cache_hashes": sum(bool(row["cache_sources"]) for row in nonzero),
            "nonbank_exact_but_fetch_patch_candidates": sum(
                bool(row["fetch_patch_parent_candidates"]) for row in nonzero
            ),
            "no_exact_bank_or_fetch_patch_candidate": sum(
                not row["bank_exact_slots"] and not row["fetch_patch_parent_candidates"]
                for row in nonzero
            ),
        }

    pair_summary = {
        "draw_count": len(draws),
        "unique_pairs_including_zero_pixel_hash": len(pair_counts),
        "unique_pairs_nonzero_pixel_hash": len([p for p in pairs if p["ps_hash"] != "0000000000000000"]),
        "draws_with_zero_pixel_hash": sum(p["draw_frequency"] for p in pairs if p["ps_hash"] == "0000000000000000"),
        "exact_pairs_with_validated_spirv_both_stages": sum(
            p["draw_frequency"] for p in pairs if p["exact_pair_has_validated_spirv_both_stages"]
        ),
        "unique_exact_pairs_with_validated_spirv_both_stages": sum(
            p["exact_pair_has_validated_spirv_both_stages"] for p in pairs
        ),
        "candidate_pairs_with_validated_spirv_both_stages": sum(
            p["draw_frequency"] for p in pairs if p["candidate_pair_has_validated_spirv_both_stages"]
        ),
    }

    catalogue = {
        "schema": 1,
        "description": "All ShaderBankFile v3 slots, with reproducible bank/cache/runtime hash joins.",
        "provenance": {
            "debug_bank": {"path": str(DEBUG_BANK_PATH), "sha256": sha(debug_raw)},
            "release_bank": {"path": str(RELEASE_BANK_PATH), "sha256": sha(release_raw)},
            "shader_survey": {"path": str(SURVEY_PATH.relative_to(ROOT)), "sha256": sha(survey_raw)},
            "xsh_inventory": {"path": str(INVENTORY_PATH), "sha256": sha(inventory_raw)},
            "runtime_draw_jsonl": {"path": str(DRAW_PATH.relative_to(ROOT)), "sha256": sha(DRAW_PATH.read_bytes()),
                                   "capture_id": "20261027T195223Z", "header": headers[0]},
        },
        "counts": {
            "slots": len(catalogue_slots),
            "vertex_slots": sum(s["stage"] == "vertex" for s in catalogue_slots),
            "pixel_slots": sum(s["stage"] == "pixel" for s in catalogue_slots),
            "hlsl_emitted": sum(s["hlsl_emitted"] for s in catalogue_slots),
            "spirv_compiled": sum(s["spirv_compiled"] for s in catalogue_slots),
            "spirv_validated": sum(s["spirv_validated"] for s in catalogue_slots),
        },
        "hash_semantics": {
            "bank_identity": "XXH3-64 over exact extracted raw microcode bytes (guest byte order)",
            "runtime_identity": "unsigned 64-bit vs_hash/ps_hash in draw-identity records",
            "cache_identity": "XXH3-64 stored in XESH .xsh, revalidated over cache bytecode",
            "exact_match": "runtime XXH3 equals a bank slot's raw microcode XXH3",
            "vertex_fetch_candidate": "same-size XSH VS differs only in dwords belonging to bank instructions whose low-five-bit opcode is vertex-fetch (0); heuristic candidate, not proven ancestry",
        },
        "slots": catalogue_slots,
    }
    dump("shader_catalogue.json", catalogue)
    dump("runtime_shader_matches.json", {
        "schema": 1,
        "capture_header": headers[0],
        "stage_statistics": stage_stats,
        "observed_unique_shaders": unique_rows,
        "xsh_cache_sources": cache_inventory,
    })
    dump("runtime_shader_pairs.json", {
        "schema": 1,
        "capture_header": headers[0],
        "summary": pair_summary,
        "pairs": pairs,
    })
    with (HERE / "runtime_shader_pairs.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=[
            "vs_hash", "ps_hash", "draw_frequency", "vs_bank_match_kind",
            "vs_candidate_slots", "ps_exact_bank_slots",
            "exact_pair_has_validated_spirv_both_stages",
            "candidate_pair_has_validated_spirv_both_stages",
        ])
        writer.writeheader()
        for pair in pairs:
            writer.writerow({key: json.dumps(pair[key], separators=(",", ":"))
                             if isinstance(pair[key], (list, dict)) else pair[key]
                             for key in writer.fieldnames})
    dump("catalogue_manifest.json", {
        "schema": 1,
        "inputs": catalogue["provenance"],
        "outputs": {
            "shader_catalogue.json": sha((HERE / "shader_catalogue.json").read_bytes()),
            "runtime_shader_matches.json": sha((HERE / "runtime_shader_matches.json").read_bytes()),
            "runtime_shader_pairs.json": sha((HERE / "runtime_shader_pairs.json").read_bytes()),
            "runtime_shader_pairs.csv": sha((HERE / "runtime_shader_pairs.csv").read_bytes()),
        },
        "counts": catalogue["counts"],
        "runtime_stage_statistics": stage_stats,
        "runtime_pair_summary": pair_summary,
    })
    print(json.dumps({"counts": catalogue["counts"], "stage_statistics": stage_stats,
                      "pair_summary": pair_summary}, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
