"""Small CPU tests for the local, ABI-preserving Xenos 1D extension."""
import hashlib
import importlib.util
import json
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "upstream/xenosrecomp/XenosRecomp/shader_recompiler.cpp"
COMMON = HERE / "xenos_1d_common.h"
MANIFEST = HERE / "upstream/manifest.json"
spec = importlib.util.spec_from_file_location("apply_1d_patch", HERE / "apply_1d_patch.py")
patch_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch_module)


class Texture1DPatchTests(unittest.TestCase):
    def test_pristine_pinned_source_and_patch_are_repeatable(self):
        raw = SOURCE.read_bytes()
        manifest = json.loads(MANIFEST.read_text())
        expected = manifest["files"]["xenosrecomp/XenosRecomp/shader_recompiler.cpp"]["sha256"]
        self.assertEqual(hashlib.sha256(raw).hexdigest(), expected)
        once = patch_module.patched_source(raw.decode())
        twice = patch_module.patched_source(raw.decode())
        self.assertEqual(once, twice)
        self.assertEqual(raw, SOURCE.read_bytes())

    def test_descriptor_extension_preserves_existing_offsets(self):
        patched = patch_module.patched_source(SOURCE.read_text())
        self.assertIn("288 + constantInfo->registerIndex * 4", patched)
        self.assertIn("18 + constantInfo->registerIndex / 4", patched)
        self.assertIn("j * 64 + constantInfo->registerIndex * 4", patched)
        self.assertIn("std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4", patched)
        self.assertIn("4 * std::size(TEXTURE_DIMENSIONS) + constantInfo->registerIndex / 4", patched)

    def test_common_helpers_are_real_one_dimensional_samples(self):
        common = COMMON.read_text()
        self.assertIn("Texture1D<float4> g_Texture1DDescriptorHeap[]", common)
        self.assertIn("register(t0, space5)", common)
        self.assertIn("g_Texture1DDescriptorHeap[resourceDescriptorIndex].Sample", common)
        self.assertIn("texture.GetDimensions(width)", common)
        self.assertIn("frac(texCoord * width - 0.5)", common)


if __name__ == "__main__":
    unittest.main(verbosity=2)
