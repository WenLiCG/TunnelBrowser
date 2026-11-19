# 脚本 71 冻结包入库（archive/handoff-71-v3-r1）

方法：software-handoff v1.3 / workflow 1.4。

本文件是仓库里的可读交接。冻结包是已经复核过的附件，按原字节放入本目录。这次入库没有新实现，也没有改脚本行为。用户已于 2026-11-19 停止开发。本归档不恢复开发。

四个脚本版本只留在 zip 里存档。没有通过的脚本是未接受的存档，不能当作可用脚本的替代，也不是 QA 通过。

仓库根目录的产品交接 `HANDOFF.md` 仍是 Tunnel Browser 0.2.0 的交接，本次未改。

## 本次核对

本节是写入本文件时核对到的事实。下一节「历史交接正文」是冻结包原文，本次没有重跑其中的测试，也不把作者自测改成独立 QA。

### 生成方式

- 来源：已复核的冻结附件 `handoff-71-v3-r1-frozen-20261119.zip`。
- 放入路径：`archive/handoff-71-v3-r1/handoff-71-v3-r1-frozen-20261119.zip`。
- 这是冻结附件入库，不是新实现。
- zip 内四个脚本保持原样，只作存档。

### 仓库提交

- 分支：`archive/handoff-71-v3-r1`
- base main：`c70f2f2c3e7894f29ae850bc1d5e7500d6ae759e`（短 SHA `c70f2f2c`）
- 该 main 的 tree：`4b75a78dbf92636dbbbfbe4c385a6a4a606660ce`
- 新 head：本分支上加入本目录的这一次提交，父提交是上面的 main。完整 head SHA 写在草稿 PR 正文。提交哈希覆盖本文件，所以完整 head SHA 不写进本文件。
- 产品源码树没有其他改动。产品 zip 没有改动。

### Release（只读，未编辑）

- id：`444130767`
- draft：`true`
- published_at：null
- updated_at：`2026-11-13T11:09:57Z`
- 资产 `TunnelBrowser-Windows-x64-0.2.0.zip` 的 digest：`sha256:de1d20b7592b1e79f75140ef0b33082ad5f37eb51b10fbacb899257fe313a117`
- 本次未上传、未改说明、未发布。

### 冻结包完整性

- zip sha256：`b59c0626c6e3f4cf3103225461e56db390520228c445f51ee946afb7de6c1350`
- zip 清单：82 个文件，未压缩合计 443167 字节。提交进仓库的是该 zip 文件本身（148221 字节）。
- 冻结包内 `HANDOFF.md` sha256：`d35602e95eda4696b083d2b0ddfa8f8d23bcf317383d53ffe91c43e87b289fe0`。本可读文件在原文前增加了本节，所以哈希不同。原文仍在 zip 内。
- 外层 `SHA256SUMS` 有 80 条。该文件自身 sha256：`ab37917aaa9bd2bcffffbd8af4f513aa74c8469b58d0ec4bede4d9db121fe540`。
- 完整性脚注：这 80 条不列出 `SHA256SUMS` 自身，也不列出 `author-v3-r1-delivery/SHA256SUMS`。82 个成员和 80 条清单的差就是这两个文件。没有为了补上这两行而修改原始 zip。
- 嵌套 `author-v3-r1-delivery/SHA256SUMS` sha256：`74ecdc767abb0fd5d796b62e669a28560fc2066ef72868f6d3fbe32debc0c679`，31 条映射。本次对抽出的文件重新核对：外层 80 条全部吻合；嵌套 31 条映射到包内 `scripts/` 与 `author-v3-r1-delivery/` 后全部吻合。嵌套清单写的是原来的绝对路径，那些路径不在本仓库。

### 四个脚本的清单哈希

哈希来自 `SHA256SUMS`，并与抽出的文件一致。四份都是未接受的存档。

