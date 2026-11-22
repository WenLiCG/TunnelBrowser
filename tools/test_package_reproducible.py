import os
from pathlib import Path
import tempfile
import unittest
import zipfile

from package import package_files, write_archive


class PackageReproducibilityTest(unittest.TestCase):
    def test_zip_bytes_ignore_source_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sources = {name: root / name for name in ("b.txt", "a.exe")}
            for path in sources.values():
                path.write_bytes(b"runtime package fixture\n")
            a, b = root / "a.zip", root / "b.zip"
            write_archive(sources, a)
            for path in sources.values():
                os.utime(path, (1700000000, 1700000000))
                path.chmod(0o777)
            write_archive(sources, b)
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with zipfile.ZipFile(a) as package:
                self.assertEqual(package.namelist(), ["TunnelBrowser/a.exe", "TunnelBrowser/b.txt"])
                for entry, mode in zip(package.infolist(), (0o100755, 0o100644)):
                    self.assertEqual(entry.date_time, (1980, 1, 1, 0, 0, 0))
                    self.assertEqual(entry.create_system, 3)
                    self.assertEqual(entry.external_attr >> 16, mode)
                    self.assertEqual(entry.compress_type, zipfile.ZIP_DEFLATED)
                    self.assertEqual(package.read(entry), b"runtime package fixture\n")

    def test_package_ignores_old_cef_and_checks_pin(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "runtime.json").write_text(
                '{"version":"0.3.0","cef":"pinned","cef_files":["Release/libcef.dll"]}', encoding="utf-8"
            )
            cef = root / "third_party" / "cef_binary_pinned_windows64_minimal" / "Release"
            libxray = root / "third_party" / "libxray" / "libxray-windows-x64"
            app = root / "build" / "app"
            for path in (cef, libxray, app, root / "third_party" / "cef_binary_old"):
                path.mkdir(parents=True)
            for path in (cef / "libcef.dll", cef / "bootstrap.exe", libxray / "libXray.dll",
                         app / "libcef.dll", app / "TunnelBrowser.exe", app / "libXray.dll",
                         app / "bootstrapc.exe", app / "dxcompiler.dll"):
                path.write_bytes(b"pinned fixture")
            version, files = package_files(root)
            self.assertEqual(version, "0.3.0")
            self.assertIn("libcef.dll", files)
            self.assertNotIn("bootstrapc.exe", files)
            self.assertNotIn("dxcompiler.dll", files)
            (app / "libcef.dll").write_bytes(b"old output")
            with self.assertRaisesRegex(RuntimeError, "libcef.dll"):
                package_files(root)


if __name__ == "__main__":
    unittest.main()
