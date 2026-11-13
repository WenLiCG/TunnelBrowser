import hashlib
import pathlib
import tempfile
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
source = root / "build" / "app"
third_party = root / "third_party"
cef = next(third_party.glob("cef_binary*"))
files = {name: source / name for name in ("TunnelBrowser.exe", "TunnelBrowser.dll", "libXray.dll")}
files.update({name: root / name for name in ("README.md", "THIRD_PARTY.md")})
for directory in (cef / "Release", cef / "Resources"):
    for path in directory.rglob("*"):
        if (path.is_file() and path.suffix.lower() not in (".lib", ".exp", ".pdb", ".log")
                and path.name not in ("bootstrap.exe", "bootstrapc.exe")):
            files[path.relative_to(directory).as_posix()] = path
for name in ("libXray-MIT.txt", "Xray-core-MPL-2.0.txt"):
    files[f"licenses/{name}"] = third_party / "licenses" / name
files["licenses/CEF-BSD.txt"] = cef / "LICENSE.txt"

archive = root / "dist" / "TunnelBrowser-Windows-x64-0.2.0.zip"
archive.parent.mkdir(exist_ok=True)
checksum = archive.with_suffix(".zip.sha256")
with tempfile.TemporaryDirectory(dir=archive.parent) as temporary:
    pending = pathlib.Path(temporary) / archive.name
    with zipfile.ZipFile(pending, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for name, path in sorted(files.items()):
            # ZIP's earliest timestamp is fixed, independent of source file mtimes.
            entry = zipfile.ZipInfo(f"TunnelBrowser/{name}", (1980, 1, 1, 0, 0, 0))
            output.writestr(entry, path.read_bytes(),
                            compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with pending.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    pending_checksum = pending.with_suffix(".zip.sha256")
    pending_checksum.write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    pending.replace(archive)
    pending_checksum.replace(checksum)
print(archive)
print("SHA-256:", digest)
