import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bundle import CaptureBundleError, build_bundle, pack_capture, verify_bundle


def resources():
    return [
        {"resource_id": "vertex-fetch-94:g1", "generation": 1, "kind": "vertex_buffer",
         "guest_address": "0x1be80000", "data": bytes(range(64)),
         "metadata": {"fetch_slot": 94, "stride": 20}},
        {"resource_id": "index-buffer:g1", "generation": 1, "kind": "index_buffer",
         "guest_address": "0x1beb1200", "data": bytes([0, 1, 2, 3]) * 48,
         "metadata": {"format": "uint16", "endian": "8in16", "count": 192}},
        {"resource_id": "vs-constants:g4", "generation": 4, "kind": "vertex_constants",
         "data": bytes(768), "metadata": {"register_count": 48}},
        {"resource_id": "ps-constants:g2", "generation": 2, "kind": "pixel_constants",
         "data": bytes(range(128)) * 4, "metadata": {"register_count": 32}},
        {"resource_id": "texture-0:g3", "generation": 3, "kind": "texture_mip",
         "guest_address": "0x19000000", "data": b"RGBA8 synthetic mip 0",
         "metadata": {"fetch_slot": 0, "mip": 0, "format": "R8G8B8A8_UNORM"}},
        {"resource_id": "target-before:g1", "generation": 1, "kind": "render_target_before",
         "data": b"pre-draw color/depth sample"},
        {"resource_id": "target-after:g1", "generation": 1, "kind": "render_target_after",
         "data": b"post-draw color/depth sample"},
    ]


def metadata(draw_records=None, status="one_shot_complete", dropped=0):
    return {
        "status": status,
        "dropped_records": dropped,
        "capture_id": "synthetic-town-square-test",
        "source_revisions": {"app": "abc123", "sdk": "def456"},
        "selector": {"vs_hash": "79eac49585797037", "ps_hash": "401a01fd4e5f8757"},
        "required_resource_kinds": ["vertex_buffer", "index_buffer", "vertex_constants",
                                     "pixel_constants", "texture_mip", "render_target_before",
                                     "render_target_after"],
        "draw_records": draw_records if draw_records is not None else [{"ordinal": 1}],
    }


class BundleTests(unittest.TestCase):
    def test_synthetic_capture_round_trips_content_addressed_blobs(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / "capture"
            rows = resources()
            # Duplicate content is deliberately deduplicated by SHA-256.
            rows.append({"resource_id": "repeat-buffer:g1", "generation": 1,
                         "kind": "vertex_buffer_copy", "data": rows[0]["data"]})
            manifest = build_bundle(bundle, metadata(), rows)
            verified = verify_bundle(bundle)
            self.assertEqual(verified["resource_count"], 8)
            self.assertEqual(len({item["sha256"] for item in verified["resources"]}), 7)
            self.assertEqual(manifest["status"], "complete")
            self.assertEqual(verified["draw_records"], [{"ordinal": 1}])
            self.assertEqual(verified["source_revisions"]["sdk"], "def456")

    def test_tampered_payload_fails_verification(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / "capture"
            build_bundle(bundle, metadata(), resources())
            manifest = json.loads((bundle / "manifest.json").read_text())
            blob = bundle / manifest["resources"][0]["blob"]
            blob.write_bytes(blob.read_bytes() + b"tamper")
            with self.assertRaises(CaptureBundleError):
                verify_bundle(bundle)

    def test_incomplete_dropped_or_multiple_draw_capture_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            for meta in (metadata(status="capacity_reached_truncated"),
                         metadata(dropped=1),
                         metadata(draw_records=[{"ordinal": 1}, {"ordinal": 2}])):
                target = parent / f"bad-{len(list(parent.iterdir()))}"
                with self.assertRaises(CaptureBundleError):
                    build_bundle(target, meta, resources())
                self.assertFalse(target.exists())

    def test_duplicate_identity_generation_and_byte_caps_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            duplicate = resources() + [resources()[0]]
            with self.assertRaises(CaptureBundleError):
                build_bundle(parent / "duplicate", metadata(), duplicate)
            with self.assertRaises(CaptureBundleError):
                build_bundle(parent / "cap", metadata(), resources(), max_total_bytes=64)
            bad_generation = resources()
            bad_generation[0]["generation"] = -1
            with self.assertRaises(CaptureBundleError):
                build_bundle(parent / "generation", metadata(), bad_generation)
            self.assertFalse(any(parent.iterdir()))

    def test_required_resource_kind_and_raw_capture_file_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "raw"
            source.mkdir()
            resources_data = resources()
            for i, item in enumerate(resources_data):
                (source / f"payload-{i}.bin").write_bytes(item["data"])
            meta = metadata()
            meta["resources"] = [
                {k: v for k, v in item.items() if k != "data"} |
                {"source_file": f"payload-{i}.bin"}
                for i, item in enumerate(resources_data)
            ]
            metadata_path = root / "raw-metadata.json"
            metadata_path.write_text(json.dumps(meta))
            packed = root / "packed"
            bundle = pack_capture(source, metadata_path, packed)
            self.assertEqual(verify_bundle(packed)["resource_count"], 7)
            self.assertEqual(bundle["status"], "complete")

            missing = metadata()
            missing["required_resource_kinds"].append("depth_attachment")
            with self.assertRaises(CaptureBundleError):
                build_bundle(root / "missing-kind", missing, resources())

            unsafe = json.loads(metadata_path.read_text())
            unsafe["resources"][0]["source_file"] = "../outside.bin"
            bad_metadata_path = root / "bad-metadata.json"
            bad_metadata_path.write_text(json.dumps(unsafe))
            with self.assertRaises((CaptureBundleError, FileNotFoundError)):
                pack_capture(source, bad_metadata_path, root / "unsafe")


if __name__ == "__main__":
    unittest.main(verbosity=2)
