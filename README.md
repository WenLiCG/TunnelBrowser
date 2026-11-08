# Tunnel Browser 0.2

最新交接见 [Tunnel Browser 0.2.0 开发交接](HANDOFF.md)。文中的构建与验证命令在完整项目源码目录运行。

Windows x64 浏览器，采用 CEF 150 的原生 Chromium 多标签界面和进程内 libXray 26.10.31。正常使用不需要管理员、系统代理或虚拟网卡。

## 日常浏览

解压完整目录，运行 `TunnelBrowser.exe`，会直接打开普通浏览器窗口。点击标签栏的 `+` 或按 `Ctrl+T` 新建标签页；所有标签页和新窗口共享同一个 profile。

- **黄色：直连，VLESS 未启用。** 可以正常浏览，使用当前网络出口。
- **绿色：VLESS 已启用。** 浏览器使用内置 SOCKS5 → VLESS + RAW/TCP + REALITY + Vision。绿色表示核心运行且代理设置已应用，节点是否可达仍取决于配置和网络。
- **红色：代理异常，联网已阻断。** 不会自动退回直连。可以重新启用 VLESS，或主动关闭 VLESS 恢复直连。

状态同时显示在浏览器标题中，不只靠颜色区分。每次启动默认黄色直连；保存节点不代表自动启用代理。

## 代理设置

在浏览器右上角菜单选择“设置”，或在网页右键菜单选择“代理与环境设置”。粘贴 `vless://` 节点链接，点击“启用 VLESS”。点击“关闭 VLESS · 直连”恢复普通浏览。切换会关闭旧连接，下载与 WebSocket 可能中断；刷新原标签页即可重新连接。

设置窗口可以关闭或按 Escape 隐藏，不会退出浏览器。关闭最后一个浏览器标签页/窗口后程序退出。

节点需要标准 UUID，以及 `type=tcp` 或 `type=raw`、`security=reality`、`flow=xtls-rprx-vision`、`sni`、`pbk`。可选 `fp`、`sid`、`spx`、`encryption=none`、`headerType=none`。不支持的参数会报错。节点用 Windows DPAPI 加密保存于 `%LOCALAPPDATA%\TunnelBrowser\node.dat`；普通运行不写核心原始日志。

## 单一 profile 与环境参数

只有一个持久 profile，不提供多配置列表。设置中的语言、IANA 时区和报告屏幕尺寸保存在 `%LOCALAPPDATA%\TunnelBrowser\fingerprint.json`。保存后完全退出浏览器，再启动生效；切换代理和打开新标签页不会自动轮换参数。

语言同时用于请求的 Accept-Language 和浏览器语言。时区与屏幕参数通过 Chromium 原生环境接口应用，不改变 Windows 设置。User-Agent、操作系统和 GPU 等继续使用当前真实内核环境。

“清空当前 profile…”会先确认，然后关闭全部标签页，退出后清理 Cookie、缓存、历史、网站存储及环境参数；已保存的 VLESS 节点保留。下次启动重新建立 profile，恢复默认环境参数。这不等于随机生成新的硬件指纹。

本版参考开源指纹浏览器的环境一致性与持久化思路，仅实现环境参数。Canvas、WebGL、音频、字体等底层指纹伪装按本次约定留到后续，不承诺完整防关联或通过某个检测网站。已验证普通页面与同源 iframe；Worker、跨进程 iframe 等更多上下文仍需单独验证。

## 构建与验证

需要 Python 3.11+、Visual Studio 2022 C++ x64 构建工具和 Windows SDK、CMake 3.21+、Ninja。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Package
```

输出：`build\app\TunnelBrowser.exe`、`dist\TunnelBrowser-Windows-x64-0.2.0.zip`。依赖固定版本并校验下载哈希。

```powershell
.\build\app\bootstrapc.exe --module=TunnelBrowser --self-test
python .\tests\local_reality.py
```

本机已通过 21 项自检：节点解析、异常输入、DPAPI、真实 DLL、SOCKS 握手、核心重启、profile 持久化、参数校验和清空范围。集成测试使用临时回环 REALITY 服务端与独立测试 profile，已检查完整协议链路、代理故障阻断、主动关闭后直连、原生多标签、黄绿主题色、页面/iframe 环境参数，以及重启后 Cookie 和存储一致性。测试额外使用开发机的 `cryptography` Python 包，不是应用运行依赖。

## 维护与边界

CEF 官方 Windows 启动器加载客户端 `TunnelBrowser.dll`，Xray 仅在主进程按需加载。保留 Chromium 多进程与沙箱，没有独立 `xray.exe`。更新需要完全退出浏览器，不能热替换 DLL；CEF 与 Xray 都需持续跟进安全版本。

代理和 WebRTC 非代理 UDP、DoH、预取限制不等于操作系统级“零旁路”保证。真实节点、IPv6、所有网站、音视频通话和长期运行仍需验收。普通直连模式允许正常 DNS 解析。应用尚未签名，不承诺 Google 同步、DRM 网站或所有扩展兼容。

第三方许可和来源见 `THIRD_PARTY.md`；早期协议证据见 `research/xray-integration-evidence.md`。