| 处置 | 包内路径 | sha256 |
| --- | --- | --- |
| 历史无效。未接受的存档 | `scripts/71-tb4-config.ps1`（v1） | `2665731807fd591bb29327e2c4f3262b2cdf79510491fef58f5ca57fb3b4edc0` |
| 历史无效。未接受的存档 | `scripts/71-tb4-config.v2.ps1`（v2） | `96a75c20218635c20e0ea3dddc5c60364ad08749f90a9f7fdf8a5fd954c24424` |
| 首轮 QA fail 后冻结。未接受的存档 | `scripts/71-tb4-config.v3.ps1`（v3） | `0b28973de68924c4de658ac9e55200ffd1cf8f9935e98c9290da86f92da31857` |
| 第二轮独立 QA fail / no-go。未接受的存档 | `scripts/71-tb4-config.v3-r1.ps1`（v3-r1） | `57ed8d2beb604b0c061e0346303ba252947bea3b94ead89ff3a98372945d55cc` |

### 仍保留的结论与未测项

这些是冻结包里已有的记录。本次只核对包还在，没有重跑。

- 作者自测 41/41 不是独立 QA。
- 两轮 fail / no-go 证据仍留在冻结包里：首轮 v3 fail；第二轮 v3-r1 fail，质量建议 no-go。
- 未测或 blocked：Windows PowerShell 5.1 和目标机没有测试。
- 两个未解决的 GUID 边界：`B01_list_guid_suffix`；`B02_get_wrong_guid_with_logon`。
- 用户于 2026-11-19 停止开发。本归档不恢复开发。

本次没有实现、修复、运行脚本、跑测试、登录 Windows、部署、发布或合并，没有改 Release 444130767 或其 zip，没有向已合并的 PR #2 追加内容，也没有把冻结包以外的恢复计划或 blocked 主机报告加进来。

## 历史交接正文

以下正文按字节复制自冻结包内 `HANDOFF.md`（sha256 `d35602e95eda4696b083d2b0ddfa8f8d23bcf317383d53ffe91c43e87b289fe0`）。它记录入库前的交接。其中「工作树不是 git 仓库」和「尚未推送」说的是当时的脚本工作树。上面一节才是本次仓库归档。

# TunnelBrowser 脚本 71 冻结交接包（software-handoff v1.3）

整理时间：2026-11-19 11:25 PST（America/Los_Angeles）。生成方式：grok 程序员，原生 Grok。本文件是停止开发后的文档封存，不是第三轮修复，不启动实现，不重跑测试，不登录 Windows，不恢复通道，不写系统配置，不发布 Release，不自行写入 GitHub，不联系其他人，不新建 Project 或 PR。

已有独立 QA 停止交接（未改原文）：`qa-stop-20261119/QA-HANDOFF-STOP-20261119.md`（原件 `/workspace/tunnelbrowser/QA-HANDOFF-STOP-20261119.md`，mtime 2026-11-19 11:21:38 PST，sha256 `7fc1295ce2fbc830a4d11fb33b742803be247d368f6a097c37bc1265509978a7`）。本包是开发侧冻结归档，与该 QA 停止交接并列，不覆盖。

归档性质：**仅存档，禁止执行。** 见同目录 `DO-NOT-EXECUTE.txt`。

## 项目 / 任务 / 阶段 / 接手人

- 项目：TunnelBrowser
- 任务：脚本 71（Windows 测试机配置：只启用 Logon 失败审核；Security 日志 maxSize 至少 268435456，不缩小、不清理、不改保留/溢出）。规格见 `scope/PROMPT-71.md`、`scope/PROMPT-71b.md`、`scope/DECISION-71-v2-20261118.md`。
- 当前阶段：开发已停止；独立 QA 第二轮已结束并冻结；本包供 PM 汇总后，由既有项目唯一更新负责人归档到 GitHub。
- 接手人：PM（兼任技术负责人）统一归档；不安排第三轮实现，除非另有明确授权。
- 本任务唯一写者（停止前）：grok 程序员（v3 与 v3-r1）。v1/v2 历史由 Codex 生成，见 `scope/71b-codex-report.md` 与 `scope/PROMPT-71.md`。本次封存未调用 Codex。

