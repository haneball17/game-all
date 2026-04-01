# game-all Rust 完整重构落地计划

## 1. 文档定位

本文档用于指导 `game-all` 的原生核心从 **C++ 主导** 迁移到 **Rust 主导**，并作为后续分阶段实施、评审、验收的统一依据。

本计划明确继承并落实以下两份既有结论：

- `docs/DNF2012台湾服与多客户端同步重设计调研.md`
- `docs/实机问题分析—同步延迟与方向键卡住.md`

这意味着本次 Rust 重构不是“为了 Rust 而 Rust”，而是服务于以下目标：

1. 保留现有项目在老 DNF 客户端上的兼容性前提；
2. 用更稳的内存管理、状态管理和测试能力重做核心逻辑；
3. 优先解决同步延迟、偶发不同步、方向键卡住等实机问题；
4. 保证整个迁移过程可分阶段、可回退、可诊断。

---

## 2. 重构总目标

### 2.1 总体目标

在 **不推翻现有“共享内存 + 被注入端执行”总架构** 的前提下，完成：

- `Payload + Injector` 原生核心的 Rust 化；
- 控制端采集/状态机/快照发布逻辑的内核 Rust 化；
- 多输入路径执行层从“补丁堆积”演进为“分层输入适配器”；
- 建立统一的诊断、观测与回退机制。

### 2.2 非目标

以下内容不作为本次重构的默认目标：

- 不重写 C# WPF GUI；
- 不立即推翻现有共享内存协议；
- 不第一阶段直接重写 MinHook / trampoline / 全部 Hook 安装壳；
- 不退化成窗口消息转发主导方案；
- 不为了追求“全 Rust”牺牲稳定性与可调试性。

---

## 3. 必须继承的架构结论

### 3.1 来自 DNF2012 调研文档的强约束

以下内容作为强约束，不允许在实施中偏离：

1. **保留“共享内存 + 被注入端执行”总架构**
   - 不改成纯窗口消息转发；
   - 不改成无注入的纯外部同步主方案。

2. **保留当前控制面抽象**
   - `ActivePid`
   - `Heartbeat`
   - `ProfileId / ProfileMode`
   - `Flags`
   - `KeyboardState / EdgeCounter / TargetMask / BlockMask`

3. **把旧 DNF 客户端视作多输入路径并存的对象**
   - `GetAsyncKeyState`
   - `GetKeyboardState`
   - `RawInput`
   - `DirectInput`
   - 焦点/前后台语义

4. **执行端重构方向必须是“分层输入适配器”**
   - 不是长期所有路径一起硬改；
   - 而是统一状态、统一缓存、统一决策，再由路径适配器执行。

### 3.2 来自实机问题分析文档的强约束

以下问题是优先级最高的落地目标：

1. 同步轻微延迟、偶发不同步；
2. 方向键高速操作后从端卡方向；
3. 多路径缓存不一致；
4. Clear / Paused / ActivePid / Alive 没有被统一纳入同一状态机；
5. 缺少足够强的诊断能力，问题难以复盘。

---

## 4. 最终目标架构

### 4.1 组件分层

#### A. Rust 核心层

建议形成以下 crate 体系：

- `game_core_protocols`
  - 协议镜像、版本、尺寸、布局测试
- `game_core_common`
  - 配置、路径、日志、错误码、时间、公共工具
- `game_injector_core`
  - Injector 配置、等待流程、成功判定、重试状态机
- `game_payload_core`
  - Payload 共享内存、状态机、同步逻辑、诊断核心
- `game_input_adapters`
  - 执行端输入适配层
- `game_input_observer`
  - 输入路径观测与自适应策略
- `game_control_core`
  - 控制端采集与快照发布内核

#### B. C++ 边界壳

尽量变薄，只保留高风险或平台边界功能：

- `Payload`
  - `DllMain`
  - MinHook 初始化与安装/卸载
  - Win32 / RawInput / DirectInput hook trampoline
- `Injector`
  - EXE 入口壳
  - 必要时保留极薄 Win32 启动桥接

#### C. C# GUI 层

保持现状，不做架构性重写：

- `MasterGUI`
- `Helper GUI`
- `Sync GUI`

GUI 只在需要时增加新的诊断状态展示，不承载核心同步逻辑。

---

## 5. 共享协议与兼容策略

### 5.1 第一阶段到第三阶段保持不变

以下内容作为兼容承诺：

- `HelperStatusV5`
- `HelperControlV4`
- `SharedKeyboardStateV2`
- 共享内存名称
- 协议版本与结构尺寸
- `artifacts/run` 运行目录布局
- DLL/EXE 产物命名
- 现有 GUI 读取方式

### 5.2 Rust 内部必须新增的抽象

虽然外部协议不变，但 Rust 内部必须引入以下统一模型：

- `InputSnapshot`
- `InputConvergenceState`
- `InputCacheArbiter`
- `InputAdapterKind`
- `InputAdapterDecision`
- `DiagnosticEvent`
- `DiagnosticSnapshot`

这些类型优先作为 Rust 内部结构存在，不在第一阶段直接暴露为共享协议。

