"""Check source-level routing and persistence invariants."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def extract_function(text, signature):
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
    brace = text.find("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unterminated {signature}")


class FailClosedSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = (ROOT / "src" / "app.cc").read_text(encoding="utf-8")
        cls.profile = (ROOT / "src" / "profile.cc").read_text(encoding="utf-8")
        cls.node = (ROOT / "src" / "node.cc").read_text(encoding="utf-8")

    def test_switch_enters_blocked_route_before_restarting_the_core(self):
        pause = extract_function(self.app, "bool PauseTraffic")
        connect = extract_function(self.app, "void BeginConnect()")
        self.assertLess(pause.index("busy = true"), pause.index("ChangeMode(Mode::Switching)"))
        self.assertLess(pause.index("ChangeMode(Mode::Switching)"), pause.index("ApplyRoute(Mode::Blocked)"))
        self.assertLess(pause.index("ApplyRoute(Mode::Blocked)"), pause.index("DrainConnections()"))
        self.assertLess(pause.index("DrainConnections()"), pause.index("UnloadPages()"))
        self.assertNotIn("Mode::Direct", pause)
        self.assertLess(connect.index("ConnectAllowed"), connect.index("PauseTraffic"))
        self.assertLess(connect.index("PauseTraffic"), connect.index("std::thread"))
        self.assertNotIn("ChangeMode(Mode::Direct)", connect)
        self.assertNotIn("FinishSwitch(Mode::Direct", connect)
        self.assertIn("ProxyEnabled()", connect)

    def test_only_explicit_disconnect_restores_direct(self):
        self.assertEqual(self.app.count("FinishSwitch(Mode::Direct"), 1)
        handler = self.app[self.app.index("if (message == kComplete)"):self.app.index("if (message == WM_CLOSE)")]
        self.assertIn("FinishSwitch(Mode::Direct, L\"VLESS 已关闭，当前为直连模式。\")", handler)
        self.assertIn("w == 3", handler)
        self.assertIn("BlockTraffic(", handler)
        self.assertIn("VLESS 启动失败，联网已阻断", handler)
        self.assertIn("std::atomic<Mode> mode{Mode::Direct}", self.app)

    def test_blocked_navigation_opens_settings_and_gate_remains(self):
        browse = extract_function(self.app, "bool OnBeforeBrowse")
        self.assertIn("mode == Mode::Blocked && frame->IsMain() && IsWebUrl(url)", browse)
        self.assertIn("ShowSettings()", browse)
        self.assertIn("return true", browse)
        gate = extract_function(self.app, "ReturnValue OnBeforeResourceLoad")
        self.assertIn("RV_CANCEL", gate)
        self.assertIn("CanBrowse()", gate)

    def test_visible_switch_label_is_present(self):
        buttons = extract_function(self.app, "void Buttons()")
        self.assertIn('L"切换 VLESS 节点"', buttons)
        self.assertIn('L"启用 VLESS"', buttons)
        panel = extract_function(self.app, "void CreatePanel() {")
        self.assertIn('L"启用 VLESS"', panel)
        self.assertIn('L"关闭 VLESS · 直连"', panel)

    def test_atomic_save_flushes_before_rename_and_cleans_up(self):
        writer = extract_function(self.profile, "bool WriteFileAtomically")
        save = extract_function(self.profile, "bool SaveProfile")
        link = extract_function(self.app, "bool SaveLink")
        self.assertLess(writer.index("FlushFileBuffers"), writer.index("MoveFileExW"))
        self.assertLess(writer.index("MoveFileExW"), writer.index("DeleteFileW"))
        self.assertIn("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH", writer)
        self.assertLess(save.index("!ValidProfile"), save.index("WriteFileAtomically"))
        self.assertNotIn("ofstream", save)
        self.assertLess(link.index("Protect("), link.index("WriteFileAtomically"))
        self.assertIn('L"node.dat"', link)
        self.assertNotIn("ofstream", link)
        reset = extract_function(self.profile, "bool ResetProfileData")
        self.assertIn('L"fingerprint.json.tmp"', reset)
        self.assertNotIn("node.dat", reset)

    def test_omitted_type_is_narrow(self):
        parser = extract_function(self.node, "std::optional<Node> ParseNode")
        self.assertIn('query["type"] != "tcp"', parser)
        self.assertIn('query["type"] != "raw"', parser)
        self.assertIn('query["security"] != "reality"', parser)
        self.assertIn('query["flow"] != "xtls-rprx-vision"', parser)
        config = extract_function(self.node, "std::string MakeConfig")
        self.assertIn('\\"network\\":\\"raw\\"', config)
        self.assertNotIn("freedom", config)
        self.assertIn('\\"listen\\":\\"127.0.0.1\\"', config)
        self.assertIn('\\"udp\\":false', config)

    def test_local_reality_fixture_stays_on_loopback(self):
        text = (ROOT / "tests" / "local_reality.py").read_text(encoding="utf-8")
        self.assertEqual(text.count("vless://"), 1)
        self.assertIn("vless://{uuid}@127.0.0.1:", text)
        self.assertNotIn("requests.", text)
        self.assertNotIn("urllib.request", text)


if __name__ == "__main__":
    unittest.main()
