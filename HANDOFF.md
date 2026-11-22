# Tunnel Browser — handoff

## CLI review 2026-11-22 (branch `tb-cli-review-20261122`)

Full-codebase review, practical fixes, regression tests and handoff. Nothing was merged, pushed to `main`, released or deployed. The 0.3.0 handoff further down is kept verbatim as the record of the last Windows acceptance.

| Item | Value |
| --- | --- |
| Repository | `https://github.com/WenLiCG/TunnelBrowser`, default branch `main` |
| Base SHA | `35ed6cb5a034cf61a8a806630f3f63a951b40c72` (remote `main` at review start, verified with `git ls-remote`) |
| Branch | `tb-cli-review-20261122` |
| Code commit | `5b55d5f5ba0df74780a4cfbe4386d624846be11f` — source, tests, README |
| Handoff commit | The commit that adds this file, on top of `5b55d5f`. It cannot contain its own SHA; read it with `git rev-parse tb-cli-review-20261122`. |
| Source CLI | Claude Code CLI; `claude --version` reported `2.1.326 (Claude Code)`; model Claude Opus 5.5 |
| Push / PR | **Blocked**, see "Publication status" below. The commits are local only. |
| Patch | `/workspace/cli-reviews/tunnelbrowser-20261122/tb-cli-review-20261122.patch` (`git format-patch --stdout 35ed6cb..tb-cli-review-20261122`). It contains both commits, including the new files. |

### Findings and disposition

Severity reflects user impact. "Code reading" means it was found by inspection, not reproduced at runtime. No Windows runtime was available.

