import hashlib
import json
import pathlib
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
runtime = json.loads((root / "runtime.json").read_text(encoding="utf-8"))
archive = root / "dist" / f"TunnelBrowser-Windows-x64-{runtime['version']}.zip"
expected, name = archive.with_suffix(".zip.sha256").read_text(encoding="ascii").split()
with archive.open("rb") as stream:
    assert name == archive.name and hashlib.file_digest(stream, "sha256").hexdigest() == expected

inputs = {"TunnelBrowser/" + name.split("/", 1)[1]: name for name in runtime["cef_files"]}
extra = {"TunnelBrowser.exe", "TunnelBrowser.dll", "libXray.dll", "README.md", "THIRD_PARTY.md",
         "licenses/CEF-BSD.txt", "licenses/libXray-MIT.txt", "licenses/Xray-core-MPL-2.0.txt",
         "licenses/libXray-dependencies.txt"}
with zipfile.ZipFile(archive) as package:
    assert set(package.namelist()) == set(inputs) | {"TunnelBrowser/" + name for name in extra}
    assert len(package.namelist()) == len(inputs) + len(extra)
    assert package.testzip() is None
    with package.open("TunnelBrowser/libcef.dll") as archived:
        cef = root / "third_party" / f"cef_binary_{runtime['cef']}_windows64_minimal"
        with (cef / "Release" / "libcef.dll").open("rb") as source:
            assert hashlib.file_digest(archived, "sha256").digest() == hashlib.file_digest(source, "sha256").digest()
    assert b"github.com/gorilla/websocket v1.5.3" in package.read("TunnelBrowser/licenses/libXray-dependencies.txt")
    dll = package.read("TunnelBrowser/TunnelBrowser.dll").lower()
    for forbidden in ("smoke-test", "self-test", str(pathlib.Path.home()), pathlib.Path.home().as_posix()):
        for encoding in ("utf-8", "utf-16-le"):
            assert forbidden.lower().encode(encoding) not in dll

print("PASS: archive integrity, pinned CEF, runtime allowlist, and release DLL")
