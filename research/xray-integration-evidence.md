# Windows 浏览器嵌入 Xray：官方证据

核查日期：2026-11-02。范围：Windows Chromium 专用浏览器，以 CEF 主进程加载 libXray DLL、回环 SOCKS 连接为评估方案；不研究 CEF 本身。本文为源代码和官方文档核查，没有构建、加载 DLL、启动代理或连接真实节点，没有读取任何用户凭据。标为“建议/推断”的内容不是已运行验证。

## 结论

**建议：首版固定 libXray 与 Xray-core 的一个匹配版本，用现成 C ABI 运行一个进程内核心。** 无需自己实现 VLESS/REALITY/Vision，也无需自己再包一层 Go→C 导出。libXray 已提供 Windows x64/ARM64 发布资产和 C ABI；但其 README 明示不保证 API 稳定。主进程只加载一次 DLL，节点切换使用 `stopXray`→`runXray`，升级 DLL 后重启浏览器。此判断由下述构建、接口、Go runtime 生命周期证据支持，不等于产品稳定性验收。[Windows 构建源码](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/build/app/windows.py)、[C 桥接源码](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/cgo_bridge/main.go)、[README](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md)、[Go DLL 卸载问题](https://github.com/golang/go/issues/11100)

同包独立进程可隔离 Go runtime/崩溃，属于另一种工程取舍；当前以“不额外运行 xray.exe”为方案假设，故不把它作为首选实现。DLL 运行在主进程意味着核心故障可能影响浏览器主进程，这是一项架构推断，不是本次复现结果。

## 1. 本次核查的版本基线

| 项目 | 已核查事实 | 官方证据 |
| --- | --- | --- |
| libXray | `v26.10.31` 发布于 2026-11-01；Release API 返回非预发行；资产包括 `libxray-windows-x64.zip` 和 `libxray-windows-arm64.zip`。未下载校验资产。 | [Release](https://github.com/XTLS/libXray/releases/tag/v26.10.31)、[Release API](https://api.github.com/repos/XTLS/libXray/releases/tags/v26.10.31) |
| libXray 源码 | `v26.10.31` 是 annotated tag，其最终提交为 `7fe8528063e54343af8ba9a37431afc56000de21`；本次读取的 main 也是此提交。此处有 tag 证据，不是把 HEAD 当发行版。 | [Tag 对象](https://api.github.com/repos/XTLS/libXray/git/tags/1273f18d3e85bfc06d10112bee202fb8fff6d32c)、[提交](https://github.com/XTLS/libXray/commit/7fe8528063e54343af8ba9a37431afc56000de21) |
| Xray-core | libXray 的 go.mod 和构建脚本固定到 `v1.260327.1-0.20261031132019-a4a3dff5b20c`；Xray-core `v26.10.31` tag 指向完整提交 `a4a3dff5b20c2faae567b742c172c7b95e75c083`。 | [go.mod](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/go.mod)、[构建脚本](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/build/app/build.py)、[Core tag](https://api.github.com/repos/XTLS/Xray-core/git/ref/tags/v26.10.31) |

**建议：**记录 DLL/header 的下载来源、校验值、构建架构及以上源码版本；应用发布时将它们视为同一套接口契约。不要让生产包启动时自动追踪 `main` 或“最新版”。本次尚未产生可用二进制校验值。

## 2. Windows C shared DLL 支持

WindowsBuilder 输出 `windows_dll/libXray.dll` 和由 Go 生成的 `libXray.h`，设置 `CGO_ENABLED=1`、`CC=gcc.exe`、`CXX=g++.exe`，调用 `go build -buildmode=c-shared ./cgo_bridge`。官方 README 要求 GCC/G++ 在 PATH，Windows 原生 amd64 和 arm64 受支持。[构建实现](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/build/app/windows.py)、[README Windows 部分](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#windows)

发布 workflow 分别在 Windows x64/ARM64 runner 构建，对 DLL 和附带 EXE 检查 PE Machine 类型，并把 DLL、header、`xray.exe` 放进对应平台包。这能证明项目配置了这些检查；本次没有检查具体 Actions 日志，不能将 workflow 文件本身等同于某次测试通过。[构建/打包 workflow](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/.github/workflows/build.yml)

注意包内 `xray.exe` 来自 libXray 的 `desktop_bin`，其 CLI 要求 `run -dns <IP:port> -interface <name> -config <path>`，不是可以无条件当成上游 Xray-core 通用 CLI 替换件的同名程序。DLL 方案不必分发或启动它。[desktop_bin CLI](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/desktop_bin/command.go)、[构建 desktop_bin](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/build/app/build.py)

## 3. 实际 API：不要照搬旧版导出名称

本基线唯一结构化 Go 入口为 `Invoke(requestJSON string) string`，C 导出为以下两项。`CGoInvoke` 用 `C.CString` 分配返回值；每个非空返回指针由调用者用 **同 DLL 的 `CGoFree`** 释放，不能换成宿主 allocator。接口接收 UTF-8 JSON 文本，不是配置路径，也不要求把整个信封 Base64 编码。[C 桥接实现](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/cgo_bridge/main.go)、[Invoke 实现](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/invoke.go)

```c
char* CGoInvoke(char* requestJSON);
void CGoFree(char* value);
```

当前调用信封为 `{"apiVersion":3,"method":"…","payload":{…}}`。完整请求和响应各限制为 16 MiB；返回统一含 `success`、`data`、`error`。接口版本只接受 3，README 还明确同一个 3 内的契约变更需要消费者同步适配，因此“apiVersion 相同”不代表可以任意互换 DLL。[接口模型](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/invoke_model.go)、[Invoke 校验/响应](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/invoke.go)、[README API](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#api)

| 目的 | method / payload | 已核查行为 |
| --- | --- | --- |
| 启动 | `runXray` / `{"xrayJson":"<序列化后的完整 Xray JSON>"}` | `LoadConfig("json", reader)`→`core.New`→`Start`；只在成功后保存托管实例；已有实例时拒绝。 |
| 停止 | `stopXray` / 可省略 payload | `Close` 后清空托管实例；没有实例时也成功。 |
| 状态 | `getXrayState` | 返回 `data.running`；只表达核心实例运行状态。 |
| 版本 | `xrayVersion` | 返回 `data.version`，为 Xray-core 版本。 |
| 构造校验 | `testXray` / `{"xrayJson":"…"}` | 构造临时实例再关闭；不调用 `Start`。运行着托管实例时拒绝。 |

表中事实来自 [Invoke 分发](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/invoke.go)、[生命周期](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/xray/xray.go)、[校验实现](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/xray/validation.go)。`runXrayFromJson` 已不是单独方法，旧版 `RunXray`/`StopXray` 等 C 导出不能假设仍存在。[README runXray](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#runxray)

**建议：**宿主用 JSON 库嵌入 `xrayJson` 字符串，逐次检查 `success`，避免手写转义；把运行状态与“节点连通/目标网页可用”分开。本次未做 ABI/调用约定/MSVC 加载实测，应使用实际下载包同版 header 进行验证。

## 4. 稳定性、Go runtime 与进程内限制

libXray README 明确：维护者少、不保证 API 稳定、只兼容最新发布的 Xray-core；不能把该库包装层当成长期稳定系统 ABI。[README Note](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#note)

libXray 官方明确要求每个进程只使用一个独立构建的 Go runtime。每个原生 libXray 工件都包含 Go runtime；不能同时加载其他独立构建的 Go/cgo/gomobile 库来规避此限制。若需要几个 Go 包，应在同一次 Go 构建内产出一个工件。[README runtime 警告](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md)

Go 的 c-shared 卸载支持请求 #11100 仍开放；Go 维护者解释 runtime 自己会启动后台 goroutine，宿主无法自行保证它们已结束。Windows `FreeLibrary` 崩溃报告 #32497 被维护者归并到该问题。[Go #11100](https://github.com/golang/go/issues/11100)、[维护者说明](https://github.com/golang/go/issues/11100#issuecomment-1336041839)、[Windows 关联](https://github.com/golang/go/issues/32497#issuecomment-1368953594)

**建议：**`stopXray` 只是停止 Xray 实例，不是关闭 Go runtime；应用运行期间不对 libXray 做 `FreeLibrary`、DLL 热替换或反复加载/卸载。节点切换复用同一个已加载模块，DLL 更新等到整个浏览器退出。仅在主进程拥有该模块，不在每个 renderer 重复初始化核心。该生命周期建议是对上游约束的工程应用，尚未在此产品验证。

Xray-core 的系统 dialer 持有进程级 DNS client 和 outbound manager；libXray 使用一个托管实例和 mutex。`testXray`、`pingBatch` 和 `runXray` 生命周期被串行化；绕过这些 API 自己创建实例不受托管锁检测，也不会被自动恢复。[Core dialer 全局变量](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/transport/internet/dialer.go)、[libXray lifecycle](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/xray/xray.go)、[README 并发限制](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#api)

`testXray` 不是无副作用沙箱：构建器可能读取 GeoData/证书、创建日志文件、改变 env/日志/DNS 全局状态，特定协议可能创建资源或后台任务；构造成功不证明监听成功、权限满足或真实连通。当前 Core 构造失败不返回部分实例，libXray 无法在这种路径关闭该部分实例。[README testXray](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#testxray)、[Core 实例创建](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/core/xray.go)

**建议：**首版只从节点字段生成应用拥有的最小配置，不将任意外来完整 Xray JSON 当作安全输入执行；不要在活动核心旁边调用测试 API。宿主还需处理端口占用、启动失败和停止失败。`getFreePorts` 返回候选端口后已关闭监听，并不预留端口，应把最终 bind 失败作为正常错误处理。[端口 API 说明](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#port)

## 5. Xray-core 本身也能作为 Go 库

上游公开 `core.StartInstance(configFormat, configBytes)`、`core.Dial`，源码标记为 `xray:api:stable`；`DialUDP` 标为 beta 且注释说明 Deadline 系列未实现。默认仅 protobuf 配置支持，JSON 格式需要导入注册包。`core.New(config)`、实例 `Start()`/`Close()` 构成 libXray 实际使用的生命周期。[Core functions](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/core/functions.go)、[Core Instance](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/core/xray.go)、[libXray 实际导入和调用](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/xray/xray.go)

**推断：**C++ CEF 直接使用这些 Go API 仍要自己承担 C ABI、内存管理、构建和 runtime 生命周期维护；针对当前简单浏览器，复用 libXray 的两个 C 导出比维护自有 bridge 更少代码。这里不把源码上的 stable 标记扩大解释为“全部 Core 内部包或 JSON 配置永久兼容”。

## 6. VLESS + RAW/TCP + REALITY + Vision 的准确含义

- RAW 是原 TCP 传输的改名。固定版本源码同时接受 `raw`/`tcp`，二者映射到内部 `tcp`；同时接受 `method`/`network`，两者并存时 `method` 优先；`rawSettings` 优先于 `tcpSettings`。[RAW 官方说明](https://xtls.github.io/config/transports/raw.html)、[固定版本 TransportProtocol/StreamConfig](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/infra/conf/transport_internet.go)
- VLESS 是代理协议，REALITY 是传输安全，`xtls-rprx-vision` 是 VLESS 的 flow；不是 `security: "xtls"`，后者在当前 Core 被明确拒绝。当前 REALITY 支持 RAW/XHTTP/gRPC，但不能由此推导 Vision 能搭配所有 REALITY 传输。当前目标组合 RAW + REALITY + Vision 有官方说明和运行代码支持。[传输配置源码](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/infra/conf/transport_internet.go)、[VLESS flow 文档](https://xtls.github.io/config/outbounds/vless.html#flow-string)、[VLESS outbound 实现](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/proxy/vless/outbound/outbound.go)
- 普通 `xtls-rprx-vision` 会拒绝目标 UDP/443；`xtls-rprx-vision-udp443` 才允许这类流量。不要为了“浏览器支持 QUIC”偷偷改成后者，应按用户节点 flow 和浏览器代理能力验收。[官方 flow 文档](https://xtls.github.io/config/outbounds/vless.html#flow-string)、[实现中 allowUDP443 分支](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/proxy/vless/outbound/outbound.go)
- 文档所述 splice 是 Linux kernel 功能，自动启用条件包含 Linux；不能把 Linux 的零拷贝/性能宣传直接作为 Windows DLL 性能结论。[官方 Splice 条件](https://xtls.github.io/config/outbounds/vless.html)
- 当前 VLESS 配置支持简化 `settings.address/port/id/encryption/flow`，也保留 `vnext[0].users[0]`；目标传统 REALITY 节点通常使用 `encryption: "none"`，它不表示 REALITY 传输未加密。配置仍必须匹配服务端。[VLESS builder](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/infra/conf/vless.go)、[VLESS 官方配置](https://xtls.github.io/config/outbounds/vless.html)
- 当前 REALITY 客户端字段为 `serverName`、`fingerprint`、`password`、`shortId`，可选 `spiderX`、`mldsa65Verify`；`publicKey` 旧字段仍兼容，非空 `password` 优先。密码字段要求 Base64URL 解码后 32 字节；shortId 最长 16 位 hex 且必须可解码，空值需服务端接受。客户端不能填服务端 `target/dest`，否则 builder 会按服务端配置处理。[REALITY 文档](https://xtls.github.io/config/transports/reality.html)、[REALITY builder](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/infra/conf/transport_security.go)

**建议：**界面显示“RAW/TCP”，导入兼容 `type=tcp`，内部生成当前固定版本支持的一种形式；不需实现多套传输。服务端地址、端口、UUID、SNI、fingerprint、REALITY password/publicKey、shortId 和 flow 采用明确字段验证。保留实际节点要求，不自行猜测私钥或替换 SNI。

## 7. 直连、DNS 和错误泄露边界

Xray 第一个 outbound 是未匹配路由时的默认出口；`routing.domainStrategy: AsIs` 不为 IP 分流额外解析，IPIfNonMatch/IPOnDemand 则可能触发内置 DNS。**建议：**“代理模式”的默认出口固定为 VLESS，避免默认 Freedom 或自动直连回退；停止/失败时让浏览器显示错误。此处的闭合失败行为还依赖 CEF 的代理配置和旁路网络测试，不能只凭 Xray 配置证明。[默认 outbound](https://xtls.github.io/config/outbound.html)、[路由与 DNS 策略](https://xtls.github.io/config/routing.html)

`localhost` DNS 使用系统配置，官方文档说明这些请求不受 Xray 控制；`tcp+local`/`https+local`/`quic+local` 也绕过路由直接查询。非 local DNS 可进入路由，DNS 服务器域名自身解析还可能形成回环。**建议：**分清“解析代理节点地址的 bootstrap DNS”与“用户目标域名 DNS”；避免宣称单填一个 DNS 字段就消除泄露。对只使用 SOCKS 的最小浏览器应尽可能将目标域名原样交给代理，但浏览器预取、DoH、WebRTC 和独立网络组件仍需另验。[官方 DNS 模式和回环说明](https://xtls.github.io/config/dns.html)

libXray README 的 Android `SetDNS`/`ResetDNS` 是 Android 专有且影响进程级 Go resolver，不能复制当成 Windows DLL 导出。[README DNS resolver](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/README.md#dns-resolver)

部分 Core 参数校验错误会包含输入值，例如无效 REALITY password、privateKey、shortId；Invoke 会把错误文字返回。**建议：**不能直接把整份配置、完整分享链接或原始错误写进常规日志；向用户显示分类错误并做字段脱敏。这是源码可见的数据暴露风险，并非声称本项目已泄露过。[REALITY 校验错误](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/infra/conf/transport_security.go)、[Invoke 错误返回](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/invoke.go)

## 8. 许可证与技术分发记录

libXray 自身为 MIT，分发时应保留其版权/许可声明；这不覆盖它链接进去的所有依赖。[固定版本 MIT LICENSE](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/LICENSE)

Xray-core 为 MPL 2.0。原文第 3.1–3.4 节涉及 Covered Software 源码分发、可执行形式对应源码获取方式、Larger Work 以及保留 notices。工程分发清单应保存精确版本的许可证、对应源码和本地修改，给接收者明确源码获取方法，并保留依赖许可证；不能只在浏览器 About 写一个“libXray MIT”就覆盖 Core 和依赖。这是对许可证原文的分发事项摘要，不是针对具体商业产品的法律意见，也不推断整个宿主浏览器一律必须采用 MPL。[固定版本 Xray-core LICENSE](https://github.com/XTLS/Xray-core/blob/a4a3dff5b20c2faae567b742c172c7b95e75c083/LICENSE)、[libXray 依赖清单](https://github.com/XTLS/libXray/blob/7fe8528063e54343af8ba9a37431afc56000de21/go.mod)

## 9. 进入实现后的最小验收

以下是建议的检查，不是已通过结果：

1. 固定 x64 DLL 与 header：核验哈希/架构/导出；从 C++ 调用版本和非法请求，再正确释放返回缓冲。
2. 单实例启动、停止和再次启动；端口占用、无效字段、重复启动有明确失败，不造成宿主崩溃。
3. 导入只处理所需节点字段；拒绝或明确报告不支持的传输/flow，不静默替换；错误输出不含节点秘密。
4. 故意让核心不可用时，浏览器请求失败，不能退回直连；分别检查普通网页、重定向、WebSocket、下载、IPv6，以及 DNS/WebRTC/UDP 旁路。
5. 用用户授权真实节点检查网站可达、节点切换、断网恢复和浏览器完整退出；抓包/连接审计后才声称流量边界符合要求。

本次省略：构建、DLL 加载、CEF 联调、运行性能、实际代理和泄露验收。主要未消除风险：不稳定包装 API、进程内 runtime/崩溃耦合、DNS/浏览器旁路和 Windows 上的实际生命周期行为。