| # | Severity | Area | Finding | Disposition |
| --- | --- | --- | --- | --- |
| F1 | Medium | Reliability, switching | `unloading` was cleared only by an `about:blank` load-end or tab close. Suppose a tab's main frame ends a switch somewhere other than `about:blank`, for example when the user opens `chrome://history` or the new-tab page mid-switch. Its id then stayed in the set forever. Every later switch waited on it, hit the 7 s timeout and went red ("关闭旧连接超时"), until that tab was closed. Code reading. | **Fixed.** `PageUnloads` (`src/route.h`) starts each switch with a fresh set and treats any finished non-web main-frame load as unloaded. Regression in `tests/host/route_test.cc`. |
| F2 | Medium | Privacy, UX | With VLESS on, the connect button and link field were disabled. Changing nodes therefore meant "关闭 VLESS · 直连" first, which reloads every open page over the **real connection**, against the "never fall back to direct implicitly" rule. Code reading. | **Fixed.** Connecting is allowed while Proxy is active. The button reads "切换 VLESS 节点", and the switch reuses the existing fail-closed path: blocked route, unload, `stopXray`→`runXray`, reopen. A failed start ends red, never direct. An empty field reconnects the saved node. **Product review requested** (visible UI change). |
| F3 | Low–Med | Compatibility | Share links that omit `type` (the share-link convention defaults it to TCP) were rejected as invalid. | **Fixed** in `src/node.cc`. An empty or omitted `type` is RAW/TCP; `ws`, `grpc`, `xhttp` and the like stay rejected. Regression in host tests and native `--self-test`. |
| F4 | Low–Med | Durability, recovery | `SaveProfile` and `SaveLink` wrote through `ofstream` and renamed without flushing, and left `*.tmp` behind on failure. A truncated `fingerprint.json` after power loss makes startup refuse to run, with a message that named neither the path nor a recovery step. | **Fixed.** `WriteFileAtomically` (`src/profile.cc`) writes, calls `FlushFileBuffers`, renames with `MOVEFILE_WRITE_THROUGH` and deletes the temporary file on failure. The startup error now shows the full path and the recovery (rename the file; browsing data and the saved node are unaffected). The conservative "refuse to start" behavior is kept. |
| F5 | Low | UX | In the red state, web navigations were cancelled silently: Chromium shows no error for `ERR_ABORTED`, so typing a URL seemed to do nothing. | **Fixed.** `OnBeforeBrowse` cancels main-frame web navigations in Blocked mode and shows the settings panel, which already states the reason. It does nothing while closing. `ResourceGate` still enforces the block independently. |
| F6 | Low | Maintainability, testing | Parser, Xray config and switching rules were testable only through the Windows-native self-test with full CEF and libXray. | **Added** `tools/test_host_cpp.py` and `tests/host/` (shims and C++ tests). They check parser rules, fail-closed config invariants (loopback-only noauth SOCKS, UDP off, first outbound VLESS, no `freedom`, `loglevel: none`, service-domain blackhole) and route rules. The suite skips on Windows and when no compiler is found. |
| F7 | Trivial | Lint | Unused `sys` and `time` imports in `tests/local_reality.py`. | **Fixed.** |
| P1 | Medium | Security (local) | The local SOCKS5 inbound is `noauth` on `127.0.0.1`. While VLESS is on, any local process, including other logged-in Windows users (loopback is shared), can use the tunnel. Chromium cannot authenticate to SOCKS. The per-enable random port only obscures it. | **Not changed; product/architecture decision.** One option: an Xray HTTP inbound with random per-session credentials, answered in `GetAuthCredentials(isProxy=true)`. That needs a new loopback test for DNS, WebSocket and fail-closed behavior. |
| P2 | Low–Med | Privacy, behavior | In proxy mode, `bypass_list "<-loopback>"` sends loopback and LAN destinations to the node. This stops pages from probing the user's localhost, but localhost dev servers are unreachable with VLESS on, and the node's own loopback is protected only by its server-side `freedom` rules. | **Kept** (consistent with fail-closed, and the loopback test depends on it). Document for users, or add opt-in client-side private-range blocking. |
| P3 | Low | Compatibility | Other share-link fields are rejected as "unsupported": `pqv` (REALITY `mldsa65Verify`), and empty `host=`/`path=` that some exporters emit. | **Not changed.** Supporting `pqv` means passing it into `realitySettings` plus a loopback test with an ML-DSA server. Product decision. |
| P4 | Low–Med | UX, privacy | All of `chrome://settings` redirects to the panel, so Clear browsing data, site-permission management and password pages are unreachable, and `chrome://password-manager` is blocked. Code reading suggests the Chrome password manager may still offer to save passwords. That is unverified. | **Product input needed.** Either set `credentials_enable_service=false` and `profile.password_manager_enabled=false`, or allow the password and permission pages. Verify on Windows first. |
| P5 | Low | Product | Every launch starts direct (yellow). There is no "always VLESS" or auto-connect option for users who never want direct. | Product decision; not changed. |
| P6 | Low | UX | Green means the core started and the route is applied, not that the node is reachable (already documented). | Suggest an optional reachability probe through the proxy; not changed. |
| P7 | Low | Fingerprint consistency | Timezone and screen overrides use per-tab DevTools Emulation. Service and shared workers, and native popups (already documented), may report system values. | Needs Windows runtime verification; not changed. |
| P8 | Low | Robustness | If `stopXray` or `runXray` never returns, `busy` stays true and the app cannot exit normally (WM_CLOSE keeps reporting "操作进行中"). Code reading. | Not changed. Consider a watchdog that offers a forced exit. |
| P9 | Low | Privacy | Profile reset removes `profile/`, `Default/`, `global/`, `Local State` and `fingerprint.json*`. Chromium root-level caches such as `GrShaderCache` and `ShaderCache` remain. | Not changed. List what remains under `%LOCALAPPDATA%\TunnelBrowser` after a reset on Windows and decide. |
| P10 | Low | UX | Browser windows open at fixed bounds `(80,60) 1180×800`, ignoring DPI and work area. Removed `chrome://` pages are cancelled silently. | Not changed. |
| P11 | Info | Architecture | The global request context starts on the blocked route and then follows the mode after the first switch. No browsing uses it, per code reading. | Not changed. |
| P12 | Info | Process | No CI. The build is Windows-only. | Recommend a Windows runner (`build.ps1 -Test`, unit tests, `local_reality.py`) plus a Linux job for `tools/test_host_cpp.py`. |
| P13 | Info | Product | Component updates are disabled, so component-delivered features such as Widevine DRM are presumably unavailable. Unverified. Service-domain blocking also breaks Google Fonts, reCAPTCHA and similar (already documented). | Kept as a deliberate product choice. |