## 目标、范围与验收（引用已确认版本，不改写 QA 结论）

原范围（PROMPT-71 / DECISION-71-v2）仍有效：仅 Logon 失败审核（无 success 开关）；Security maxSize ≥ 268435456 且不缩小；退出码 0=全成功且复制成功，2=复制失败，1=配置/脚本失败（复制已记录时）。不扩大范围。

独立验收以 QA 两轮原文为准，作者自测不是独立结论，更不是用户批准、合并或发布：

1. 首轮（冻结 v3 `0b28973de68924c4de658ac9e55200ffd1cf8f9935e98c9290da86f92da31857`）：fail。16 pass / 4 fail，`run.exit=1`。证据：`qa-round1-v3-0b28973d/`（原目录 `/workspace/tunnelbrowser/qa-71-v3-0b28973d/`，run.log mtime 2026-11-19 06:25:31 PST）。四反例均 writes=2 set=1 sl=1 exit=0：`QA_malformed_audit_unrecognized output`、`QA_malformed_audit_System audit policy`、`QA_malformed_audit_Logon/Logoff`、`QA_list_guid_without_label`。
2. 第二轮（候选 v3-r1 `57ed8d2beb604b0c061e0346303ba252947bea3b94ead89ff3a98372945d55cc`）：fail；质量建议 no-go；上机 blocked，不自动安排第三轮实现。原文：`qa-round2-v3-r1-57ed8d2b/QA-71-v3-r1-REVIEW.md`。原 QA 套件 **20/20**（`run.log` mtime 2026-11-19 06:39:40 PST，`run.exit=0`）；新增边界 **4 pass / 2 fail**（`boundaries.log` mtime 2026-11-19 07:08:46 PST，`boundaries.exit=1`）。作者 41/41 不是本独立结论。

第二轮两项高严重度阻塞缺陷（仍写入，预期零写）：

- `B01_list_guid_suffix`：list CSV 中官方 GUID 尾随 FFFF 仍被接受，离线桩记录 /set 与 sl 各 1 次，exit 0。
- `B02_get_wrong_guid_with_logon`：get CSV 名称 Logon 但 GUID 为 Logoff 时仍被接受，离线桩记录 /set 与 sl 各 1 次，exit 0。

通过的新增边界（零写）：`B03_get_conflicting_values`、`B04_list_conflicting_names`、`B05_localized_unknown`、`B06_get_csv_missing_setting`。

Windows PowerShell 5.1、目标机 GUID 实际映射/实际原值、恢复通道、配置写入及 Release：均未运行 / 未授权，不视为通过。离线模拟缺陷不等于已在 Windows 造成写入；亦不能据此证明生产无风险（QA 停止交接已澄清原报告相关措辞，不回写历史结果）。

## 仓库 / 分支 / diff / base / head / Project（据现有文档，不猜）

脚本 71 的工作树**不是 git 仓库**，故本任务的 branch / base / head / PR：**不适用**。

- 依据：收入本包的 `scope/PROMPT-71.md` 正文写明工作目录 `/workspace/codex/tunnelbrowser/TB-4-investigate/`「本目录不是 git 仓库」。当前交付位于 `/workspace/tunnelbrowser/tbshare/tb4/jobs/`；本次只读核对 `/workspace/tunnelbrowser`、`tbshare`、`tbshare/tb4`、`tbshare/tb4/jobs`、`/workspace/codex/tunnelbrowser` 及 `TB-4-investigate` 均无 `.git`。
- 文件级 diff（非 git）：作者交付 `author-v3-r1-delivery/71-v3-to-v3-r1.diff`（冻结 v3 → v3-r1）；另有 `author-v3-delivery/evidence/71-v2-to-v3.diff`。