---

## 6. Rust 内部接口规划

### 6.1 Injector Core C ABI

需要逐步稳定为：

- `injector_core_default_view()`
- `injector_core_helper_status_version()`
- `injector_core_helper_status_size()`
- `injector_core_load_config(...)`
- `injector_core_build_runtime_plan(...)`
- `injector_core_run(...)`

### 6.2 Payload Core C ABI

需要逐步稳定为：

- `payload_core_start(...)`
- `payload_core_stop()`
- `payload_core_on_hook_observation(...)`
- `payload_core_query_keyboard_view(...)`
- `payload_core_collect_diagnostics(...)`

### 6.3 FFI 约束

跨语言边界遵循以下规则：

- 只传递 POD struct、错误码、指针+长度、回调表；
- 不跨边界传 Rust `String` / `Vec` / `trait object`；
- 不直接把复杂 C++ 类型暴露给 Rust；
- 协议结构优先按字节视图处理，避免 packed 字段未对齐 UB 风险。

---

## 7. 分阶段实施计划

### 阶段 0：基础层收口

### 目标
把现有 Rust 基础层正式收口为可持续演进的基线。

### 内容
- 固化 `game_core_protocols` 为 Rust 侧协议镜像；
- 固化 `game_core_common` 为基础设施；
- 固化 `game_injector_core` 的配置模型；
- 固化 `game_payload_core` 的方向键释放收敛状态机；
- 补齐 crate 说明、FFI 边界说明、crate 关系说明。

### 验收
- Rust 侧 `cargo test` 通过；
- 协议尺寸/偏移测试齐全；
- 方向键释放收敛逻辑具备自动化测试；
- 现有代码运行逻辑不被替换。

---

### 阶段 1：Injector Rust 化

### 目标
优先迁移最线性的模块，验证 Rust 在现有工程里的实用性。

### 内容
1. 用 `game_injector_core` 接管：
   - 配置解析
   - 路径归一化
   - 默认配置生成
   - 协议契约常量
2. 继续接管：
   - 等待目标窗口/进程状态机
   - successfile 检测
   - 共享内存心跳兜底
   - 超时/重试逻辑
3. C++ `Injector` 只保留：
   - EXE 入口
   - 极薄 Win32 调用桥

### 验收
- 新旧配置行为一致；
- successfile/心跳兜底行为一致；
- 超时与重试策略一致；
- 日志与错误信息更可诊断。

---

### 阶段 2：Payload 核心状态 Rust 化

### 目标
把最适合 Rust 管理、也是当前问题最集中的状态层迁出 C++。

### 内容
Rust 接管：

- 配置加载
- successfile 生命周期
- Helper/Sync 启停生命周期
- 共享内存创建/打开/读写
- 心跳线程
- 快照对象
- `Seq` 一致性逻辑
- `Alive / Paused / Clear / ActivePid` 统一状态模型
- 方向键释放收敛
- 快照缓存仲裁

C++ 保留：
- `DllMain`
- Hook 安装/卸载
- MinHook / RawInput / DirectInput trampoline

### 验收
- GUI 完全无感知；
- 共享协议不变；
- 状态层逻辑由 Rust 主导；
- 方向键卡住问题有统一治理入口。

---

### 阶段 3：执行端输入适配层 Rust 化

### 目标
落实“分层输入适配器”架构。

### 内容
新增并接入：

- `Win32StateAdapter`
- `RawInputAdapter`
- `DirectInputAdapter`
- `FocusAdapter`

统一要求：

- 各路径不允许自行解释共享快照；
- 各路径不允许自行决定缓存是否复用；
- 各路径不允许自行维护孤立的 Clear/Paused/Alive 逻辑；
- 所有路径必须由 Rust 统一状态机与缓存仲裁器给出决策。

### 验收
- 各输入路径对方向键释放表现一致；
- 各路径不再拥有独立的旧快照缓存策略；
- RawInput / DirectInput / Win32 状态查询共用同一份核心决策结果。

---

### 阶段 4：控制端采集内核 Rust 化

### 目标
解决当前控制端采集线程模型偏弱、心跳补偿偏慢的问题。

### 内容
1. 保留 C# GUI 展示与配置界面；
2. 将控制端采集/状态机/快照发布迁入 Rust：
   - 专用采集线程
   - 按键状态跟踪
   - 热键处理
   - 自动暂停/恢复
   - 快照发布层
3. 分层结构固定为：
   - 输入采集层
   - 状态机层
   - 快照发布层

### 验收
- 控制端不再依附 UI 线程采集；
- 高频方向键场景的延迟明显收敛；
- 心跳补偿和事件驱动不再混乱耦合。

---

### 阶段 5：路径观测、自适应策略与诊断体系

### 目标
把“经验性兼容”变成“可观测兼容”。

### 内容
新增：

- `game_input_observer`
- 统一诊断事件模型
- 统一诊断快照模型
- 基于路径观测的策略选择逻辑

路径观测至少要能回答：

