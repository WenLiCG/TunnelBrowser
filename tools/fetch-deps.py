"""Download pinned upstream binaries and their license notices (stdlib only)."""
import hashlib
import pathlib
import tarfile
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[1]
CEF = "150.0.23+g4ccc4ca+chromium-150.0.7871.287"
FILES = [
    ("cef.tar.bz2", f"https://cef-builds.spotifycdn.com/cef_binary_{CEF.replace('+', '%2B')}_windows64_minimal.tar.bz2",
     "sha1", "804ecda5e1adad6ff4b7ffe3fbc45af13d1eeff6"),
    ("libxray.zip", "https://github.com/XTLS/libXray/releases/download/v26.10.31/libxray-windows-x64.zip",
     "sha256", "9a3b1548667836a42b48bb0e37505726d4cd72e8429182e870b92df4cc8172f3"),
]


def fetch(item):
    name, url, algorithm, expected = item
    archive = ROOT / "downloads" / name
    archive.parent.mkdir(exist_ok=True)
    if not archive.exists():
        part = archive.with_suffix(".part")
        with urllib.request.urlopen(url, timeout=120) as response, part.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        part.replace(archive)
    with archive.open("rb") as stream:
        actual = hashlib.file_digest(stream, algorithm).hexdigest()
    if actual != expected:
        raise RuntimeError(f"Checksum mismatch: {archive}; remove this cached file and retry")
    if name.startswith("cef"):
        if not (ROOT / "third_party" / f"cef_binary_{CEF}_windows64_minimal" / "include").exists():
            with tarfile.open(archive) as content:
                content.extractall(ROOT / "third_party", filter="data")
    else:
        with zipfile.ZipFile(archive) as content:
            for member in content.infolist():
                if pathlib.PurePosixPath(member.filename).name in ("libXray.dll", "libXray.h"):
                    content.extract(member, ROOT / "third_party" / "libxray")
    print(f"Verified {name}: {algorithm}:{actual}", flush=True)


def main():
    (ROOT / "third_party").mkdir(exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as executor:
        list(executor.map(fetch, FILES))
    licenses = ROOT / "third_party" / "licenses"
    licenses.mkdir(exist_ok=True)
    for name, url in {
        "libXray-MIT.txt": "https://raw.githubusercontent.com/XTLS/libXray/7fe8528063e54343af8ba9a37431afc56000de21/LICENSE",
        "Xray-core-MPL-2.0.txt": "https://raw.githubusercontent.com/XTLS/Xray-core/a4a3dff5b20c2faae567b742c172c7b95e75c083/LICENSE",
    }.items():
        path = licenses / name
        if not path.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                path.write_bytes(response.read())


if __name__ == "__main__":
    main()
