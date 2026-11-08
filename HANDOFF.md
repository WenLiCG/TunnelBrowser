# Tunnel Browser 0.2.0 开发交接

交接日期 2026-11-08。项目是一个 Windows x64 专用浏览器，优先保证普通网站和原生多标签稳定运行。使用 CEF 的 Chrome 风格界面与 Chromium 多进程沙箱，按需在浏览器主进程内运行 libXray。用户操作见 [README](README.md)，依赖版本、来源和许可证见 [THIRD_PARTY](THIRD_PARTY.md)。

## 接手需要哪些文件

- 本次核对时，Git 仓库尚无提交或远程地址。交接源码时，以当前工作区为准，带上根目录的 `.gitignore`、`CMakeLists.txt`、Markdown 文档以及 `src/`、`tools/`、`tests/`、`research/`。接手人应先将这份源码建立为版本基线，再开始改动。
- `build/`、`downloads/`、`third_party/`、`dist/` 都是忽略目录。前三者分别是构建输出、下载和本机工具缓存、已解包依赖，换机器可以重新生成。
- 现有运行包是 `dist/TunnelBrowser-Windows-x64-0.2.0.zip`，SHA-256 为 `3dbaaa2a4ffc63624989dfac1ce7e787f810b1c22e1ebf0b06532203c836ace3`。解压完整目录后运行 `TunnelBrowser.exe`。包尚未签名。
- 用户数据在 `%LOCALAPPDATA%\TunnelBrowser`，与源码、二进制交接分开。普通故障报告提供脱敏的错误分类、发生阶段、版本和复现步骤即可。完整节点链接、生成的 Xray JSON 和用户 profile 都可能含敏感信息。

## 本地构建和验证

在 Windows x64 上准备 VS 2022 C++ x64 Build Tools、Windows SDK、CMake、Ninja 和 Python。开发机使用 Python 3.14.3，可直接用此版本复现。脚本可在 x64 Native Tools 环境运行，也可用 `vswhere` 寻找已安装的工具链。原机 `downloads/` 下的便携工具链是本地缓存。

在项目根目录依次执行下列命令。

```powershell
# 下载或核对缓存，构建 Release 并运行自检
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1

# 单独重跑 DLL、解析和 profile 自检
.\build\app\bootstrapc.exe --module=TunnelBrowser --self-test

# 在同一个 Python 环境准备 cryptography，再运行回环集成
python -m pip install cryptography
python .\tests\local_reality.py

# 验收通过后，生成 ZIP 和 SHA-256 文件
python .\tools\package.py
```

- `tools/fetch-deps.py` 固定并校验 CEF `150.0.23+g4ccc4ca+chromium-150.0.7871.287` 和 libXray `v26.10.31` 的 Windows x64 归档。首次构建需访问 CEF 下载站、GitHub Release 和许可证文件地址。
- `CMakeLists.txt` 生成 `build/app/TunnelBrowser.dll`，复制 CEF 运行库和资源，并把官方沙箱启动器复制为 `TunnelBrowser.exe`。自检通过原名 `bootstrapc.exe` 加 `--module=TunnelBrowser` 调用客户端 DLL。
- `tests/local_reality.py` 启动回环 HTTP fixture 和 REALITY/Vision 服务端，用临时节点和独立数据目录运行三次浏览器。预期输出 `PASS: proxy`、两次 `PASS: direct` 和总结。失败时优先看 `%TEMP%\tunnel-browser-test-*` 中本次运行目录的 `result.json`（若已生成）、`environments.json` 和两侧日志。测试生成的 fixture 也包含临时节点信息。
- `tools/package.py` 从 `build/app` 组装目录并压缩，包含运行资源和第三方许可证。它会复用现有 `dist/TunnelBrowser/`。升级依赖或删除运行文件后，先用干净的输出目录出包，避免把上版遗留文件带进去。更改发布版本时同步更新脚本内的 ZIP 名和 README。

## 源码入口和必须保持的行为

| 改动范围 | 从哪里读 |
| --- | --- |
| 进程生命周期、原生多标签、设置入口 | `src/app.cc` 的 `Run`、`App`、`BrowserClient`、`PanelProc` |
| 节点导入、代理启停、切换时的网络控制 | `src/node.cc` 的 `ParseNode`、`MakeConfig`，以及 `app.cc` 的 `Core`、`BeginConnect`、`Disconnect`、`FinishSwitch`、`ProxyPolicy`、`Traffic` |
| 持久化、清空 profile、环境参数 | `src/profile.cc` 的 `LoadProfile`、`SaveProfile`、`ResetProfileData`，以及 `BrowserClient::OnAfterCreated`、`OnDevToolsMethodResult` |

