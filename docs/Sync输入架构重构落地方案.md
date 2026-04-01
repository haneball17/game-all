# Sync 输入架构重构落地方案

## 1. 文档定位

本文档用于指导 `game-all` 中 `Payload/modules/sync/SyncMod.cpp` 的输入执行层重构，目标是把当前“多输入路径长期并行强改”的补丁式实现，演进为**单一逻辑状态 + 分层输入适配器 + 路径观测驱动**的稳定架构。

本文档不是替代 `docs/Rust完整重构落地计划.md`，而是其在 **Sync 输入执行层** 上的专门实施方案：

- `docs/Rust完整重构落地计划.md` 负责总方向；
- 本文档负责当前最紧迫的输入卡键/持续移动问题的分阶段落地；
- 后续实施、评审、验收、回退，统一以本文档为执行基线。

---

## 2. 当前基线

### 2.1 可回退基线

当前已冻结一个“可用但未彻底解决问题”的基线分支：

- 分支：`baseline/sync-usable-20260402`
- 基线提交：`5cbdc08 fix(sync): snapshot usable baseline before input-architecture refactor`

该基线的定位是：

1. 已包含最近一轮方向键收敛修补、方向键 claim 跟踪、`[DIR]` 日志、测试样例；
2. 可以继续用于实机回退、对照、二分与日志复盘；
3. 不是最终稳定方案，不能再作为长期演进主线继续叠补丁。

### 2.2 当前已知现象

基于诊断日志：

- 日志文件：`artifacts/run/logs/exports/diagnostic_20260402_023405_20260402_023632.log`
- 现象：仍然存在**小概率松键后持续移动**；
- 触发特征：方向键高速切换、长按后松开、切换前后台语义时最明显。

### 2.3 已确认的日志特征

本轮日志中已经确认：

1. 出现大量 synthetic `RawInputData vkey=0x00` 事件；
2. 存在 `Raw claim` 与 `Win32 claim` 长时间不一致窗口；
3. 出现 `DirClaims: Raw=1 Win32=0` 的持续漂移；
4. 漂移不是单个 release 脉冲数量不足，而是当前状态模型本身不稳。

诊断统计（来自该日志）如下：

- `vkey=0x00` synthetic RawInput：`1204` 次；
- 其中发生在方向 claim 活跃期间：`602` 次；
- Raw/Win32 claim 不一致窗口：`197` 个；
- 最长漂移窗口约 `4.7s`，期间 Win32 已 release，但 Raw 仍残留按下。

---

## 3. 问题归因

### 3.1 `neutral raw`（`vkey=0x00`）策略本身不稳

当前 `RawInputMappingState.h` 在方向键长按时，会返回一个“中性 RawInput 事件”：

- `vKey = 0`
- `MakeCode = 0`
- `Flags = 0` 或 `RI_KEY_BREAK`

该策略的设计目的是“吞掉源重复，不重复宣称方向仍按下”，但实机日志表明它带来了新的不确定性：

- 目标程序如何解释 `vkey=0x00` 并无稳定保证；
- 它既不是一个真实方向 transition，也不是协议层的逻辑状态变化；
- 它把“重复抑制”变成了一种未定义事件注入行为。

### 3.2 `rawClaimed / win32Claimed` 双状态模型容易漂移

当前实现中，方向键在不同通道上分别维护状态：

- `rawClaimed`
- `win32Claimed`

问题在于：

1. 两者都被主逻辑直接读取；
2. 两者都可能独立 press/release；
3. 一方先 release 但另一方未及时 release 时，会把“内部不一致”暴露成持续移动；
4. 后续只能依赖 clear / bypass / 超时警告来收敛，而不是由统一逻辑态驱动。

### 3.3 当前实现同时长期强改多条输入语义

当前执行层长期并行干预：

- `GetAsyncKeyState`
- `GetKeyboardState`
- `GetRawInputData`
- `GetRawInputBuffer`
- `GetMessage / PeekMessage`
- `DirectInput`

这类实现的核心问题不是“覆盖太多”，而是：

- 查询型接口、事件型接口、设备轮询接口被统一当成“同一类状态输出”；
- 不同路径的语义天然不等价，边沿时刻尤其容易漂移；
- 结果就是当前的 `claim + force release + clear + mapping repeat suppression` 逐渐演化成补丁堆。

### 3.4 RawInput 前后台语义补丁风险较高

当前实现还会把 `WM_INPUT` 中的：

