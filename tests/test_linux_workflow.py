"""Linux staging/launch checks with synthetic content; no game dump required."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "linux_workflow", Path(__file__).resolve().parents[1] / "tools/linux.py")
workflow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workflow)


class LinuxWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "dump"
        self.destination = self.root / "checkout"
        self.destination.mkdir()
        (self.source / "data").mkdir(parents=True)
        (self.source / "default.xex").write_bytes(b"synthetic executable")
        (self.source / "data/marker").write_bytes(b"content")
        (self.source / "data/dir.manifest").write_bytes(b"marker\r\n")
        self.version = dict(hash=hashlib.sha256(b"synthetic executable").hexdigest(),
                            xexSize=20, compatible=True, name="Synthetic",
                            requiredFiles=["data/marker"], rejectFiles=["data/tu.bnk"],
                            rejectDirectories=[])
        self.catalogue = {"versions": [self.version]}

    def stage(self):
        return workflow.stage_content(self.source, self.destination, self.catalogue)

    def test_copy_is_independent_and_repeatable(self):
        self.stage()
        self.stage()
        self.assertFalse((self.destination / "default.xex").is_symlink())
        (self.destination / "data/marker").write_bytes(b"changed")
        self.assertEqual((self.source / "data/marker").read_bytes(), b"content")

    def test_preserves_runtime_manifest_and_extra_lua(self):
        self.stage()
        (self.destination / "data/dir.manifest").write_text("updated locally")
        (self.destination / "data/custom.lua").write_text("local script")
        self.stage()
        self.assertEqual((self.destination / "data/dir.manifest").read_text(), "updated locally")
        self.assertTrue((self.destination / "data/custom.lua").exists())

    def test_rejects_unknown_xex(self):
        (self.source / "default.xex").write_bytes(b"unsupported")
        with self.assertRaises(ValueError):
            self.stage()
        self.assertFalse((self.destination / "data").exists())

    def test_rejects_missing_or_conflicting_markers(self):
        (self.source / "data/tu.bnk").touch()
        with self.assertRaises(ValueError):
            self.stage()
        (self.source / "data/tu.bnk").unlink()
        (self.source / "data/marker").unlink()
        with self.assertRaises(ValueError):
            self.stage()

    def test_refuses_overwriting_existing_xex(self):
        target = self.destination / "default.xex"
        target.write_bytes(b"user work")
        with self.assertRaises(ValueError):
            self.stage()
        self.assertEqual(target.read_bytes(), b"user work")

    def test_rejects_symlinked_staging_and_source(self):
        (self.destination / "data").symlink_to(self.source / "data", target_is_directory=True)
        with self.assertRaises(ValueError):
            self.stage()
        (self.destination / "data").unlink()
        (self.source / "data/link").symlink_to(self.source / "default.xex")
        with self.assertRaises(ValueError):
            self.stage()

    def test_rejects_external_xex_symlink(self):
        (self.destination / "default.xex").symlink_to(self.source / "default.xex")
        with self.assertRaises(ValueError):
            self.stage()

    def test_refuses_partial_existing_staging(self):
        (self.destination / "data").mkdir()
        with self.assertRaises(ValueError):
            self.stage()

    def test_rejects_nested_source(self):
        with self.assertRaises(ValueError):
            workflow.stage_content(self.source, self.root, self.catalogue)

    def test_launch_uses_sync_shaders_wayland_and_no_timer(self):
        import argparse
        build = self.destination / "out/build"
        build.mkdir(parents=True)
        (build / "fable_2").touch()
        args = argparse.Namespace(backend="wayland", gpu=0, async_shaders=False,
                                  extra=["--", "--mouse_look=true"])
        with patch.object(workflow, "ROOT", self.destination), \
             patch.object(workflow, "BUILD", build), \
             patch.object(workflow.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(workflow.launch(args), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[0], str(build / "fable_2"))
            self.assertIn("--async_shader_compilation=false", command)
            self.assertEqual(command[-1], "--mouse_look=true")
            self.assertEqual(run.call_args.kwargs["env"]["SDL_VIDEODRIVER"], "wayland")
            self.assertNotIn("timeout", run.call_args.kwargs)

    def test_build_configures_again_after_codegen(self):
        import argparse
        import json
        (self.destination / "constants").mkdir()
        (self.destination / "constants/game_versions.json").write_text(json.dumps(self.catalogue))
        self.stage()
        args = argparse.Namespace(cc="clang", cxx="clang++", jobs=2)
        with patch.object(workflow, "ROOT", self.destination), \
             patch.object(workflow, "prepare_sdk") as prepare, \
             patch.object(workflow, "run") as run:
            workflow.build(args)
            prepare.assert_called_once()
            calls = [call.args[0] for call in run.call_args_list]
            self.assertEqual(len(calls), 4)
            self.assertEqual(calls[0], calls[2])
            self.assertIn("fable_2_codegen", calls[1])
            self.assertIn("fable_2", calls[3])
            self.assertIn("-DCMAKE_CXX_FLAGS=-march=x86-64-v2", calls[0])


if __name__ == "__main__":
    unittest.main()
