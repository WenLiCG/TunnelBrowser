"""Build portable C++ checks from tests/host and run them.

- node_test: src/node.cc against tests/host/shim (share-link parsing and fail-closed Xray config).
- route_test: header-only route-switching rules from src/route.h.

Runs on non-Windows hosts with a C++20 compiler (set TB_HOST_CXX, e.g. "clang++" or "python -m ziglang c++").
Windows builds cover the parser rules in the native --self-test.
"""
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = ROOT / "tests" / "host"
SERVICE_HEADER = (ROOT / "src" / "service_domains.h").read_text(encoding="utf-8")


def host_compiler():
    configured = os.environ.get("TB_HOST_CXX")
    if configured:
        return shlex.split(configured)
    for name in ("clang++", "g++", "c++"):
        found = shutil.which(name)
        if found:
            return [found]
    return None


def build(test, sources, folder):
    compiler = host_compiler()
    if not compiler:
        raise unittest.SkipTest("No C++20 compiler; set TB_HOST_CXX")
    binary = pathlib.Path(folder) / test
    command = compiler + ["-std=c++20", "-Wall", "-Wextra", "-I", str(HOST / "shim"), "-I", str(ROOT / "src"),
                          *map(str, sources), "-o", str(binary)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise AssertionError(f"Host build of {test} failed:\n{result.stderr[-4000:]}")
    return binary


@unittest.skipIf(sys.platform == "win32", "Windows uses the native --self-test")
class HostCppTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        try:
            cls.node = build("node_test", [ROOT / "src" / "node.cc", HOST / "node_test.cc"], cls.temporary.name)
            cls.route = build("route_test", [HOST / "route_test.cc"], cls.temporary.name)
        except BaseException:
            cls.temporary.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_binary(self, *args):
        result = subprocess.run([*args], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_parser_rules(self):
        self.assertIn("PASS", self.run_binary(self.node))

    def test_route_rules(self):
        self.assertIn("PASS", self.run_binary(self.route))

    def test_generated_config_is_fail_closed(self):
        config = json.loads(self.run_binary(self.node, "--config"))
        self.assertEqual(config["log"], {"loglevel": "none"})
        [inbound] = config["inbounds"]
        self.assertEqual((inbound["listen"], inbound["port"], inbound["protocol"]), ("127.0.0.1", 10808, "socks"))
        self.assertEqual(inbound["settings"], {"auth": "noauth", "udp": False})
        self.assertNotIn("sniffing", inbound)
        # The first outbound is the default route: it must be the node, never a direct fallback.
        outbounds = config["outbounds"]
        self.assertEqual(outbounds[0]["protocol"], "vless")
        self.assertEqual({o["protocol"] for o in outbounds}, {"vless", "blackhole"})
        vless = outbounds[0]
        self.assertEqual(vless["settings"]["flow"], "xtls-rprx-vision")
        self.assertEqual(vless["settings"]["encryption"], "none")
        self.assertEqual(vless["streamSettings"]["network"], "raw")
        self.assertEqual(vless["streamSettings"]["security"], "reality")
        self.assertNotIn("domainStrategy", config["routing"])
        [rule] = config["routing"]["rules"]
        self.assertEqual(rule["outboundTag"], "blocked")
        blocked = {d.removeprefix("domain:") for d in rule["domain"]}
        for domain in ("accounts.google.com", "googleapis.com", "gstatic.com", "tunnelbrowser.invalid"):
            self.assertIn(domain, blocked)
            self.assertIn(f'"{domain}"', SERVICE_HEADER)


if __name__ == "__main__":
    unittest.main()
