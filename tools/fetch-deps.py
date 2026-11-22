"""Download pinned upstream binaries and their license notices (stdlib only)."""
import hashlib
import json
import pathlib
import shutil
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNTIME = json.loads((ROOT / "runtime.json").read_text(encoding="utf-8"))
CEF = f"cef_binary_{RUNTIME['cef']}_windows64_minimal"
FILES = [
    (f"{CEF}.tar.bz2", f"https://cef-builds.spotifycdn.com/{urllib.parse.quote(CEF)}.tar.bz2", RUNTIME["cef_sha256"]),
    ("libxray.zip", f"https://github.com/XTLS/libXray/releases/download/v{RUNTIME['libxray']}/libxray-windows-x64.zip",
     "9a3b1548667836a42b48bb0e37505726d4cd72e8429182e870b92df4cc8172f3"),
]


def download(path, url, expected):
    def verify(file):
        with file.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
                raise RuntimeError(f"SHA-256 mismatch: {path}; check this cached file before retrying")

    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        verify(path)
        return
    with tempfile.TemporaryDirectory(dir=path.parent) as temp:
        pending = pathlib.Path(temp) / path.name
        with urllib.request.urlopen(url, timeout=120) as response, pending.open("wb") as output:
            shutil.copyfileobj(response, output, 1024 * 1024)
        verify(pending)
        pending.replace(path)


def fetch(item):
    name, url, expected = item
    archive = ROOT / "downloads" / name
    download(archive, url, expected)
    if name.startswith("cef"):
        dest = ROOT / "third_party" / CEF
        if not dest.exists():
            with tempfile.TemporaryDirectory(dir=dest.parent) as temp, tarfile.open(archive) as content:
                content.extractall(temp, filter="data")
                try:
                    (pathlib.Path(temp) / CEF).rename(dest)
                except FileExistsError:
                    if not dest.is_dir():
                        raise
    else:
        with zipfile.ZipFile(archive) as content:
            for member in content.infolist():
                if pathlib.PurePosixPath(member.filename).name in ("libXray.dll", "libXray.h"):
                    content.extract(member, ROOT / "third_party" / "libxray")
    print(f"Verified {name}: sha256:{expected}", flush=True)


def main():
    (ROOT / "third_party").mkdir(exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as executor:
        list(executor.map(fetch, FILES))
    licenses = ROOT / "third_party" / "licenses"
    licenses.mkdir(exist_ok=True)
    download(licenses / "libXray-MIT.txt",
             "https://raw.githubusercontent.com/XTLS/libXray/7fe8528063e54343af8ba9a37431afc56000de21/LICENSE",
             "992cf88a505936a11bd0fff17b7842abd9411e8da41fd69f22f47ae915e9d198")
    download(licenses / "Xray-core-MPL-2.0.txt",
             "https://raw.githubusercontent.com/XTLS/Xray-core/a4a3dff5b20c2faae567b742c172c7b95e75c083/LICENSE",
             "1f256ecad192880510e84ad60474eab7589218784b9a50bc7ceee34c2b91f1d5")


if __name__ == "__main__":
    main()