产品仓库在既有 TB-1/TB-2/TB-3 文档中有记载，但那些任务不是脚本 71，不能把旧 main / Release 当作本包已入库位置：

- 仓库 URL：`https://github.com/WenLiCG/TunnelBrowser`。来源：`/workspace/tunnelbrowser/tbshare/qa/TB-2-TASK.md`（「仓库：https://github.com/WenLiCG/TunnelBrowser」）、同目录 `TB-3-TASK.md`（相同句子）、`/workspace/tunnelbrowser/TB-1-evidence-reqs.txt`（仓库同一 URL，并记载当时固定版本 `ebad1c852e3985560990ed65ee351edd45990cc6`，main / v0.2.0）。
- 历史产品 PR（与脚本 71 无关）：PR #1（TB-2，`tbshare/qa/TB2-VERDICT-20261113.md`）、PR #2（TB-3，`tbshare/qa/TB3-LINUX-VERDICT-20261113.md`，文中记载 2026-11-13 核实的 head/base，且为 open/draft/未合并）。本次未针对脚本 71 开 PR，也不把这些 SHA 当作 71 的 base/head。
- GitHub Project 看板或带编号的「唯一执行 Project」URL：在本次检索的本项目文档中**未找到**，记为**未知**，不猜测、不创建。
- 「唯一执行入口」作为流程要求出现在：`software-team-workflow`（「仓库工作经当前项目唯一执行入口」）；以及 `TB-2-TASK.md` / `TB-3-TASK.md`（「若共享电脑无推送权限，把补丁交给该项目唯一执行入口开 PR」）。这些来源没有给出 Project 编号或入口 URL。QA 停止交接亦写明：本次未实时核对仓库 URL、PR、base/head，由既有项目唯一更新负责人从既有项目记录核实，不以旧 main 或 Release 猜测；QA 未执行 GitHub 更新。本开发交接同样不自行推送。

## 生成方式与顾问意见

- v1 / v2：Codex（见 `scope/PROMPT-71.md`、`scope/PROMPT-71b.md`、`scope/71b-codex-report.md`）。顾问意见以原报告为准，不当本包新验证事实。
- v3 与 v3-r1：grok 程序员，原生 Grok。作者交付：`author-v3-delivery/DELIVERY-71-v3-20261119.md`、`author-v3-r1-delivery/DELIVERY-71-v3-r1-20261119.md`。
- 本封存：grok 程序员，原生 Grok。未请 Codex 顾问（任务明确不要调用 Codex；且为停止后的文档整理，不是实现）。

## 版本身份与处置（历史无效 vs 候选 no-go）

本次只读核对这些现文件 sha256，与作者交付及 QA 停止交接记载一致；未改原文件。

| 处置 | 文件（归档副本在 `scripts/`） | sha256 |
|---|---|---|
| 历史无效 | `71-tb4-config.ps1`（v1） | `2665731807fd591bb29327e2c4f3262b2cdf79510491fef58f5ca57fb3b4edc0` |
| 历史无效 | `71-tb4-config.v2.ps1`（v2） | `96a75c20218635c20e0ea3dddc5c60364ad08749f90a9f7fdf8a5fd954c24424` |
| 历史无效（首轮 QA fail 后冻结） | `71-tb4-config.v3.ps1`（v3） | `0b28973de68924c4de658ac9e55200ffd1cf8f9935e98c9290da86f92da31857` |
| 候选 no-go（第二轮独立 QA fail） | `71-tb4-config.v3-r1.ps1`（v3-r1） | `57ed8d2beb604b0c061e0346303ba252947bea3b94ead89ff3a98372945d55cc` |

