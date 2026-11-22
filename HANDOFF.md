# Tunnel Browser 0.3.0 开发交接

发布包面向 Windows x64。CEF Chrome Runtime 提供原生标签页，官方启动器维持 Chromium 多进程沙箱，libXray 在启用 VLESS 时按需加载。

## 交接状态

2026-11-22 已在 Windows x64 验收 0.3.0。本版关闭内置 Google 服务，拦截已知服务域名，保留官方 CEF 渲染内核。固定依赖为 CEF `150.0.24`（Chromium `150.0.7871.289`）和 libXray `26.10.31`。

- 本地成品是 `dist/TunnelBrowser-Windows-x64-0.3.0.zip`，含 25 个文件，共 167,837,633 字节。包及配套的 `.zip.sha256` 由构建脚本生成。
- 本地成品 SHA-256 为 `e61e43678a719f09a16f88aaadbf6b4f8d8cdba31f32624f7172b07a4d88278d`。
- Git 交接包含源码、测试、依赖清单和构建脚本。ZIP、已下载依赖与本机构建产物通过 `.gitignore` 排除。

## 源码与数据

| 模块 | 入口 |
| --- | --- |
| 浏览器生命周期、设置面板、CEF 网络策略和代理状态 | [src/app.cc](src/app.cc) |
| Google 浏览器服务域名清单 | [src/service_domains.h](src/service_domains.h) |
| 分享链接解析、REALITY 配置 | [src/node.cc](src/node.cc) |
| 浏览资料持久化和清理 | [src/profile.cc](src/profile.cc) |
| 内核版本、下载摘要及运行文件清单 | [runtime.json](runtime.json) |
| 依赖获取、Windows 构建、出包 | [tools/fetch-deps.py](tools/fetch-deps.py)、[tools/build.ps1](tools/build.ps1)、[tools/package.py](tools/package.py) |

用户数据在 `%LOCALAPPDATA%\TunnelBrowser`。继续使用既有 `profile/`，二次启动通过同一 request context 打开新窗口。CEF 还会创建 `Default/`，清理时一并覆盖它和旧版的 `global/`。清理保留由 DPAPI 保护的节点。故障记录只提交错误阶段、版本和复现信息。

## 重新构建

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

## 已验收与待验证

| 验收项 | 结果 |
| --- | --- |
| Windows 构建、原生自检与脚本测试 | 中文路径下的 PowerShell 5.1 构建通过。23 项原生自检、5 项 Python 测试和 3 个工具链场景通过。 |
| 本地 REALITY 回环 | 代理、直连和重启直连均通过。验证了代理故障阻断、主动恢复直连、原生标签、GET 和 POST 弹窗及 Cookie 跨重启保留。回环网络日志未记录发往 Google 服务的请求。 |
| 解压后的发行包 | 包完整性、运行文件清单和正式 DLL 检查通过。使用隔离资料验证了启动、二次打开和跨窗口读取同一站点数据。相同构建输入的重复打包字节一致。 |
| 公网和长期运行 | 真实节点、IPv6、DNS 和 WebRTC、持续连接和下载、长时间运行仍待验收。可复现编译需在同一 Windows 测试机上固定 Python、MSVC、SDK、CMake 和 Ninja，从干净工作区做两次完整构建并比较 EXE、DLL 和 ZIP 的 SHA-256。 |

若通过远程桌面连接 Windows 测试机，关闭自动重连，并在启动日志中标明执行者与任务。更新 CEF 后重做回环、语言资源与发行文件清单检查。

## 必须保持的行为

- 应用打开的窗口先设置时区与屏幕尺寸，再访问启动网址。原生弹窗保留原始导航与请求体。当前 CEF 在原生弹窗里可能仍报告系统屏幕尺寸，回环测试分别验证了弹窗的导航、语言及时区。
- 切换时使用保留的本地拒绝端口和请求门禁。已打开的网页卸载后等连接关闭，最后应用新线路并重开页面。失败进入红色阻断状态。只有用户选择关闭 VLESS 才切回直连。
- 本地 SOCKS 绑定回环，禁用 UDP，无直连回退。每次启用重新选择端口。会话代数用于忽略上次代理的晚到错误。
- 维护 CEF 启动 flags、每个浏览上下文的偏好、CEF 请求过滤和 libXray 出站规则。禁用 Google 浏览器服务的验证范围是当前构建的集成及已知域名，操作系统的 DNS、其他程序或用户访问的网站各自运行。
- CEF 150 的 `CloseAllConnections` 须传有效回调。libXray 是 Go DLL，模块在进程退出时由系统回收。分发时保留 [第三方归属](THIRD_PARTY.md) 和随包通知。
