"""Build and run the independent host checks in tests/host/independent_test.cc.

This is separate from the author's tools/test_host_cpp.py. It skips on Windows,
where the native --self-test is the runtime check, and when no C++20 compiler
is available. It does not download CEF, link a browser, or open a network tunnel.
"""
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = ROOT / "tests" / "host"


def host_compiler():
    configured = os.environ.get("TB_HOST_CXX")
    if configured:
        return shlex.split(configured)
    for name in ("clang++", "g++", "c++"):
        found = shutil.which(name)
        if found:
            return [found]
    return None


@unittest.skipIf(sys.platform == "win32", "Windows uses the native --self-test")
class IndependentHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = host_compiler()
        if not compiler:
            raise unittest.SkipTest("No C++20 compiler; set TB_HOST_CXX")
        cls.temporary = tempfile.TemporaryDirectory()
        binary = pathlib.Path(cls.temporary.name) / "independent_test"
        command = compiler + [
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-I",
            str(HOST / "shim"),
            "-I",
            str(ROOT / "src"),
            str(ROOT / "src" / "node.cc"),
            str(HOST / "independent_test.cc"),
            "-o",
            str(binary),
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=600, check=False)
        if result.returncode:
            cls.temporary.cleanup()
            raise AssertionError(f"Independent host build failed:\n{result.stderr[-4000:]}")
        cls.binary = binary
        cls.compiler_stderr = result.stderr

    @classmethod
    def tearDownClass(cls):
        temporary = getattr(cls, "temporary", None)
        if temporary is not None:
            temporary.cleanup()

    def test_parser_route_and_fail_closed_config(self):
        result = subprocess.run([self.binary], capture_output=True, text=True, timeout=60, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS: independent host checks", result.stdout)
        self.assertNotIn("FAIL:", result.stderr)

    def test_host_build_has_no_warnings(self):
        self.assertEqual(self.compiler_stderr.strip(), "")


if __name__ == "__main__":
    unittest.main()
