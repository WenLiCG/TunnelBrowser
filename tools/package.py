"""Build the portable ZIP from the same runtime file list used by CMake."""
import hashlib
import json
import pathlib
import shutil
import tempfile
import zipfile


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def package_files(root):
    runtime = json.loads((root / "runtime.json").read_text(encoding="utf-8"))
    third_party = root / "third_party"
    cef = third_party / f"cef_binary_{runtime['cef']}_windows64_minimal"
    app = root / "build" / "app"
    files = {}
    staged = {path.split("/", 1)[1]: cef / path for path in runtime["cef_files"]}
    staged["TunnelBrowser.exe"] = cef / "Release" / "bootstrap.exe"
    staged["libXray.dll"] = third_party / "libxray" / "libxray-windows-x64" / "libXray.dll"
    for name, expected in staged.items():
        source = app / name
        if sha256(source) != sha256(expected):
            raise RuntimeError(f"Staged runtime differs from the pinned input: {name}")
        files[name] = source
    files["TunnelBrowser.dll"] = app / "TunnelBrowser.dll"
    for name in ("README.md", "THIRD_PARTY.md"):
        files[name] = root / name
    for name in ("libXray-MIT.txt", "Xray-core-MPL-2.0.txt"):
        files[f"licenses/{name}"] = third_party / "licenses" / name
    files["licenses/CEF-BSD.txt"] = cef / "LICENSE.txt"
    files["licenses/libXray-dependencies.txt"] = root / "LIBXRAY-NOTICES.txt"
    return runtime["version"], files


def write_archive(files, pending):
    with zipfile.ZipFile(pending, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(f"TunnelBrowser/{name}", (1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (0o100755 if name.lower().endswith(".exe") else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.compress_level = 6
            entry.file_size = path.stat().st_size
            with path.open("rb") as source, output.open(entry, "w") as destination:
                shutil.copyfileobj(source, destination, 1024 * 1024)


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    version, files = package_files(root)
    archive = root / "dist" / f"TunnelBrowser-Windows-x64-{version}.zip"
    archive.parent.mkdir(exist_ok=True)
    checksum = archive.with_suffix(".zip.sha256")
    with tempfile.TemporaryDirectory(dir=archive.parent) as temporary:
        pending = pathlib.Path(temporary) / archive.name
        write_archive(files, pending)
        digest = sha256(pending)
        pending_checksum = pending.with_suffix(".zip.sha256")
        pending_checksum.write_text(f"{digest}  {archive.name}\n", encoding="ascii")
        pending.replace(archive)
        pending_checksum.replace(checksum)
    print(archive)
    print("SHA-256:", digest)


if __name__ == "__main__":
    main()
