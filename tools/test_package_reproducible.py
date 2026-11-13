"""Exercise the actual archive-writing block without CEF or a dist tree."""
import ast
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
import zipfile


class PackageReproducibilityTest(unittest.TestCase):
    def test_archive_ignores_source_mtime(self):
        script = Path(__file__).with_name("package.py")
        tree = ast.parse(script.read_text(encoding="utf-8"))
        # Run the production ZipFile block in isolation from artifact discovery.
        blocks = [node for node in ast.walk(tree) if isinstance(node, ast.With)
                  and any(isinstance(item.context_expr, ast.Call)
                          and isinstance(item.context_expr.func, ast.Attribute)
                          and isinstance(item.context_expr.func.value, ast.Name)
                          and item.context_expr.func.value.id == "zipfile"
                          and item.context_expr.func.attr == "ZipFile"
                          for item in node.items)]
        self.assertEqual(len(blocks), 1)
        writer = compile(ast.Module(body=blocks, type_ignores=[]), str(script), "exec")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archives = []
            for index, mtime in enumerate((946684800, 1735689600)):
                source = root / f"input-{index}"
                source.write_bytes(b"identical DLL contents\n" * 100)
                os.utime(source, (mtime, mtime))
                pending = root / f"output-{index}.zip"
                exec(writer, {"zipfile": zipfile, "pending": pending,
                              "files": {"z.dll": source, "a.dll": source}})
                archives.append(pending.read_bytes())
                with zipfile.ZipFile(pending) as archive:
                    self.assertEqual(archive.namelist(),
                                     ["TunnelBrowser/a.dll", "TunnelBrowser/z.dll"])
                    for entry in archive.infolist():
                        self.assertEqual(entry.compress_type, zipfile.ZIP_DEFLATED)
                        self.assertEqual(archive.read(entry), source.read_bytes())
            self.assertEqual(hashlib.sha256(archives[0]).hexdigest(),
                             hashlib.sha256(archives[1]).hexdigest())
            for index in range(2):
                with zipfile.ZipFile(root / f"output-{index}.zip") as archive:
                    for entry in archive.infolist():
                        self.assertEqual(entry.date_time, (1980, 1, 1, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
