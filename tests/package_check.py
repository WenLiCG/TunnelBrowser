import hashlib
import pathlib
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
archive = root / "dist" / "TunnelBrowser-Windows-x64-0.2.0.zip"
expected, name = archive.with_suffix(".zip.sha256").read_text(encoding="ascii").split()
with archive.open("rb") as stream:
    assert name == archive.name and hashlib.file_digest(stream, "sha256").hexdigest() == expected

with zipfile.ZipFile(archive) as package:
    assert package.testzip() is None
    entries = {pathlib.PurePosixPath(name).name for name in package.namelist()}
    assert {"TunnelBrowser.exe", "TunnelBrowser.dll", "libcef.dll", "libXray.dll",
            "CEF-BSD.txt", "libXray-MIT.txt", "Xray-core-MPL-2.0.txt"} <= entries
    assert not any(name.lower().endswith(".log") for name in entries)
    dll = package.read("TunnelBrowser/TunnelBrowser.dll").lower()
    for prefix in (str(pathlib.Path.home()), pathlib.Path.home().as_posix()):
        for encoding in ("utf-8", "utf-16-le"):
            assert prefix.lower().encode(encoding) not in dll

print("PASS: portable package integrity, required files, and build data exclusion")
