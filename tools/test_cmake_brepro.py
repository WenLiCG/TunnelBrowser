import pathlib
import re
import unittest


class CMakeBreproTest(unittest.TestCase):
    def test_brepro_is_confined_to_msvc_branch(self):
        cmake = (pathlib.Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text()
        # Keep nested if scopes and exclude else/elseif branches of the MSVC guard.
        scopes = []
        outside = []
        guarded = []
        for line in cmake.splitlines():
            code = line.split("#", 1)[0]
            command = re.match(r"\s*(if|elseif|else|endif)\s*\((.*?)\)", code, re.I)
            if command:
                kind, condition = command.groups()
                kind = kind.lower()
                if kind == "if":
                    scopes.append(bool(re.fullmatch(
                        r'CMAKE_CXX_COMPILER_ID\s+STREQUAL\s+"MSVC"', condition.strip())))
                elif kind in ("else", "elseif"):
                    self.assertTrue(scopes, "Unmatched CMake branch")
                    scopes[-1] = False
                else:
                    self.assertTrue(scopes, "Unmatched CMake endif")
                    scopes.pop()
            (guarded if any(scopes) else outside).append(code)
        self.assertFalse(scopes, "Unclosed CMake if block")
        self.assertNotIn("/Brepro", "\n".join(outside),
                         "/Brepro must only apply inside the MSVC compiler branch")
        self.assertIn("/Brepro", "\n".join(guarded))

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