### 网络和代理

- 每次启动是黄色 `Direct`。保存节点只记住配置。设置面板的“启用 VLESS”把状态切到 `Switching`，暂停请求、停止当前加载并关闭旧连接。worker 完成核心启停后，通过 Win32 消息让 UI 线程应用 CEF 代理配置。等待各 request context 的连接关闭回调后才恢复请求。
- 代理路径是 `Chromium → 127.0.0.1 的 SOCKS5 → 进程内 libXray → VLESS / RAW / REALITY / Vision`。SOCKS 只绑定回环且禁用 UDP。应用从严格校验过的分享链接生成配置，只有 VLESS 出口。CEF 代理规则使用固定 SOCKS、移除隐式回环旁路且无直连回退。普通直连需要正常 DNS，fixture 的回环解析限制只在测试模式使用。
- 绿色 `Proxy` 表示核心已启动且规则已应用，实际节点可达性还需要发请求验证。核心启动、停止或策略应用失败，以及 CEF 报本地代理连接失败，会进入红色 `Blocked`。用户主动关闭 VLESS 才恢复黄色直连。单个远端站点超时或握手失败可能仍显示绿色。
- libXray 使用 `CGoInvoke` / `CGoFree` 和 API v3。由 DLL 释放返回缓冲，复用同一个已加载模块执行 `runXray` / `stopXray`。替换 DLL 前要完全退出浏览器，进程内的核心故障可能影响浏览器主进程。运行包内只有库，没有独立 `xray.exe`。
- **CEF 150 已踩坑**。调用 `CloseAllConnections` 必须传有效的引用计数回调。内部会调用它，传空曾触发 `libcef.dll` 访问冲突 `0xc0000005`。涉及多标签时，用 `IsSharingWith` 判断 request context 是否共享存储。

### 单一 profile

| 路径相对于 `%LOCALAPPDATA%\TunnelBrowser` | 内容 |
| --- | --- |
| `profile/`、`global/`、`Local State` | CEF 浏览数据和全局状态 |
| `fingerprint.json` | schema v1，保存内部 ID、语言列表、IANA 时区和报告屏幕宽高 |
| `node.dat` | Windows 当前用户 DPAPI 保护的节点链接，换 Windows 用户时应重新导入 |

- 所有标签页和窗口共享一个 profile。语言同时应用到请求和页面环境。locale、时区和报告屏幕尺寸在每个标签页通过 Chromium DevTools Emulation 设置，首次主框架 HTTP(S) 导航等待三项调用确认。设置保存后，完全退出再启动生效。
- “清空当前 profile”需用户确认。先关闭标签页并完成 `CefShutdown`，再清理固定的 profile 文件和目录。保留 `node.dat`，重启时生成新内部 ID。清理前会验证目标路径归属。损坏的环境参数文件会保留并提示启动失败，排障时先备份并检查 `fingerprint.json`。

## 验收基线和后续范围

- 0.2 发布时通过 21 项原生自检。回环集成通过完整 REALITY/Vision 请求链路，并从服务端访问日志确认流量实际经过代理。覆盖了核心故障时请求失败、主动关闭后恢复直连、原生新标签、黄绿状态、页面和同源 iframe 环境参数，以及重启后 Cookie 和 localStorage 持久化。出包时检查了 ZIP 完整性、运行库哈希和 `xray.exe` 缺席。
- 2026-11-08 交接时，用现有构建重跑了 21 项自检及三个集成场景，均通过。回环 HTTP fixture 记录了一次 `WinError 10053`（连接被中止），测试最终退出码为 0。重新计算的 ZIP 哈希与发布记录一致。
- 首次接手后建议先建立源码基线并跑上述自检和集成，再用获授权的真实节点验收。优先检查断网与恢复、节点切换和退出，同时分别验证 DNS、IPv6、WebRTC、WebSocket 和下载行为。回环结果只覆盖 fixture；真实节点、完整旁路审计、长期稳定性和完整视觉验收尚未完成。
- 用户本阶段选择先做浏览体验和环境参数。Canvas、WebGL、音频和字体属于后续工作，Worker 和跨进程 iframe 的环境覆盖仍需测试。内部 profile ID 的轮换与网页可见的硬件指纹无关。
- 升级时核对 [THIRD_PARTY.md](THIRD_PARTY.md) 的上游版本和许可。CEF 版本同时在 CMake 和下载脚本固定。libXray 升级要重新验证 C ABI、版本自检、配置 schema 与核心启停，并重跑回环集成。早期 [Xray 调研](research/xray-integration-evidence.md) 可用于追溯设计和上游实现，其中的“未构建”描述的是调研阶段。