v1/v2 使用的脚本 GUID 与官方 Logon 子类 GUID 不一致等问题，见 `author-v3-delivery/docs/GUID-EVIDENCE.md`。v2 曾准备上机但作业通道/写门问题导致 blocked，见原 `jobs/RUN-71-v2-20261119-blocked.md`（**因含主机地址/账号，未纳入本包**）。冻结 v3 因解析过宽（非空即过、GUID 无名称仍确认）被首轮 QA 判 fail。v3-r1 修了首轮四反例对应问题，但第二轮仍 fail（GUID 尾随 FFFF；名称 Logon 配 Logoff GUID），质量建议 no-go。全部仅存档，禁止执行。

## 已完成与未完成

已完成（停止前，仅历史记载）：v1/v2/v3/v3-r1 脚本及作者交付；作者对冻结 v3 的四反例复现；v3-r1 的 TDD 自测（含原 16、补充分支 41、隔离 QA 副本 20）。独立 QA 两轮及 QA 停止交接已存在，本包只复制不改。

未完成 / 明确停止：第三轮修复未做且本包不启动；Windows 5.1 未测；目标机未测；上机 blocked；通道未恢复；配置未写；Release 未授权；GitHub 尚未由本角色更新（应交 PM 经唯一既有入口归档）。跨项目授权不可继承。

## 证据（不重跑；时间来自原日志/mtime，时区 America/Los_Angeles）

环境（QA 与作者自测相同限定）：共享 Linux，pwsh 7.5.13，auditpol/wevtutil 为离线桩。这不是 Windows 5.1，也不是目标机。

独立 QA（结论以此为准）：

- 首轮：`qa-round1-v3-0b28973d/`。`Run-QA.ps1` `29a215d9c1d3212469d6ec250200865cdc76c6a58efb62f2adc399386076045f`；`run.log` `2393431b9a75f9aab1894b4cb1d677e275ecaa9d794d05ef15965bf6b56a3a0f`；`run.exit` `4355a46b19d348dc2f57c046f8ef63d4538ebb936000f3c9ee954a27460dd865`（内容为 1）；`results.results.json` `78bef2b6156c3b8609abfcf321ec987de6c77747ecbf20939521291e85a5c2b1`。16 pass / 4 fail。首次失败已保留，未见用重试覆盖。
- 第二轮原 20 例：`qa-round2-v3-r1-57ed8d2b/run.log` `1c421725836e6080fa69d3db138d1db44a83fb1c5e5c8ddd0e020712e97b5b08`；`run.exit` `9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa`（内容为 0）。20 pass。该 `run.log` 的 sha256 与作者隔离 QA 副本 GREEN stdout（`author-v3-r1-delivery/evidence/green/green-qacopy-20261119-063531.stdout.txt`）相同，只说明这 20 例输出一致，**不能**把作者自测升格为独立结论。
- 第二轮新增边界：`Run-Boundaries.ps1` `320f8f902488bc11a1bfdc53493d36d26d173408ed9b8b29b649e377b3607f84`；`boundaries.log` `05e948b867286c38591272091fa1616f813b44445ed7466ee2e470a60a94ae5a`；`boundaries.json` `5cc04e58fdf9e789d10fc680b7971367240fb58fde2c5d5a3f9525a5c17e8715`；`boundaries.exit` `4355a46b19d348dc2f57c046f8ef63d4538ebb936000f3c9ee954a27460dd865`（内容为 1，故与首轮 `run.exit` 哈希相同）。4 pass / 2 fail。复现命令以 QA 原文为准，本包不执行：`PATH=/home/box/opt/powershell/7:$PATH /home/box/opt/powershell/7/pwsh -NoProfile -File /workspace/tunnelbrowser/qa-71-v3-r1-57ed8d2b/Run-Boundaries.ps1 -ScriptUnderTest /workspace/tunnelbrowser/tbshare/tb4/jobs/71-tb4-config.v3-r1.ps1 -EvidenceFile /workspace/tunnelbrowser/qa-71-v3-r1-57ed8d2b/boundaries`
- 第二轮评论：`QA-71-v3-r1-REVIEW.md` `34f4dc7a0dfba3a3f303f550b038b78e86dfae198b63c64a64594070e9ef00ba`。

