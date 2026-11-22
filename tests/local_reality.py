"""Loopback-only REALITY/Vision integration; needs the installed cryptography package."""
import base64
import ctypes
import datetime
import http.server
import json
import pathlib
import socket
import ssl
import subprocess
import tempfile
import threading
import urllib.parse
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, x25519
from cryptography.x509.oid import NameOID

ROOT = pathlib.Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def check_browser_network(folder):
    log = json.loads((folder / "netlog.json").read_text(encoding="utf-8"))
    events = log["events"]
    hosts = {urllib.parse.urlsplit(event.get("params", {}).get("url", "")).hostname for event in events}
    assert hosts <= {None, "127.0.0.1", "tunnelbrowser.invalid"}, hosts
    # Chromium checks the IPv6 route with a UDP socket. Verify it sends no data there.
    types = log["constants"]["logEventTypes"]
    probes = {event["source"]["id"] for event in events
              if event.get("params", {}).get("address") == "[2001:4860:4860::8888]:443"}
    assert not any(event["type"] == types["UDP_BYTES_SENT"] and event["source"]["id"] in probes
                   for event in events)


def run():
    app = ROOT / "build" / "test"
    lib = ctypes.CDLL(str(app / "libXray.dll"))
    lib.CGoInvoke.argtypes = [ctypes.c_char_p]
    lib.CGoInvoke.restype = ctypes.c_void_p
    lib.CGoFree.argtypes = [ctypes.c_void_p]

    def invoke(method, config=None):
        request = {"apiVersion": 3, "method": method}
        if config is not None:
            request["payload"] = {"xrayJson": json.dumps(config)}
        pointer = lib.CGoInvoke(json.dumps(request).encode())
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            lib.CGoFree(pointer)

    class Site(http.server.BaseHTTPRequestHandler):
        requests = 0
        reports = []
        accept_languages = []
        paths = []

        def do_GET(self):
            Site.requests += 1
            Site.paths.append(self.path)
            Site.accept_languages.append(self.headers.get("Accept-Language", ""))
            if self.path in ("/favicon.ico", "/?blocked", "/?offline-check"):
                self.send_response(204)
                self.end_headers()
                return
            page = b"<!doctype html><title>Frame</title>" if self.path == "/frame" else b'''<!doctype html><title>Checking environment</title>
<h1>Tunnel Browser integration test</h1><input required><script>
(async () => {
 const existing = localStorage.getItem('identity');
 const previousCookie = document.cookie;
 const identity = existing || crypto.randomUUID();
 if (!existing) {
   localStorage.setItem('identity', identity);
   document.cookie = 'identity=' + identity + '; Max-Age=31536000; SameSite=Lax; Path=/';
 }
 const frame = document.createElement('iframe'); frame.src = '/frame';
 const ready = new Promise(resolve => frame.onload = resolve); document.body.append(frame); await ready;
 const report = {existing, identity, previousCookie, cookie: document.cookie, page: location.search,
   pushMessaging: typeof PushManager, validationMessage: document.querySelector('input').validationMessage,
   language: navigator.language, languages: navigator.languages,
   timezone: Intl.DateTimeFormat().resolvedOptions().timeZone, width: screen.width, height: screen.height,
   frame: {language: frame.contentWindow.navigator.language,
     timezone: frame.contentWindow.Intl.DateTimeFormat().resolvedOptions().timeZone,
     width: frame.contentWindow.screen.width, height: frame.contentWindow.screen.height}};
 const response = await fetch('/report', {method:'POST', body:JSON.stringify(report)});
 if (response.ok) document.title = 'TunnelBrowser Local Test';
})();</script>'''
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(page)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length)
            if self.path == "/?popup-post" and body == b"check=original":
                self.do_GET()
                return
            if self.path != "/report":
                self.send_error(400)
                return
            Site.reports.append(json.loads(body))
            self.send_response(200)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok")

        def log_message(self, *_):
            pass

    # Keep failed test artifacts for diagnosis; no real user profile or node is used.
    folder = pathlib.Path(tempfile.mkdtemp(prefix="tunnel-browser-test-"))
    key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(minutes=1))
            .not_valid_after(now + datetime.timedelta(days=1)).sign(key, hashes.SHA256()))
    (folder / "cert.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    (folder / "key.pem").write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    site = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Site)
    camouflage = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Site)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_3
    tls.load_cert_chain(folder / "cert.pem", folder / "key.pem")
    tls.set_alpn_protocols(["h2", "http/1.1"])
    camouflage.socket = tls.wrap_socket(camouflage.socket, server_side=True)
    for server in (site, camouflage):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    private = x25519.X25519PrivateKey.generate()
    encode = lambda data: base64.urlsafe_b64encode(data).decode().rstrip("=")
    public = encode(private.public_key().public_bytes_raw())
    node_port = free_port()
    uuid = "00112233-4455-6677-8899-aabbccddeeff"
    config = {"log": {"loglevel": "debug", "error": str(folder / "server-debug.log"), "access": str(folder / "server-access.log")}, "inbounds": [{"listen": "127.0.0.1", "port": node_port,
        "protocol": "vless", "settings": {"clients": [{"id": uuid, "flow": "xtls-rprx-vision"}], "decryption": "none"},
        "streamSettings": {"network": "raw", "security": "reality", "realitySettings": {
            "target": f"127.0.0.1:{camouflage.server_port}", "serverNames": ["localhost"],
            "privateKey": encode(private.private_bytes_raw()), "shortIds": ["ab12"]}}}],
        "outbounds": [{"protocol": "blackhole", "tag": "block"}, {"protocol": "freedom", "tag": "fixture", "settings": {
            "finalRules": [{"action": "allow", "network": "tcp", "ip": ["127.0.0.1/32"], "port": str(site.server_port)}]}}],
        "routing": {"rules": [{"type": "field", "ip": ["127.0.0.1/32"], "port": str(site.server_port), "outboundTag": "fixture"}]}}
    result = invoke("runXray", config)
    assert result["success"], result
    fixture = {"link": f"vless://{uuid}@127.0.0.1:{node_port}?type=tcp&security=reality&flow=xtls-rprx-vision&sni=localhost&pbk={public}&sid=ab12&fp=chrome",
               "url": f"http://127.0.0.1:{site.server_port}/"}
    path = folder / "fixture.json"
    profile_dir = folder / "browser-data"
    profile_dir.mkdir()
    expected = {"version": 1, "id": "integration-single-profile", "languages": "en-US,en",
                "timezone": "America/New_York", "screenWidth": 1600, "screenHeight": 900}
    (profile_dir / "fingerprint.json").write_text(json.dumps(expected), encoding="utf-8")
    process = None
    try:
        for scenario in ("proxy", "direct", "direct"):
            fixture["scenario"] = scenario
            path.write_text(json.dumps(fixture), encoding="utf-8")
            report_path = folder / "result.json"
            report_path.unlink(missing_ok=True)
            process = subprocess.Popen([str(app / "TunnelBrowser.exe"), f"--smoke-test={path}"], cwd=app)
            status = process.wait(timeout=60)
            report = json.loads(report_path.read_text()) if report_path.exists() else {}
            assert status == 0 and report.get("success"), (status, report, str(folder))
            assert report["yellow"] and (scenario != "proxy" or report["green"]), report
            check_browser_network(folder)
            if scenario == "direct":
                assert report["tabs"] >= 4, report
            assert json.loads((profile_dir / "fingerprint.json").read_text()) == expected
            print("PASS:", scenario, report, flush=True)
        assert Site.requests > 0, "Fixture never received a web request"
        assert not ({"/?blocked", "/?offline-check"} & set(Site.paths)), Site.paths
        assert {"/?tab2", "/?popup-get", "/?popup-post", "/?direct-after-disable"} <= set(Site.paths), Site.paths
        access = (folder / "server-access.log").read_text(encoding="utf-8")
        assert f"127.0.0.1:{site.server_port}" in access, "VLESS server did not carry the browser request"
        assert "tunnelbrowser.invalid" not in access, "Browser service request was forwarded to the VLESS node"
        assert len(Site.reports) >= 6, Site.reports
        identity = Site.reports[0]["identity"]
        for i, report in enumerate(Site.reports):
            assert report["language"] == "en-US" and report["languages"] == ["en-US", "en"], report
            assert report["timezone"] == "America/New_York", report
            frame = report["frame"]
            assert frame["language"] == "en-US" and frame["timezone"] == "America/New_York", report
            if not report["page"].startswith("?popup-"):
                assert (report["width"], report["height"]) == (1600, 900), report
                assert (frame["width"], frame["height"]) == (1600, 900), report
            assert report["identity"] == identity and (i == 0 or report["existing"] == identity), report
            assert report["previousCookie"] == ("" if i == 0 else f"identity={identity}"), report
            assert identity in report["cookie"], report
            assert report["pushMessaging"] == "undefined", report
            assert report["validationMessage"] == "Please fill out this field.", report
        assert all(value.startswith("en-US") for value in Site.accept_languages), Site.accept_languages
        (folder / "environments.json").write_text(json.dumps(Site.reports, indent=2), encoding="utf-8")
        print("PASS: fail-closed, explicit restore, native tabs, GET/POST popups, locale, cookies and browser-service network checks")
        print("Test artifacts:", folder)
    finally:
        (folder / "environments.json").write_text(json.dumps(Site.reports, indent=2), encoding="utf-8")
        if process is not None and process.poll() is None:
            process.terminate()
        invoke("stopXray")
        site.shutdown()
        camouflage.shutdown()


if __name__ == "__main__":
    run()
