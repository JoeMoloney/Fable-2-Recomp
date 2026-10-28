#!/usr/bin/env python3
"""Select one recorded target draw by shader, geometry and full stable state."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct


FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211


def _add_u32(state: int, value: int) -> int:
    for shift in (0, 8, 16, 24):
        state ^= (value >> shift) & 0xFF
        state = (state * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return state


def _as_int(value) -> int:
    if isinstance(value, str):
        return int(value, 0)
    return int(value)


def state_signature(record: dict) -> int:
    """FNV-1a-64 over stable draw inputs; excludes host pointers and ordinal."""
    state = FNV64_OFFSET

    def u32(value):
        nonlocal state
        state = _add_u32(state, _as_int(value))

    def u64(value):
        value = _as_int(value)
        u32(value & 0xFFFFFFFF)
        u32((value >> 32) & 0xFFFFFFFF)

    u64(record["vs_hash"])
    u64(record["ps_hash"])
    for field in (
        "primitive", "index_count", "source_select", "major_mode",
        "major_mode_explicit", "index_info_present", "index_format",
        "index_endian", "index_base", "index_length", "index_buffer_count",
        "guest_vertex_count", "host_vertex_count", "processed_guest_index_base",
        "host_index_format", "host_index_endian", "index_buffer_type",
        "host_vertex_shader_type", "tessellation_mode",
    ):
        u32(record[field])
    for word in record["render_target_registers"]:
        u32(word)
    for value in record["viewport"]:
        u32(struct.unpack("<I", struct.pack("<f", float(value)))[0])
    for value in record["scissor"]:
        u32(value)
    mask = record["vertex_fetch_mask"]
    for word in mask:
        u32(word)
    for fetch in sorted(record["vertex_fetches"], key=lambda item: item["index"]):
        u32(fetch["index"])
        for word in fetch["words"]:
            u32(word)
    u32(record["texture_fetch_mask"])
    for fetch in sorted(record["texture_fetches"], key=lambda item: item["index"]):
        u32(fetch["index"])
        for word in fetch["words"]:
            u32(word)
    return state


def matches(record: dict, selector: dict) -> bool:
    expected = selector["match"]
    if _as_int(record["vs_hash"]) != int(selector["vs_hash"], 16):
        return False
    if _as_int(record["ps_hash"]) != int(selector["ps_hash"], 16):
        return False
    for key, value in expected.items():
        if key == "vertex_fetches":
            actual = {str(f["index"]): [_as_int(w) for w in f["words"]]
                      for f in record.get("vertex_fetches", [])}
            if isinstance(value, dict):
                if any(actual.get(index) != [_as_int(w) for w in words]
                       for index, words in value.items()):
                    return False
            else:
                expected_fetches = {str(f["index"]): [_as_int(w) for w in f["words"]]
                                    for f in value}
                if actual != expected_fetches:
                    return False
        elif key == "texture_fetches":
            actual = [{"index": int(f["index"]),
                       "words": [_as_int(w) for w in f["words"]]}
                      for f in record.get("texture_fetches", [])]
            expected_textures = [{"index": int(f["index"]),
                                  "words": [_as_int(w) for w in f["words"]]}
                                 for f in value]
            if actual != expected_textures:
                return False
        elif record.get(key) != value:
            return False
    return state_signature(record) == int(selector["state_signature_fnv64"], 16)


def select_records(capture: Path, selector: dict) -> tuple[dict, list[dict]]:
    rows = [json.loads(line) for line in capture.read_text().splitlines() if line.strip()]
    if not rows or rows[0].get("type") != "header":
        raise ValueError("capture lacks a JSONL header")
    draws = [row for row in rows[1:] if row.get("type") == "draw"]
    matches_found = [row for row in draws if matches(row, selector)]
    return rows[0], matches_found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--selector", type=Path, default=Path(__file__).with_name(
        "selector-standard-material-v1.json"))
    args = parser.parse_args()
    selector = json.loads(args.selector.read_text())
    header, found = select_records(args.capture, selector)
    print(json.dumps({
        "capture_status": header.get("status"),
        "capture_records": header.get("records"),
        "capture_dropped": header.get("dropped_records"),
        "selector": selector["selector_id"],
        "selector_state_signature_fnv64": selector["state_signature_fnv64"],
        "matching_records": len(found),
        "first_matching_ordinal": found[0]["ordinal"] if found else None,
    }, indent=2, sort_keys=True))
    return 0 if found else 2


if __name__ == "__main__":
    raise SystemExit(main())