- `RIM_INPUTSINK` 改成 `RIM_INPUT`

这是为了让后台客户端把输入当成前台输入处理，但它本质上是在“补丁化修正宿主消息语义”。

该策略短期能提升兼容性，但长期风险包括：

- 宿主程序对 RawInput 前后台语义的内部假设被打破；
- 一旦宿主在 RawInput 路径里自己维护设备状态，就更容易出现通道间残留；
- 这类补丁必须留在“适配器边界”，不能继续扩散到主状态模型中。

---

## 4. 官方文档约束与架构含义

以下官方文档不是为了证明“现在代码不能用”，而是为了明确：**当前多路径强改方案不适合作为长期稳定架构**。

### 4.1 `GetKeyboardState` 与 `GetAsyncKeyState` 语义不同

- `GetKeyboardState` 文档：
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeyboardstate
- `GetAsyncKeyState` 文档：
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate

结论：

- `GetKeyboardState` 与线程消息处理相关；
- `GetAsyncKeyState` 是异步查询语义；
- 不能把两者当作“同一份状态的两个出口”，更不能长期靠互相 claim 来收敛。

### 4.2 Raw Input 是独立输入模型

- `RAWKEYBOARD` 文档：
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawkeyboard
- `WM_INPUT` 文档：
  https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input
- Raw Input 概述：
  https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input

结论：

- RawInput 是事件流模型，不应被当成长期状态缓存模型；
- `vkey=0x00 / MakeCode=0` 这类 synthetic neutral 事件不应作为主策略；
- RawInput 更适合只投影**真实 transition**，而不是承担“维持长按存在感”的职责。

### 4.3 `RegisterRawInputDevices` 不适合库层随意主导

- `RegisterRawInputDevices` 文档：
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices

结论：

- RawInput 注册对宿主进程是全局性影响；
- 文档明确不鼓励 library 随意接管；
- 因此当前 RawInput 补丁必须被收敛到最小适配层，不能继续扩成状态机中心。

### 4.4 DirectInput 不应继续拥有独立主逻辑

DirectInput 对当前老客户端仍可能有兼容价值，但在本项目中：

- 它只能作为**适配器输出层**；
- 不能继续拥有独立决策、独立收敛、独立补偿窗口；
- 否则会把输入状态再复制一份，持续放大不一致风险。

---

## 5. 重构目标

### 5.1 必须达到的目标

1. 消除“方向键松开后持续移动”的主要根因；
2. 保留“共享内存 + 被注入端执行”的总架构；
3. 保留现有 Hook 壳与宿主兼容能力，但让其从“主逻辑”退回“适配器边界”；
4. 为后续 Rust 收口建立明确、稳定的边界。

### 5.2 非目标

本次不追求：

- 一次性重写全部 `SyncMod.cpp`；
- 一次性移除全部 C++ Hook 壳；
- 一次性改协议或推翻共享内存布局；
- 以“减少代码行数”为目标的激进删改。

---

## 6. 目标架构

### 6.1 单一 authoritative logical key state

后续方向键与普通键都统一收敛到一份逻辑状态：

- `logicalDesiredState[256]`
- `logicalEdgeCounter[256]`
- `logicalActiveProfile`
- `logicalFlags`（alive/paused/clear/bypass 等）

这份状态是唯一可被视为“是否应该 down/up”的真值来源。

### 6.2 输入路径观测层

新增路径观测层，负责回答：

- 当前宿主更依赖哪条输入链；
- 最近 1~3 秒真正活跃的输入路径是什么；
- 当前是否处于 RawInput 主导、Win32 查询主导、DirectInput 主导或混合期。

观测层只输出事实，不直接改键态。

### 6.3 适配器层

#### Win32 适配器

职责：

- 回答 `GetAsyncKeyState` / `GetKeyboardState`；
- 只根据 `logicalDesiredState` 投影查询结果；
- 不再拥有独立 claim 主导权。

#### RawInput 适配器

职责：

- 只在逻辑边沿变化时输出合法的 MAKE/BREAK；
- 不再制造 `neutral raw`；
- 不再用 synthetic repeat 去“维持长按存在感”。

#### DirectInput 适配器

职责：

- 只按逻辑态投影设备状态；
- 不再自己维护单独的释放收敛策略；
- 不再作为方向键残留问题的额外状态中心。

### 6.4 诊断事件总线

