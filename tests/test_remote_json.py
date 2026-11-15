"""Compile the actual remote JSON parser without the SDK, sockets or game."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/input/remote_control_server.h"


def parser_source():
    text = HEADER.read_text()
    start = text.index("namespace json_min {")
    marker = "}  // namespace json_min"
    end = text.index(marker, start) + len(marker)
    return """#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
""" + text[start:end]


CHECKS = r'''
#include <cstdio>
#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
  return 1; } } while (false)

int main() {
  using namespace json_min;
  Value root;
  CHECK(Parse(R"({"cmd":"script","steps":[{"input":"A","delay_ms":120},
    {"input":"B","enabled":true}],"number":-3,"flag":false,"truth":"yes",
    "digits":"42","invalid":"not-a-number","nothing":null,
    "nested":{"message":"line\nquote\"","unicode":"\u00e9"}})", root));
  CHECK(root.type == Value::Type::Object);
  CHECK(root.obj.front().first == "cmd");
  CHECK(root.FindStr("cmd") && *root.FindStr("cmd") == "script");
  CHECK(root.Find("missing") == nullptr);
  CHECK(root.FindStr("number") == nullptr);
  CHECK(root.FindStr("missing") == nullptr);
  CHECK(root.GetInt("number", 99) == -3);
  CHECK(root.GetInt("digits", 99) == 42);
  CHECK(root.GetInt("invalid", 99) == 99);
  CHECK(root.GetInt("missing", 99) == 99);
  CHECK(!root.GetBool("flag", true));
  CHECK(root.GetBool("number", false));
  CHECK(root.GetBool("truth", false));
  CHECK(root.GetBool("missing", true));
  CHECK(root.Find("nothing") && root.Find("nothing")->type == Value::Type::Null);
  const Value* steps = root.Find("steps");
  CHECK(steps && steps->type == Value::Type::Array && steps->arr.size() == 2);
  CHECK(steps->arr[0].GetInt("delay_ms", -1) == 120);
  CHECK(steps->arr[1].GetBool("enabled", false));
  CHECK(steps->arr[1].Find("enabled")->AsInt(-1) == 1);
  const Value* nested = root.Find("nested");
  CHECK(nested && nested->FindStr("message"));
  CHECK(*nested->FindStr("message") == "line\nquote\"");
  CHECK(nested->FindStr("unicode") && *nested->FindStr("unicode") == "\xc3\xa9");

  // Recursive pair/vector copy, assignment, move and destruction must compile.
  Value copy = root;
  copy.obj.front().second.str = "changed";
  CHECK(*root.FindStr("cmd") == "script");
  Value assigned;
  assigned = copy;
  Value moved = std::move(assigned);
  Value move_assigned;
  move_assigned = std::move(moved);
  CHECK(*move_assigned.FindStr("cmd") == "changed");
  CHECK(move_assigned.Find("steps")->arr.size() == 2);
  CHECK(root.GetInt("number", 0) == -3);

  Value duplicates;
  CHECK(Parse(R"({"x":1,"x":2})", duplicates));
  CHECK(duplicates.obj.size() == 2 && duplicates.GetInt("x", 0) == 1);
  Value empty;
  CHECK(Parse(R"({"array":[],"object":{}})", empty));
  CHECK(empty.Find("array")->arr.empty());
  CHECK(empty.Find("object")->obj.empty());
  CHECK(empty.Find("object")->Find("absent") == nullptr);
  CHECK(JEsc("line\nquote\"slash\\") == "line\\nquote\\\"slash\\\\");
  CHECK(JEsc(std::string(1, '\x01')) == "\\u0001");
  for (std::string_view input : {"", "{", "[", "{\"a\":}", "[] trailing"}) {
    Value invalid;
    CHECK(!Parse(input, invalid));
  }
  std::puts("remote JSON checks passed");
  return 0;
}
'''


class RemoteJsonTests(unittest.TestCase):
    def compile_and_run(self, compiler):
        executable = shutil.which(compiler)
        if executable is None:
            self.skipTest(f"{compiler} is not installed")
        preferred_temp = Path(tempfile.gettempdir()) / "opencode"
        with tempfile.TemporaryDirectory(
                prefix="fable2-json-",
                dir=preferred_temp if preferred_temp.is_dir() else None) as directory:
            source = Path(directory) / "remote_json.cpp"
            binary = Path(directory) / "remote_json"
            source.write_text(parser_source() + CHECKS)
            compiled = subprocess.run(
                [executable, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                 str(source), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("remote JSON checks passed", result.stdout)

    def test_clang_recursive_json(self):
        self.compile_and_run("clang++")

    def test_gcc_recursive_json(self):
        self.compile_and_run("g++")


if __name__ == "__main__":
    unittest.main()