- 当前客户端更依赖哪条输入路径；
- 哪条路径发生了边沿丢失；
- 为什么延迟；
- 为什么卡方向；
- 当前是否命中 Clear / Focus spoof / Force release。

### 验收
- 实机问题可复盘；
- 可以基于观测选择最小干预路径；
- 不再长期盲目对所有路径一起硬改。

---

## 8. 实机问题导向的专项落地要求

### 8.1 针对“延迟 / 偶发不同步”

Rust 重构中必须显式落实：

1. 控制端采集线程与 UI 解耦；
2. 快照发布与补偿逻辑分层；
3. 快照缓存仲裁统一化；
4. 执行端不同输入路径共享同一快照刷新决策；
5. 具备记录“最近一次发布/读取/命中缓存”的诊断事件。

### 8.2 针对“方向键卡住”

Rust 重构中必须显式落实：

1. 方向键释放收敛状态机；
2. 方向键释放窗口内禁止复用旧快照；
3. Clear 后显式清理内部残留态；
4. RawInput / DirectInput / Win32 三类路径统一服从强制抬起决策；
5. 焦点相关路径必须被纳入诊断模型。

---

## 9. 自动化测试计划

### 9.1 协议测试
- `HelperStatusV5` 尺寸/偏移测试
- `HelperControlV4` 尺寸/偏移测试
- `SharedKeyboardStateV2` 尺寸/偏移测试
- 映射名称规则测试

### 9.2 Injector 测试
- 默认配置文本测试
- INI 解析测试
- 路径归一化测试
- successfile 判定测试
- 心跳兜底测试
- 超时/重试测试

### 9.3 Payload 状态机测试
- `Clear / Paused / Alive / ActivePid` 组合测试
- 快照 `Seq` 一致性测试
- 方向键长按/快速点按/快速切换测试
- 强制抬起掩码生成测试
- 快照缓存仲裁测试

### 9.4 输入适配测试
- Win32 状态查询测试
- RawInput 收敛测试
- DirectInput 收敛测试
- 焦点语义切换测试
- Mapping / 非 Mapping 模式方向键释放测试

### 9.5 诊断测试
- 方向键 Down/Up 事件记录测试
- 快照发布时间记录测试
- 最近一次 RawInput / DirectInput 决策记录测试
- Clear 后残留态清理记录测试

---

## 10. 实机验证计划

必须按以下顺序验证：

1. 单实例基础运行；
2. 双实例基础同步；
3. 高频方向键点按；
4. 长按方向键后快速松手；
5. 左右快速切换后停止；
6. Alt-Tab 与前后台切换；
7. 自动暂停/恢复；
8. 心跳超时；
9. 长时间运行 soak test；
10. 诊断日志与复盘输出检查。

---

## 11. 迁移与回退策略

### 11.1 构建层回退
每个阶段都必须保留：

- 旧 C++ 实现；
- 新 Rust 实现；
- 可明确切换的构建或链接入口。

### 11.2 运行层回退
若某阶段已经把 Rust 接入主链路，则必须同时具备：

- 切回旧 C++ 核心的能力；
- 不破坏现有配置与运行目录的能力；
- 问题复现时可快速二分定位到新旧实现差异。

### 11.3 切换顺序
固定顺序如下，不允许跳步：

1. 协议/基础设施
2. Injector
3. Payload 状态层
4. 输入适配层
5. 控制端采集层
6. 路径观测与自适应

---

## 12. 文档与实施要求

### 12.1 每阶段都要新增实施记录
每一阶段落地后，必须在 `docs/` 下新增或更新实施记录，至少包含：

- 改动范围
- 新增接口
- 保留的旧逻辑
- 已验证项
- 未验证项
- 回退方式

### 12.2 项目合并文档同步更新
每个阶段落地后，需要在 `docs/项目合并文档.md` 中补一条阶段记录，确保演进历史可追溯。

---

## 13. 验收标准

### 13.1 阶段性验收
- 阶段 1：Injector 核心逻辑可由 Rust 驱动，行为不退化；
- 阶段 2：Payload 状态与共享内存逻辑可由 Rust 主导，GUI 无感知；
- 阶段 3：多输入路径共享统一状态机与缓存仲裁；
- 阶段 4：控制端采集不再依附 UI 线程；
- 阶段 5：实机问题可复盘，可基于观测优化路径策略。

### 13.2 最终验收
- 不破坏现有协议与 GUI 兼容性；
- 不增加同步延迟；
- 不增加方向键卡住概率；
- 能定位延迟/不同步/卡方向来源；
- 可回退；
- Rust 侧关键状态机具备自动化测试覆盖。

---

## 14. 默认假设与决策

- 默认只重构 `Payload + Injector + 控制端内核`；
- 默认不重写 GUI；
- 默认保留共享内存协议直至新执行层稳定；
- 默认保留 C++ 边界壳处理高风险 Win32/Hook 安装；
- 默认优先解决“方向键卡住”和“快照缓存不一致”；
- 默认两份既有文档的结论视为本计划的上位约束；
- 默认不为追求形式上的“全 Rust”而牺牲稳定性。