当前日志体系继续保留，但要升级为围绕“逻辑态—适配器—观测”的诊断模型：

- `logical_state_changed`
- `adapter_projected`
- `adapter_drift_detected`
- `input_path_observed`
- `fallback_clear_applied`

后续所有“卡方向”定位应优先看这些事件，而不是只靠 `orphan_up / missing_keyup` 侧面判断。

---

## 7. 关键设计决策

### 7.1 删除 `neutral raw`

明确决策：

- 不再允许方向键路径输出 `vkey=0x00` synthetic RawInput；
- 方向键长按期间，如果没有新的逻辑 transition，则 RawInput 适配器**不输出占位事件**；
- “吞重复”不再通过伪事件实现，而是通过**不重复投影**实现。

### 7.2 方向键只输出真实 transition

明确决策：

- 方向键 `down -> up`、`up -> down` 才允许投影到 RawInput；
- 不再把宿主原始重复包逐个改写为 synthetic 状态保持包；
- 方向键的“存在感”由逻辑态与其他查询型接口保障，而不是由 RawInput 重复包保障。

### 7.3 双 claim 退役为“已输出态”

当前：

- `rawClaimed`
- `win32Claimed`

后续改为：

- `rawProjectedState`
- `win32ProjectedState`
- `diProjectedState`

含义变化：

- 不再表示“谁拥有真值”；
- 只表示“该适配器上一次已经投影出的状态”；
- 真值只来自 `logicalDesiredState`。

### 7.4 观测驱动而非永久全通道强改

明确决策：

- 不立即删除现有多路径 Hook；
- 但要逐步从“长期强改全部路径”迁移到“按观测结果决定主干预通道，其余通道降级为兼容投影”；
- 对于长时间完全未被宿主读取的通道，应允许降低干预频率和侵入性。

### 7.5 Rust 负责决策，C++ 负责边界

明确决策：

- 方向对冲、释放收敛、逻辑期望态、路径决策优先下沉到 `game_payload_core`；
- `SyncMod.cpp` 保留 Hook 安装、协议桥接、宿主兼容与适配器外壳；
- 不再继续在 C++ 层堆积复杂的方向键状态仲裁逻辑。

---

## 8. 分阶段实施

### Phase 0：冻结基线与保留现状日志

目标：

- 让后续每一步都可回退到 `baseline/sync-usable-20260402`；
- 保留当前日志结构，作为对照样本。

输出：

- 基线分支与提交；
- 本文档；
- 现有日志文件保留用于回归比较。

### Phase 1：短期止血

目标：

- 先移除当前最明显的高风险机制；
- 在不大改外部接口的前提下，减少持续移动概率。

必须实施：

1. 删除 `RawInputMappingState.h` 中返回 `vKey=0` 的路径；
2. `TryPickMappingRawKeyEvent(...)` 仅返回真实目标键 press/release；
3. 方向键长按时不再输出 synthetic repeat raw down；
4. RawInput 只在逻辑边沿变化时投影 transition；
5. 增加一条显式日志：`raw_neutral_suppressed`，用于确认旧机制已被移除。

验收：

- 日志中不再出现 `vkey=0x00` synthetic RawInput；
- 高频方向键时持续移动概率显著下降；
- 没有新增明显的吞键问题。

### Phase 2：状态模型重构

目标：

- 从“通道 claim 驱动”迁到“逻辑态驱动”。

必须实施：

1. 建立统一 `logicalDesiredState`；
2. 把 `rawClaimed / win32Claimed` 从主决策链中移除；
3. 每个适配器只持有 `lastProjectedState`；
4. `Clear / Paused / Alive / Bypass` 统一作用于逻辑态，而不是通道态分别处理。

验收：

- 不再出现长时间 `DirClaims: Raw=1 Win32=0` 式漂移；
- 日志能直接解释逻辑态与适配器投影的差异。

### Phase 3：适配器分层

目标：

- 把当前 `SyncMod.cpp` 的巨型分支逻辑拆成稳定边界。

必须实施：

1. 提炼 Win32 / RawInput / DirectInput 三个适配器；
2. 统一输入：`logicalDesiredState + runtime decision + path observation`；
3. 统一输出：`adapter projection result + diagnostics`；
4. `WM_INPUT / RIM_INPUTSINK` 补丁逻辑只保留在 RawInput 适配器边界，不再散落进主状态机。

验收：