### Well-designed behavior that was kept

- Fail-closed switching: a reserved, non-listening `SO_EXCLUSIVEADDRUSE` loopback port serves as the "blocked" proxy. `ResourceGate` tags each request with its route generation, so late requests and errors from an old route are ignored. Pages unload before the new route applies, and connections are drained with valid `CloseAllConnections` callbacks.
- Only an explicit "关闭 VLESS · 直连" restores direct. Proxy failure (`ERR_PROXY_CONNECTION_FAILED`) goes red.
- Generated Xray config: SOCKS on 127.0.0.1 with UDP off, the first and default outbound is VLESS, no `freedom`, logs off, a service-domain blackhole, and no `domainStrategy`, so no local DNS. QUIC is disabled, WebRTC uses `disable_non_proxied_udp`, DoH is off and prediction is off. The new host test now guards these invariants.
- Strict share-link parser (single `@`, IPv6 literals, hex `sid`, 43-character base64url `pbk`, in-origin `spx`, fingerprint allowlist). Error messages never echo node secrets, which is now tested.
- DPAPI-protected `node.dat`, a password-style link field, and secrets never logged.
- Environment overrides (timezone, screen) are applied before the first navigation. bfcache is disabled so pages don't survive across routes.
- The Chromium sandbox stays on. `command_line_args_disabled` is set and dangerous switches are refused. Release builds carry no test entry points, which `package_check.py` verifies.
- Pinned SHA-256 downloads, tar extraction with `filter="data"`, reproducible ZIP, `ResetProfileData` refuses junctions that escape the data root, and libXray is never unloaded.

### Changes

| File | Change |
| --- | --- |
| `src/route.h` (new) | `Mode`, `IsWebUrl`, `CanBrowseIn`, `ConnectAllowed`, `DisconnectAllowed`, `PageUnloads`. Header-only, no CEF or Win32. |
| `src/app.cc` | Uses `route.h`. Per-switch unload tracking (F1), node switching while Proxy plus the label and status text (F2), blocked-navigation feedback (F5), `SaveLink` via `WriteFileAtomically` (F4), the startup error with path and recovery (F4), and 4 new native self-test checks. 23 → 27 executed checks. CRLF line endings preserved. |
| `src/node.cc` | Omitted or empty `type` means RAW/TCP (F3). |
| `src/profile.{h,cc}` | `WriteFileAtomically`; `SaveProfile` uses it (F4). |
| `tests/host/*`, `tools/test_host_cpp.py` (new) | Portable host tests (F6). The shims are used on non-Windows hosts only. |
| `tests/local_reality.py` | Removed unused imports (F7). |
| `README.md` | Documents omitted `type`, node switching and red-state feedback. |

No dependency was added to the product or the build. The host tests need only some C++20 compiler.

### Test environment and results

Environment: Linux 6.12 x86_64 container; Python 3.13.5; PowerShell 7.5.13 (parser only). zig 0.17.1 (bundled clang) and pyflakes/ruff were installed with `pip --target` into the session-local `/workspace/cli-reviews/tunnelbrowser-20261122/localdeps`, outside the repo; nothing went system-wide. Pinned CEF `150.0.24` and libXray `26.10.31` were fetched with `python3 tools/fetch-deps.py` (SHA-256 verified, 21 s) into the gitignored `downloads/` and `third_party/`. Times are UTC, 2026-11-22.

