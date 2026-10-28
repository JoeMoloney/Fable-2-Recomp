"""Versioned, content-addressed writer/verifier for one-draw capture bundles.

The runtime side may export bounded raw resources to a private staging directory;
this module creates the durable manifest and SHA-256-addressed blobs on desktop.
It never reads or writes a game installation.
"""
from __future__ import annotations

import hashlib
import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile


class CaptureBundleError(ValueError):
    pass


_RESOURCE_ID = re.compile(r"^[A-Za-z0-9._:@+-]{1,192}$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_bundle(destination: Path, metadata: dict, resources: list[dict],
                 max_total_bytes: int = 256 * 1024 * 1024,
                 max_blob_bytes: int = 128 * 1024 * 1024) -> dict:
    """Write one complete draw and its resource blobs; refuses partial captures."""
    destination = Path(destination)
    if destination.exists():
        raise CaptureBundleError(f"destination already exists: {destination}")
    if metadata.get("status") not in ("one_shot_complete", "complete"):
        raise CaptureBundleError("capture status is not complete")
    if metadata.get("dropped_records", 0) != 0:
        raise CaptureBundleError("capture reports dropped records")
    draws = metadata.get("draw_records")
    if not isinstance(draws, list) or len(draws) != 1:
        raise CaptureBundleError("bundle must contain exactly one selected draw")
    if not isinstance(metadata.get("capture_id"), str) or not metadata["capture_id"]:
        raise CaptureBundleError("capture_id is required")
    if not isinstance(metadata.get("source_revisions"), dict) or not metadata["source_revisions"]:
        raise CaptureBundleError("source_revisions provenance is required")
    if not isinstance(metadata.get("selector"), dict) or not metadata["selector"]:
        raise CaptureBundleError("selector provenance is required")
    if not isinstance(resources, list) or not resources:
        raise CaptureBundleError("bundle has no resource payloads")
    if max_total_bytes <= 0 or max_blob_bytes <= 0:
        raise CaptureBundleError("capture byte limits must be positive")

    seen_ids: set[str] = set()
    prepared = []
    total_bytes = 0
    for item in resources:
        resource_id = item.get("resource_id")
        kind = item.get("kind")
        generation = item.get("generation")
        data = item.get("data")
        if not isinstance(resource_id, str) or not _RESOURCE_ID.fullmatch(resource_id):
            raise CaptureBundleError("invalid resource_id")
        if resource_id in seen_ids:
            raise CaptureBundleError(f"duplicate resource_id: {resource_id}")
        seen_ids.add(resource_id)
        if not isinstance(kind, str) or not kind:
            raise CaptureBundleError(f"missing resource kind: {resource_id}")
        if not isinstance(generation, int) or generation < 0:
            raise CaptureBundleError(f"invalid resource generation: {resource_id}")
        if not isinstance(data, bytes) or not data:
            raise CaptureBundleError(f"empty or non-bytes payload: {resource_id}")
        if len(data) > max_blob_bytes:
            raise CaptureBundleError(f"resource exceeds per-blob limit: {resource_id}")
        total_bytes += len(data)
        if total_bytes > max_total_bytes:
            raise CaptureBundleError("capture exceeds aggregate byte limit")
        prepared.append((item, data, _sha256(data)))

    required_kinds = set(metadata.get("required_resource_kinds", []))
    present_kinds = {item["kind"] for item, _, _ in prepared}
    if not required_kinds.issubset(present_kinds):
        raise CaptureBundleError(
            f"missing required payload kinds: {sorted(required_kinds - present_kinds)}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".draw-capture-", dir=destination.parent))
    try:
        blob_dir = temporary / "blobs"
        blob_dir.mkdir()
        manifest_resources = []
        unique_blob_bytes = 0
        written_hashes: set[str] = set()
        for item, data, digest in prepared:
            relative_blob = f"blobs/{digest}.bin"
            if digest not in written_hashes:
                (temporary / relative_blob).write_bytes(data)
                written_hashes.add(digest)
                unique_blob_bytes += len(data)
            manifest_resources.append({
                "resource_id": item["resource_id"],
                "generation": item["generation"],
                "kind": item["kind"],
                "guest_address": item.get("guest_address"),
                "byte_length": len(data),
                "sha256": digest,
                "blob": relative_blob,
                "metadata": item.get("metadata", {}),
            })
        manifest = {
            "schema": 1,
            "status": "complete",
            "capture_id": metadata.get("capture_id"),
            "source_revisions": metadata.get("source_revisions", {}),
            "selector": metadata.get("selector", {}),
            "required_resource_kinds": sorted(required_kinds),
            "draw_records": draws,
            "dropped_records": 0,
            "resource_count": len(manifest_resources),
            "resource_payload_bytes": total_bytes,
            "unique_blob_bytes": unique_blob_bytes,
            "resources": manifest_resources,
        }
        manifest_path = temporary / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        verify_bundle(temporary)
        temporary.rename(destination)
        return manifest
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def verify_bundle(bundle: Path, max_total_bytes: int = 256 * 1024 * 1024) -> dict:
    bundle = Path(bundle)
    manifest_path = bundle / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or manifest.get("status") != "complete":
        raise CaptureBundleError("unsupported/incomplete bundle manifest")
    if manifest.get("dropped_records") != 0 or len(manifest.get("draw_records", [])) != 1:
        raise CaptureBundleError("bundle is not a complete single-draw capture")
    resources = manifest.get("resources")
    if not isinstance(resources, list) or len(resources) != manifest.get("resource_count"):
        raise CaptureBundleError("resource count mismatch")
    required_kinds = set(manifest.get("required_resource_kinds", []))
    present_kinds = {item.get("kind") for item in resources}
    if not required_kinds.issubset(present_kinds):
        raise CaptureBundleError("bundle is missing required resource kinds")
    seen_ids = set()
    total = 0
    unique: dict[str, int] = {}
    for item in resources:
        resource_id = item.get("resource_id")
        if resource_id in seen_ids or not isinstance(resource_id, str) or not _RESOURCE_ID.fullmatch(resource_id):
            raise CaptureBundleError("invalid/duplicate resource identity")
        seen_ids.add(resource_id)
        digest = item.get("sha256")
        blob = item.get("blob")
        if not isinstance(digest, str) or not _SHA256.fullmatch(digest):
            raise CaptureBundleError(f"invalid digest: {resource_id}")
        if blob != f"blobs/{digest}.bin":
            raise CaptureBundleError(f"non-content-addressed blob path: {resource_id}")
        path = bundle / blob
        resolved_bundle = bundle.resolve()
        resolved_path = path.resolve()
        if resolved_bundle not in resolved_path.parents or (bundle / "blobs").is_symlink() or \
           not path.is_file() or path.is_symlink():
            raise CaptureBundleError(f"missing or unsafe blob: {resource_id}")
        data = path.read_bytes()
        if len(data) != item.get("byte_length") or _sha256(data) != digest:
            raise CaptureBundleError(f"blob size/hash mismatch: {resource_id}")
        generation = item.get("generation")
        if not isinstance(generation, int) or generation < 0:
            raise CaptureBundleError(f"invalid generation: {resource_id}")
        total += len(data)
        unique[digest] = len(data)
    if total != manifest.get("resource_payload_bytes"):
        raise CaptureBundleError("aggregate resource byte count mismatch")
    if total > max_total_bytes:
        raise CaptureBundleError("bundle exceeds aggregate byte limit")
    if sum(unique.values()) != manifest.get("unique_blob_bytes"):
        raise CaptureBundleError("unique blob byte count mismatch")
    return manifest


def pack_capture(source_dir: Path, metadata_path: Path, destination: Path,
                 max_total_bytes: int = 256 * 1024 * 1024) -> dict:
    """Package a raw one-draw metadata file and relative payload files."""
    source_dir = Path(source_dir).resolve(strict=True)
    metadata = json.loads(Path(metadata_path).read_text())
    resources = metadata.pop("resources", None)
    if not isinstance(resources, list):
        raise CaptureBundleError("metadata resources must be a list")
    payloads = []
    total = 0
    for item in resources:
        item = dict(item)
        source_name = item.pop("source_file", None)
        if not isinstance(source_name, str):
            raise CaptureBundleError("resource source_file is required")
        candidate = source_dir / source_name
        if candidate.is_symlink():
            raise CaptureBundleError("resource source symlinks are not permitted")
        source = candidate.resolve(strict=True)
        if source_dir not in source.parents or not source.is_file():
            raise CaptureBundleError("resource source path escapes the capture input directory")
        total += source.stat().st_size
        if total > max_total_bytes:
            raise CaptureBundleError("capture exceeds aggregate byte limit")
        item["data"] = source.read_bytes()
        payloads.append(item)
    return build_bundle(destination, metadata, payloads, max_total_bytes=max_total_bytes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("metadata_json", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--max-total-bytes", type=int, default=256 * 1024 * 1024)
    args = parser.parse_args()
    try:
        manifest = pack_capture(args.source_dir, args.metadata_json, args.destination,
                                args.max_total_bytes)
    except (CaptureBundleError, OSError, json.JSONDecodeError) as error:
        print(f"capture bundle rejected: {error}", file=sys.stderr)
        return 1
    print(json.dumps({
        "status": manifest["status"],
        "capture_id": manifest["capture_id"],
        "draw_records": len(manifest["draw_records"]),
        "resource_count": manifest["resource_count"],
        "resource_payload_bytes": manifest["resource_payload_bytes"],
        "unique_blob_bytes": manifest["unique_blob_bytes"],
        "destination": str(args.destination),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
