"""Exercise the actual archive-writing block without CEF or a dist tree."""
import ast
import hashlib
import os
import stat
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile


class PackageReproducibilityTest(unittest.TestCase):
    def archive_writer(self):
        script = Path(__file__).with_name("package.py")
        tree = ast.parse(script.read_text(encoding="utf-8"))
        blocks = [node for node in ast.walk(tree) if isinstance(node, ast.With)
                  and any(isinstance(item.context_expr, ast.Call)
                          and isinstance(item.context_expr.func, ast.Attribute)
                          and isinstance(item.context_expr.func.value, ast.Name)
                          and item.context_expr.func.value.id == "zipfile"
                          and item.context_expr.func.attr == "ZipFile"
                          for item in node.items)]
        self.assertEqual(len(blocks), 1)
        module = ast.Module(body=blocks, type_ignores=[])
        return module, compile(module, str(script), "exec")

    def test_archive_has_fixed_file_permissions(self):
        _, writer = self.archive_writer()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {}
            # Deliberately disagree with both PM-defined destination modes.
            for name, mode in (("z.exe", 0o600), ("a.EXE", 0o777),
                               ("m.dll", 0o777), ("README", 0o600)):
                source = root / name
                source.write_bytes(b"reproducible archive contents\n" * 100)
                source.chmod(mode)
                files[name] = source
            pending = root / "output.zip"
            original_writestr = zipfile.ZipFile.writestr
            writes = []

            def record_write(archive, entry, data, **kwargs):
                writes.append(kwargs)
                return original_writestr(archive, entry, data, **kwargs)

            with mock.patch.object(zipfile.ZipFile, "writestr", record_write):
                exec(writer, {"zipfile": zipfile, "pending": pending, "files": files})
            self.assertEqual(len(writes), len(files))
            for options in writes:
                self.assertEqual(options.get("compress_type"), zipfile.ZIP_DEFLATED)
                self.assertEqual(options.get("compresslevel"), 6)
            with zipfile.ZipFile(pending) as archive:
                self.assertEqual(archive.namelist(),
                                 [f"TunnelBrowser/{name}" for name in sorted(files)])
                for entry in archive.infolist():
                    name = entry.filename.removeprefix("TunnelBrowser/")
                    expected = 0o100755 if name.lower().endswith(".exe") else 0o100644
                    with self.subTest(name=name):
                        self.assertEqual(entry.create_system, 3)
                        self.assertEqual(entry.date_time, (1980, 1, 1, 0, 0, 0))
                        self.assertEqual(entry.compress_type, zipfile.ZIP_DEFLATED)
                        self.assertEqual(archive.read(entry), files[name].read_bytes())
                        self.assertNotEqual(expected, stat.S_IMODE(files[name].stat().st_mode)
                                            | stat.S_IFREG)
                        self.assertNotEqual(entry.external_attr >> 16,
                                            files[name].stat().st_mode)
                        self.assertEqual(entry.external_attr >> 16, expected)

    def test_directory_zipinfo_has_fixed_permissions(self):
        _, writer = self.archive_writer()
        entries = []
        original_init = zipfile.ZipInfo.__init__

        def record_init(entry, *args, **kwargs):
            original_init(entry, *args, **kwargs)
            entries.append(entry)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "licenses"
            directory.mkdir()
            directory.chmod(0o700)
            # Exercise the same production constructor. The current writer reads
            # directories as files; inspect its metadata even when that read fails.
            with mock.patch.object(zipfile.ZipInfo, "__init__", record_init):
                try:
                    exec(writer, {"zipfile": zipfile, "pending": root / "output.zip",
                                  "files": {"licenses/": directory}})
                except IsADirectoryError:
                    pass
            self.assertEqual(len(entries), 1)
            entry = entries[0]
            self.assertEqual(entry.filename, "TunnelBrowser/licenses/")
            self.assertEqual(entry.create_system, 3)
            self.assertEqual(entry.date_time, (1980, 1, 1, 0, 0, 0))
            self.assertEqual(entry.external_attr >> 16, 0o040755)
            self.assertTrue(entry.external_attr & 0x10)

    def test_zipinfo_create_system_is_explicitly_unix(self):
        module, _ = self.archive_writer()
        instances = {target.id for node in ast.walk(module)
                     if isinstance(node, ast.Assign)
                     and isinstance(node.value, ast.Call)
                     and isinstance(node.value.func, ast.Attribute)
                     and isinstance(node.value.func.value, ast.Name)
                     and node.value.func.value.id == "zipfile"
                     and node.value.func.attr == "ZipInfo"
                     for target in node.targets if isinstance(target, ast.Name)}
        self.assertTrue(instances, "Production writer must construct ZipInfo entries")
        assigned = {target.value.id for node in ast.walk(module)
                    if isinstance(node, ast.Assign)
                    and isinstance(node.value, ast.Constant) and node.value.value == 3
                    for target in node.targets
                    if isinstance(target, ast.Attribute)
                    and target.attr == "create_system"
                    and isinstance(target.value, ast.Name)}
        self.assertTrue(instances <= assigned,
                        "Every production ZipInfo instance needs explicit create_system = 3")

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