| Time | Command | Result |
| --- | --- | --- |
| 15:56:37 | `python3 -m unittest discover -s tools -p "test_*.py" -v` (base) | 5/5 pass |
| ≈15:56 | `pyflakes tools tests` (base) | 2 unused imports (F7) |
| ≈15:59 | Windows x64 compile check of base `src/*.cc`, release and `TB_TESTING` variants (see below) | 6/6 compile, 0 errors |
| 16:00:15 | `TB_HOST_CXX="python3 -m ziglang c++" python3 -m unittest discover -s tools -p "test_node_host.py"` against base `node.cc` | **RED as intended**: `FAIL: omitted type defaults to RAW/TCP`; all other parser checks and the config invariants pass |
| 16:00:41 | Same, after the `node.cc` fix | GREEN, 2/2 |
| 16:02:41 | `… -p "test_host_cpp.py"` with a behavior-preserving `route.h` extraction | **RED as intended**: `switch node while VLESS is active`, `a tab that left web content for an internal page releases the switch`, `a new switch waits only for pages it tracks`; characterization checks pass |
| 16:03:02 | Same, after the `route.h` fix | GREEN, 3/3 |
| 16:06:08–16:06:56 | Fresh final run with the zig cache cleared (`localdeps/evidence-final.txt`) | Compile check 6/6, 0 errors, the only `src/` warning a pre-existing `NONCLIENTMETRICSW` initializer note. `unittest` 8/8 pass with `TB_HOST_CXX`; without a compiler, 7 pass and 1 skipped as designed. pyflakes clean. `git -c core.whitespace=cr-at-eol diff --check` clean. |
| ≈16:07 | PowerShell `Parser::ParseFile` on `tools/build.ps1` and `tools/test_vcvars_import.ps1` | 0 parse errors |
| ≈16:07 | Secret and PII grep over tracked and new files | none |

Compile check command (script kept outside the repo in `localdeps/syntax-check.sh`): `python3 -m ziglang c++ -target x86_64-windows-gnu -std=c++20 -Wall -Wextra -c <src> -o /tmp/...` with CEF's Windows defines (`WIN32 _WIN32 UNICODE WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN CEF_USE_SANDBOX …`) and `-I third_party/cef_binary_…`, once more with `-DTB_TESTING -DXRAY_VERSION="26.10.31"`. **This is a clang + mingw-w64 front-end check against the real CEF headers. It is not an MSVC build and nothing was linked or run.**

**Not run (Windows / GUI only):** `tools/build.ps1 -Test` (MSVC build plus native `--self-test`), `tests/local_reality.py` (needs `build/test/TunnelBrowser.exe` and the libXray DLL), `tools/build.ps1 -Package` with `tests/package_check.py`, `tools/test_vcvars_import.ps1` (needs `cmd.exe`), and every GUI flow (panel, tabs, popups, DPI, switching). I did not watch any GUI behavior.

### Remaining risks and platform gaps

- The F1, F2 and F5 changes in `app.cc` are covered by host tests of the rules and by a Windows-header compile, but have **not** been exercised in CEF on Windows. `OnLoadEnd` now forwards every main-frame load-end to `ResumeTraffic`. That is a no-op unless a switch is pending and all tracked pages are unloaded, but it needs the loopback run.
- MSVC-specific diagnostics and the `/Brepro` / deterministic flags were not exercised.
- P1–P13 above stay open.
- The 0.3.0 package hash below describes the old build. Any new package must be rebuilt and re-verified. `runtime.json` still says `0.3.0`; bump it (e.g. `0.3.1`) only when cutting a release.

### Config migration and rollback

- No data-format change. `fingerprint.json` (`version: 1`), DPAPI `node.dat` and the temporary-file names (`fingerprint.json.tmp`, `node.dat.tmp`) are unchanged, and `ResetProfileData` still covers them. No migration step.
- Rollback is nondestructive: revert the branch commits, or keep running the 0.3.0 ZIP. Both versions read the same `%LOCALAPPDATA%\TunnelBrowser` data.

### Publication status (exact failures)

