# Tunnel Browser 0.3

Windows x64 便携式浏览器。原生 Chromium 标签页由 CEF 150 提供，VLESS 由进程内 libXray 提供。解压完整目录后运行 `TunnelBrowser.exe`，无需管理员权限、系统代理或虚拟网卡。[已发布版本](https://github.com/WenLiCG/TunnelBrowser/releases)见 GitHub。

## 浏览与代理

- 每次启动从本地空白页开始，默认黄色直连。地址栏可打开网页，主动提交关键词时使用 DuckDuckGo 搜索。
- 在浏览器菜单选择“设置”，或在网页上右键选择“代理与环境设置”。右键菜单和设置窗口都有当前模式文字。
- 粘贴 VLESS 节点并点击“启用 VLESS”。已保存节点用当前 Windows 用户的 DPAPI 保护，留空可直接使用。接受 VLESS + RAW/TCP + REALITY + Vision 分享链接，省略 `type` 时按 RAW/TCP 处理。
- VLESS 已启用时，按钮变为“切换 VLESS 节点”。粘贴新节点后，浏览器会暂时阻断流量。换线完成后保存新节点，并通过新线路重新打开网页。换线失败时进入红色阻断，保留上次保存的节点。清空输入框即可重试已保存的节点。
- 绿色表示本地核心已启动且浏览器已应用代理规则。节点是否可达取决于当前配置和网络。红色表示代理异常，只有主动点击“关闭 VLESS · 直连”才恢复直连。红色状态下打开网页会弹出设置窗口说明原因。
- 切换路线时暂停浏览，并用新路线重新打开当前网页。切换前请保存表单内容，下载和长连接可能中断。

## 精简的浏览器组件

应用关闭 Google 浏览器登录与同步、翻译、云拼写检查、Safe Browsing 云检查、后台组件更新、遥测上传、Lens、投放和推送。扩展商店和关联的入口已移除。地址栏关闭联网建议，新标签页不载入远程内容。

应用在 Chromium 请求、DNS 解析和 VLESS 出站规则中拦截已知 Google 服务域名。依赖 Google 账号、验证码、字体或脚本的网站功能可能受影响。已知域名规则的范围见 [service_domains.h](src/service_domains.h)，用户主动打开普通网站仍使用网页自己的资源和网络路径。程序保留官方 CEF 内核及其许可文本。

发行包只包含所用的 CEF 运行文件和六种界面语言资源。WebGPU 和 SwiftShader 软件 WebGL 已裁剪。常规网页、视频和硬件加速路径继续使用 CEF 原生实现。

## 单一浏览资料

标签页、新窗口和二次启动共享一个持久 profile。语言、时区和报告屏幕尺寸保存在 `%LOCALAPPDATA%\TunnelBrowser\fingerprint.json`，完全退出后再打开生效。切换网络使用同一份资料。

网页自行打开的原生弹窗可能报告系统屏幕尺寸。普通标签和应用新建窗口使用已保存的尺寸。

“清空当前 profile…”在确认并退出后清除原有浏览资料与环境参数，保留 DPAPI 加密的 `node.dat`。

## 构建与验证

需要 Windows x64、Visual Studio 2022 C++ Build Tools 和 Windows SDK、CMake 3.21+、Ninja 及 Python 3.11.4+。正式打包使用 Python 3.14.3。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Test
python -m unittest discover -s tools -p "test_*.py"
python .\tests\local_reality.py
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Package
python .\tests\package_check.py
```

`-Test` 额外构建 `build\test` 并运行原生自检。回环测试需要 Python 的 `cryptography`，仅使用临时数据和本机 REALITY 服务器。正式文件位于 `build\app`，发行 ZIP 和 SHA-256 位于 `dist`。构建、打包、内核升级和验收细节见 [HANDOFF](HANDOFF.md) 与 [THIRD_PARTY](THIRD_PARTY.md)。