- `SyncMod.cpp` 主流程以“读快照 -> 算逻辑态 -> 按适配器投影”为主；
- 通道特定兼容细节不会再直接修改主逻辑状态。

### Phase 4：观测驱动策略与 Rust 收口

目标：

- 完成“决策 Rust 化、边界 C++ 化”。

必须实施：

1. 把方向对冲、释放收敛、适配器决策继续收口到 Rust；
2. 新增路径观测摘要与策略选择；
3. 支持按宿主真实输入读取行为启停/降级某些干预路径。

验收：

- C++ 层主要剩余 Hook 外壳与 FFI 桥；
- Rust 成为逻辑决策中心；
- 新日志能回答“为什么当前选择这个适配策略”。

---

## 9. 代码边界变化

### 9.1 `SyncMod.cpp` 保留职责

重构后 `SyncMod.cpp` 应保留：

- Hook 安装/卸载；
- Win32 / RawInput / DirectInput trampoline；
- 共享快照读取与 FFI 桥接；
- 适配器层调用；
- 宿主兼容补丁。

### 9.2 应迁入 Rust 的职责

应优先迁入 `game_payload_core`：

- 逻辑键态模型；
- 方向键对冲裁决；
- 释放收敛窗口；
- 逻辑态到适配器决策的投影规则；
- 诊断事件模型。

### 9.3 内部状态结构调整目标

建议逐步形成以下内部结构：

- `LogicalInputState`
- `AdapterProjectedState`
- `InputPathObservation`
- `SyncRuntimeContext`
- `DiagnosticEventBuffer`

这些结构第一阶段可先在 C++ 侧建立雏形，后续逐步替换为 Rust 产出的 FFI 数据结构。

---

## 10. 验证与验收

### 10.1 自动化验证

至少保留并逐步扩展以下测试方向：

1. 方向键状态结构测试；
2. Mapping 事件选择测试；
3. 方向键对冲裁决测试；
4. 释放收敛窗口测试；
5. 逻辑态到适配器输出的表驱动测试。

### 10.2 实机场景

每个阶段都必须回归以下场景：

1. 左右方向键高速点按 10 秒；
2. 上下方向键高速切换 10 秒；
3. 方向键长按后立即松开；
4. 切换前后台时继续同步；
5. 同时存在技能键连发与方向键操作。

### 10.3 日志验收标准

重构后的验收标准必须至少满足：

- 不再出现 `vkey=0x00` synthetic RawInput；
- 不再出现长时间 Raw/Win32 漂移；
- `orphan_up / missing_keyup` 若出现，能被新的诊断事件直接解释；
- 任何持续移动问题都能定位到“逻辑态错误”或“适配器投影错误”二者之一，而不是模糊地归咎于多通道残留。

---

## 11. 回退策略

### 11.1 基线回退

任何阶段性失败，直接回退到：

- 分支：`baseline/sync-usable-20260402`
- 提交：`5cbdc08`

### 11.2 阶段内回退

后续每个阶段都必须具备：

- 单独开关或编译期开关；
- 日志可观测；
- 明确禁用后回到上阶段行为。

### 11.3 不允许的回退方式

不允许继续使用以下方式作为长期方案：

- 增加更多 `neutral raw` 变种；
- 为某个通道继续叠更多“额外 release 脉冲”；
- 在 `claim` 漂移问题上继续做局部补丁而不改状态模型。

---

## 12. 执行顺序建议

后续实际开发时，严格按以下顺序推进：

1. 先做 **Phase 1：移除 neutral raw**；
2. 再做 **Phase 2：建立单一逻辑态**；
3. 然后做 **Phase 3：适配器分层**；
4. 最后做 **Phase 4：Rust 收口与观测驱动策略**。

原因是：

- Phase 1 能最快降低当前实机风险；
- Phase 2 才能真正消除漂移根因；
- Phase 3/4 是把方案从“能修”变成“可维护”。

---

## 13. 结论

当前 `Sync` 输入执行层的问题已经不再适合继续通过“加一条 release 兜底”来解决。

必须承认并固化以下事实：

1. 当前版本可以作为回退基线，但不是最终稳定方案；
2. `neutral raw` 与双 claim 模型是当前最明确的结构性风险；
3. 正确方向不是继续补丁，而是建立**单一逻辑状态 + 分层输入适配器 + 路径观测驱动**；
4. 这条路线与既有 Rust 重构总计划一致，应作为其在 Sync 输入执行层的直接落地方案。
