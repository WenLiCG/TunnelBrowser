"""Source and git invariants for the imported candidate.

These checks read the tree. They do not compile the Windows app, run CEF, or
decide the open product questions.
"""
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASE = "35ed6cb5a034cf61a8a806630f3f63a951b40c72"
IMPORTED_TREE = "a4d8a13c4f1826dbb7c3b464c80c9dbd82ba846c"
CODE_SUBJECT = "fix: fail-closed node switching, sticky switch stall, durable saves"
HANDOFF_SUBJECT = "docs: complete HANDOFF for 2026-11-22 CLI review"


def git_log():
    result = subprocess.run(
        ["git", "log", "--format=%H%x09%T%x09%P%x09%an%x09%ae%x09%s", f"{BASE}..HEAD"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    commits = []
    for line in result.stdout.splitlines():
        sha, tree, parent, author, email, subject = line.split("\t", 5)
        commits.append(
            {
                "sha": sha,
                "tree": tree,
                "parent": parent,
                "author": author,
                "email": email,
                "subject": subject,
            }
        )
    return commits


def find_commit(subject):
    matches = [commit for commit in git_log() if commit["subject"] == subject]
    if len(matches) != 1:
        raise AssertionError(f"expected one commit {subject!r}, found {len(matches)}")
    return matches[0]


def _top_level_items(text):
    depth = 0
    commas = 0
    for char in text:
        if char in "([{":
            depth += 1
        elif char in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif char == "," and depth == 0:
            commas += 1
    return commas + 1


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


class ImportedTreeTest(unittest.TestCase):
    def test_imported_commits_preserve_author_and_tree(self):
        code = find_commit(CODE_SUBJECT)
        handoff = find_commit(HANDOFF_SUBJECT)
        self.assertEqual(code["parent"], BASE)
        self.assertEqual(handoff["parent"], code["sha"])
        self.assertEqual(handoff["tree"], IMPORTED_TREE)
        self.assertEqual(code["author"], "Claude Code review")
        self.assertEqual(handoff["author"], "Claude Code review")
        self.assertEqual(code["email"], "claude-code-review@local")
        self.assertEqual(handoff["email"], "claude-code-review@local")

    def test_app_cc_keeps_crlf(self):
        data = (ROOT / "src" / "app.cc").read_bytes()
        self.assertIn(b"\r\n", data)
        self.assertFalse(data.replace(b"\r\n", b"").count(b"\n"), "src/app.cc has a bare LF line")


class FailClosedSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = (ROOT / "src" / "app.cc").read_text(encoding="utf-8")
        cls.profile = (ROOT / "src" / "profile.cc").read_text(encoding="utf-8")
        cls.node = (ROOT / "src" / "node.cc").read_text(encoding="utf-8")
        cls.runtime = (ROOT / "runtime.json").read_text(encoding="utf-8")

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

    def test_native_self_test_counts_27_with_the_new_names(self):
        start = self.app.index("int SelfTest()")
        end = self.app.index('std::cout << "PASS: "', start)
        body = self.app[start:end]
        for name in (
            "omitted type defaults to RAW/TCP",
            "switch node without a direct hop",
            "atomic save leaves no temporary file",
            "failed save removes its temporary file",
        ):
            self.assertIn(name, body)
        initializer = body[body.index("for (const auto& bad :"):body.index(") {", body.index("for (const auto& bad :"))]
        cases = _top_level_items(initializer[initializer.index("{") + 1:])
        self.assertEqual(cases, 6)
        runtime_checks = body.count("check(") + (cases - 1)
        self.assertEqual(runtime_checks, 27)

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

    def test_omitted_type_is_narrow_and_release_metadata_is_unchanged(self):
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
        self.assertIn('"version": "0.3.0"', self.runtime)

    def test_local_reality_fixture_stays_on_loopback(self):
        text = (ROOT / "tests" / "local_reality.py").read_text(encoding="utf-8")
        self.assertEqual(text.count("vless://"), 1)
        self.assertIn("vless://{uuid}@127.0.0.1:", text)
        self.assertNotIn("requests.", text)
        self.assertNotIn("urllib.request", text)


if __name__ == "__main__":
    unittest.main()