作者自测（非独立 QA；命令与时间见 `author-v3-r1-delivery/DELIVERY-71-v3-r1-20261119.md`，本包不重跑）：

- 对冻结 v3 复现首轮四反例：2026-11-19 06:28:13–06:28:49 PST，16 pass / 4 fail。
- RED（r1 测试打冻结 v3）：06:31:11–06:31:59 PST，25 pass / 16 fail / 共 41。首次失败已保留。
- GREEN 原 16：06:33:59–06:34:34 PST，16/16。
- GREEN 原 16+补充：06:34:38–06:35:25 PST，**作者 41/41**。
- GREEN 隔离 QA 副本：06:35:31–06:36:08 PST，20/20。副本方法见该交付第 5.4 节。

跳过 / 阻塞 / 未运行：Windows 5.1 解析与目标机 `/list` 对照、真实写入、回退、通道恢复、Release——全部未运行且现授权不允许。v2 上机 historically blocked，详情因含主机资料未打包。

## 缺陷、未测、风险与授权边界

- 独立结论：v3-r1 仍 fail / no-go。GUID 匹配无首尾约束；get CSV 用名称 Logon OR GUID 命中，导致名称与错误 GUID 冲突时仍写入。
- 未测：Windows PowerShell 5.1、目标机、真实配置写入。
- 风险：离线桩已证明错误接受可走到 /set 与 sl；未上机故无本次生产写入证据，但不能证明生产无风险。
- 授权：用户批准的是停止开发并整理交接，供 PM 经唯一既有入口归档 GitHub。不是批准修复、合并、上机或发布。任何恢复开发/上机/发布需另有授权。本包去除凭据、主机账号、私聊；不打包未发布 Release 包；不全量打包 tbshare。

## 归档范围、省略与未收录

收录：`scripts/` 四个脚本版本；`scope/` 规格与历史 Codex 报告（不含含主机资料的 blocked 报告）；`author-v3-delivery/`（去掉 `docs/RESTORE-SESSION-PLAN.md`，改为 `RESTORE-SESSION-PLAN.OMITTED.txt` 说明原因）；`author-v3-r1-delivery/` 全文；两轮 QA 目录原文复制；QA 停止交接原文复制；本 `HANDOFF.md`、`SHA256SUMS`、`DO-NOT-EXECUTE.txt`。

明确不收录（原件仍留在共享电脑原路径，未删除、未修改）：

- `tbshare/tb4/jobs/RUN-71-v2-20261119-blocked.md` 与 `71-v3-delivery/docs/RESTORE-SESSION-PLAN.md`（含主机地址/账号）。
- 全量 `tbshare`、作业通道 runner/心跳、Windows 原始采集、RDP 截图、未发布/已发布的产品 zip（例如 `delivery-main-c70f2f2c` 下的安装包）。
- 脚本 70、私聊、凭据与密钥。
- GitHub 入库后的提交 SHA/仓库内路径：本次尚未推送，未知。

未覆盖：GitHub Project URL 未知；71 在产品仓库中的目标路径/分支未知（工作树不是仓库）；目标机与 Windows 5.1 证据不存在。

## 下一步、建议技能、完成判据

- 下一步（仅归档）：PM 按既有项目记录确认唯一更新入口后，将本 `HANDOFF.md`、`SHA256SUMS` 与 zip 作为真实附件归档到 GitHub；保留 fail/no-go，不称质量已通过；归档后补记仓库可访问路径与提交 SHA。不要把共享电脑临时绝对路径当作唯一来源。
- 不要开第三轮修复，除非另有授权。若日后恢复开发，建议 software-handoff v1.3 与 software-release-gate v1.0；重验必须绑定当时候选，不能沿用作者 41/41。
- 本封存完成判据：独立归档目录与 zip 已生成，哈希已核对，敏感项已排除，原脚本/报告未被修改，本角色未写 GitHub。GitHub 上出现提交不是本角色已完成事项。