- `GIT_TERMINAL_PROMPT=0 git push origin tb-cli-review-20261122:tb-cli-review-20261122` → `fatal: could not read Username for 'https://github.com': terminal prompts disabled` (exit 128). No credential helper or token is configured in this environment.
- `gh auth status` → `You are not logged into any GitHub hosts.` (exit 1). No PR was opened. No credentials were created, scraped or requested.
- An earlier invocation in the same command line carried a mistyped, invalid `git push` flag. Git rejects unknown options before doing anything, so it had no effect. No hooks were skipped and nothing was force-pushed.

### Exact continuation steps

Next owner: the repository owner (WenLiCG), or a Windows tester they designate.

1. Publish from a machine with normal GitHub access, using either route:
   - From this checkout: `git push origin tb-cli-review-20261122`
   - Or elsewhere: `git checkout -b tb-cli-review-20261122 35ed6cb5a034cf61a8a806630f3f63a951b40c72 && git am --keep-cr /path/to/tb-cli-review-20261122.patch` (`--keep-cr` is required because `src/app.cc` uses CRLF)
2. Open the PR without merging: `gh pr create --base main --head tb-cli-review-20261122 --title "Fail-closed node switching, sticky switch stall fix, durable saves" --body-file <body>`. Paste this section as the body.
3. On Windows x64, from a clean worktree of the branch:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_vcvars_import.ps1
   python -m unittest discover -s tools -p "test_*.py" -v   # test_host_cpp skips on Windows
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Test   # expect "PASS: 27 checks"
   python -m pip install cryptography; python .\tests\local_reality.py
   ```
4. Manual GUI checks, using an isolated data directory (the loopback fixture's `browser-data`, or a throwaway Windows user), never a real profile:
   - With VLESS green, paste a second node and click "切换 VLESS 节点". Open pages must reopen through the new node, and the fixture or netlog must show no direct request.
   - During a switch, open `chrome://history` in one tab. The switch must finish without the red "关闭旧连接超时".
   - In the red state, type an `http(s)` URL. The settings panel must appear and the fixture must receive no request.
   - Corrupt a test-only `fingerprint.json`. The error must show the full path and the recovery text, then start normally after renaming it.
   - Check the panel at 100%, 150% and 200% DPI; the button label is longer than before.
5. Decide on F2 (UI change) and P1, P3, P4 and P5, which need product input.
6. Only when releasing: bump `runtime.json` `version`, run `build.ps1 -Package` and `tests/package_check.py`, and record the new ZIP SHA-256 here.

---

## 0.3.0 handoff (original text; only heading levels demoted)

### Tunnel Browser 0.3.0 开发交接

发布包面向 Windows x64。CEF Chrome Runtime 提供原生标签页，官方启动器维持 Chromium 多进程沙箱，libXray 在启用 VLESS 时按需加载。

#### 交接状态

2026-11-22 已在 Windows x64 验收 0.3.0。本版关闭内置 Google 服务，拦截已知服务域名，保留官方 CEF 渲染内核。固定依赖为 CEF `150.0.24`（Chromium `150.0.7871.289`）和 libXray `26.10.31`。

- 本地成品是 `dist/TunnelBrowser-Windows-x64-0.3.0.zip`，含 25 个文件，共 167,837,633 字节。包及配套的 `.zip.sha256` 由构建脚本生成。
- 本地成品 SHA-256 为 `e61e43678a719f09a16f88aaadbf6b4f8d8cdba31f32624f7172b07a4d88278d`。
- Git 交接包含源码、测试、依赖清单和构建脚本。ZIP、已下载依赖与本机构建产物通过 `.gitignore` 排除。

#### 源码与数据

| 模块 | 入口 |
| --- | --- |
| 浏览器生命周期、设置面板、CEF 网络策略和代理状态 | [src/app.cc](src/app.cc) |
| Google 浏览器服务域名清单 | [src/service_domains.h](src/service_domains.h) |
| 分享链接解析、REALITY 配置 | [src/node.cc](src/node.cc) |
| 浏览资料持久化和清理 | [src/profile.cc](src/profile.cc) |
| 内核版本、下载摘要及运行文件清单 | [runtime.json](runtime.json) |
| 依赖获取、Windows 构建、出包 | [tools/fetch-deps.py](tools/fetch-deps.py)、[tools/build.ps1](tools/build.ps1)、[tools/package.py](tools/package.py) |

