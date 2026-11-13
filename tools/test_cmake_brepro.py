import pathlib
import re
import unittest


class CMakeBreproTest(unittest.TestCase):
    def test_tunnelbrowser_release_compile_and_link_brepro(self):
        cmake = (pathlib.Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text()
        for operation in ("compile", "link"):
            with self.subTest(operation=operation):
                blocks = re.findall(
                    rf'target_{operation}_options\(TunnelBrowser\s+PRIVATE\s+(.*?)\)',
                    cmake, re.DOTALL)
                self.assertTrue(any('"$<$<CONFIG:Release>:/Brepro>"' in block
                                    for block in blocks),
                                f"TunnelBrowser Release {operation} options lack /Brepro")
        self.assertIn('"$<$<CONFIG:Release>:/experimental:deterministic>"', cmake)
        self.assertIn('"$<$<CONFIG:Release>:/pathmap:${build_source_path}=.>"', cmake)


if __name__ == "__main__":
    unittest.main()
