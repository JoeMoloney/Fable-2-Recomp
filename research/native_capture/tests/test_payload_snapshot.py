import sys
from pathlib import Path
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bundle import CaptureBundleError, verify_bundle
from payload_snapshot import Payload, SelectedDrawSnapshot


class PayloadSnapshotTests(unittest.TestCase):
    def test_copies_source_lifetime_and_preserves_guest_endian_and_registers(self):
        source = bytearray(b"\x01\x02\x03\x04")
        snapshot = SelectedDrawSnapshot()
        snapshot.add(Payload("ib:gen7", 7, "index_buffer", source,
                             "guest_raw", "8in16", (0x4000,), "0x1000"))
        source[:] = b"\xff" * 4
        row = snapshot.seal(["index_buffer"])[0]
        self.assertEqual(row["data"], b"\x01\x02\x03\x04")
        self.assertEqual(row["metadata"]["endian"], "8in16")
        self.assertEqual(row["metadata"]["source_registers"], [0x4000])
        with self.assertRaises(CaptureBundleError):
            snapshot.add(Payload("late:g1", 1, "vertex_buffer", b"x", "guest_raw", "none"))

    def test_missing_resource_fails_closed(self):
        snapshot = SelectedDrawSnapshot()
        snapshot.add(Payload("vs:g1", 1, "vertex_constants", b"abcd",
                             "host_shader_visible", "host_native", (0x4000,)))
        with self.assertRaisesRegex(CaptureBundleError, "missing required"):
            snapshot.seal(["index_buffer"])

    def test_concurrent_additions_and_deterministic_bundle(self):
        payloads = [Payload(f"resource-{i}:g1", 1, "vertex_buffer", bytes([i + 1]),
                            "guest_raw", "none") for i in range(12)]

        def build(destination, order):
            snapshot = SelectedDrawSnapshot()
            threads = [threading.Thread(target=snapshot.add, args=(payloads[i],))
                       for i in order]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
            metadata = {"status": "one_shot_complete", "dropped_records": 0,
                        "capture_id": "deterministic", "source_revisions": {"sdk": "base"},
                        "selector": {"name": "synthetic"}, "draw_records": [{"ordinal": 1}]}
            snapshot.write_bundle(destination, metadata, ["vertex_buffer"])
            return (destination / "manifest.json").read_bytes()

        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            first = build(root / "first", list(range(12)))
            second = build(root / "second", list(reversed(range(12))))
            self.assertEqual(first, second)
            self.assertEqual(verify_bundle(root / "first")["resource_count"], 12)

    def test_byte_limit_and_invalid_representation_fail_closed(self):
        snapshot = SelectedDrawSnapshot(max_bytes=3)
        with self.assertRaisesRegex(CaptureBundleError, "byte limit"):
            snapshot.add(Payload("too-big:g1", 1, "vertex_buffer", b"four",
                                 "guest_raw", "none"))
        with self.assertRaisesRegex(CaptureBundleError, "representation"):
            Payload("bad:g1", 1, "vertex_buffer", b"x", "unknown", "none").as_bundle_resource()


if __name__ == "__main__":
    unittest.main()