用户数据在 `%LOCALAPPDATA%\TunnelBrowser`。继续使用既有 `profile/`，二次启动通过同一 request context 打开新窗口。CEF 还会创建 `Default/`，清理时一并覆盖它和旧版的 `global/`。清理保留由 DPAPI 保护的节点。故障记录只提交错误阶段、版本和复现信息。

#### 重新构建

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_vcvars_import.ps1
python -m unittest discover -s tools -p "test_*.py" -v
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Test -Package
python .\tests\local_reality.py
python .\tests\package_check.py
```

- 构建要求 Python 3.11.4+，打正式包使用 Python 3.14.3。回环前在同一 Python 环境运行 `python -m pip install cryptography`。PowerShell 5.1 用 ASCII 批处理和进程环境传路径，cmd 以 UTF-16LE 导回环境变量。
- `build/test` 包含原生自检和回环入口，`build/app` 用于发行。出包器按清单收集文件，校验包内 CEF 与编译所用 SDK 一致，并固定 ZIP 条目的顺序、时间和权限。
- 回环使用独立的 `browser-data` 及临时节点。失败时查看 `%TEMP%\tunnel-browser-test-*\result.json`、同目录的 `environments.json` 和服务端日志。测试日志和资料目录留在本机。

#### 已验收与待验证

| 验收项 | 结果 |
| --- | --- |
| Windows 构建、原生自检与脚本测试 | 中文路径下的 PowerShell 5.1 构建通过。23 项原生自检、5 项 Python 测试和 3 个工具链场景通过。 |
| 本地 REALITY 回环 | 代理、直连和重启直连均通过。验证了代理故障阻断、主动恢复直连、原生标签、GET 和 POST 弹窗及 Cookie 跨重启保留。回环网络日志未记录发往 Google 服务的请求。 |
| 解压后的发行包 | 包完整性、运行文件清单和正式 DLL 检查通过。使用隔离资料验证了启动、二次打开和跨窗口读取同一站点数据。相同构建输入的重复打包字节一致。 |
| 公网和长期运行 | 真实节点、IPv6、DNS 和 WebRTC、持续连接和下载、长时间运行仍待验收。可复现编译需在同一 Windows 测试机上固定 Python、MSVC、SDK、CMake 和 Ninja，从干净工作区做两次完整构建并比较 EXE、DLL 和 ZIP 的 SHA-256。 |

若通过远程桌面连接 Windows 测试机，关闭自动重连，并在启动日志中标明执行者与任务。更新 CEF 后重做回环、语言资源与发行文件清单检查。

#### 必须保持的行为

- 应用打开的窗口先设置时区与屏幕尺寸，再访问启动网址。原生弹窗保留原始导航与请求体。当前 CEF 在原生弹窗里可能仍报告系统屏幕尺寸，回环测试分别验证了弹窗的导航、语言及时区。
- 切换时使用保留的本地拒绝端口和请求门禁。已打开的网页卸载后等连接关闭，最后应用新线路并重开页面。失败进入红色阻断状态。只有用户选择关闭 VLESS 才切回直连。
- 本地 SOCKS 绑定回环，禁用 UDP，无直连回退。每次启用重新选择端口。会话代数用于忽略上次代理的晚到错误。
- 维护 CEF 启动 flags、每个浏览上下文的偏好、CEF 请求过滤和 libXray 出站规则。禁用 Google 浏览器服务的验证范围是当前构建的集成及已知域名，操作系统的 DNS、其他程序或用户访问的网站各自运行。
- CEF 150 的 `CloseAllConnections` 须传有效回调。libXray 是 Go DLL，模块在进程退出时由系统回收。分发时保留 [第三方归属](THIRD_PARTY.md) 和随包通知。
