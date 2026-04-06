# game-all Rust 后续全面重构计划

## 1. 文档定位

本文档基于当前仓库真实状态，对 `game-all` 后续仍需要进行 Rust 重构的部分做一次**全面评估、优先级排序与实施细化**。

它与现有文档的关系如下：

- [docs/Rust完整重构落地计划.md](./Rust完整重构落地计划.md)
  - 作用：定义总方向、总体原则、crate 蓝图与阶段目标。
- [docs/Rust核心重构实施记录.md](./Rust核心重构实施记录.md)
  - 作用：记录已经完成的 Rust 接入批次与验证结果。
- [docs/Sync输入架构重构落地方案.md](./Sync输入架构重构落地方案.md)
  - 作用：聚焦 `Sync` 输入执行层的专项重构。
- **本文档**
  - 作用：基于当前仓库代码现状，明确“哪些部分仍然需要 Rust 化、为什么、怎么拆、先做什么、哪些不该做”。

本文档不是为了追求“全 Rust”，而是为了把最适合 Rust 管理的状态机、协议、IPC、决策层继续收口到 Rust，同时把不适合迁移的 Win32/x86 边界保留在 C++/C# 层。

---

## 2. 当前仓库现状

### 2.1 已经完成的 Rust 化部分

当前 `rust/` workspace 已存在并接入以下 crate：

- `game_core_protocols`
- `game_core_common`
- `game_injector_core`
- `game_payload_core`

已落地内容包括：

1. 协议镜像、尺寸/偏移测试；
2. 通用基础设施（INI、路径、兼容值解析）；
3. Injector 默认配置、协议契约与最小 FFI；
4. Payload 的运行时决策、方向键释放收敛、单键决策、逻辑键态决策、通道发射决策、路径级决策；
5. 部分 Windows 工程的 Rust 静态库接入与构建验证。

### 2.2 当前仍是重逻辑中心的代码

按体量与职责看，当前仍然最重的几个逻辑中心是：

- [Payload/modules/sync/SyncMod.cpp](../Payload/modules/sync/SyncMod.cpp)
  - 约 4835 行
- [Payload/modules/helper/HelperMod.cpp](../Payload/modules/helper/HelperMod.cpp)
  - 约 3337 行
- [Injector/main.cpp](../Injector/main.cpp)
  - 约 1171 行
- [GUI/Modules/Sync/Core/SyncController.cs](../GUI/Modules/Sync/Core/SyncController.cs)
  - 约 774 行

这四处也是后续 Rust 重构的主战场。

### 2.3 当前不建议作为 Rust 主战场的部分

以下部分目前不应作为后续 Rust 重构主目标：

- WPF 界面层：
  - `GameMasterGUI`
  - `GameHelperGUI`
  - `DNFSyncBox`
- `DllMain`、MinHook 初始化、vtbl/trampoline、裸地址 patch 壳；
- 直接依赖 x86 ABI、目标进程私有结构偏移、汇编/调用约定的代码。

原因是这些部分的主要风险不在“状态一致性/内存安全/可测试性”，而在平台边界与 ABI 脆弱性。它们更适合做薄边界壳，而不是迁移成新的 Rust 复杂入口。

---

## 3. 重构判断原则

后续是否要迁入 Rust，统一按以下标准判断：

### 3.1 应优先迁入 Rust 的特征

同时满足以下一项或多项时，应优先 Rust 化：

1. **强状态机特征**
   - 有明确状态转移、暂停/恢复、清理、回退、超时、重试等语义；
2. **多路径一致性问题**
   - 同一语义在 Win32/RawInput/DirectInput/共享内存/GUI 间重复实现；
3. **协议与布局敏感**
   - 需要跨语言共享结构体、版本号、大小、偏移一致；
4. **适合表驱动测试**
   - 核心逻辑可以脱离宿主进程做纯逻辑测试；
5. **当前 C++/C# 实现已经形成巨型函数或跨层漂移**
   - 例如多个 if/else 分支同时管理路径、状态、日志、回退。

### 3.2 不应优先迁入 Rust 的特征

满足以下情况时，不应作为当前优先级：

1. **主要价值是平台边界壳**
   - 例如 `DllMain`、hook trampolines、裸 Win32 callback；
2. **主要风险在目标游戏私有 ABI**
   - 例如内存 patch、特定地址调用、汇编桥；
3. **本质是 UI 交互**
   - 例如 WPF View / ViewModel / 用户输入响应。

---

## 4. 官方文档与最佳实践约束

以下官方文档直接影响本项目的重构边界与设计：

### 4.1 DLL 入口必须最小化

Microsoft 对 `DllMain` 的最佳实践非常明确：

- 不应在 `DllMain` 中做复杂初始化；
- 不应在 `DllMain` 中做可能触发 loader lock 问题的操作；
- 不应把大量状态机、线程初始化、依赖调用堆进 `DllMain`。

因此：

- `DllMain` 更应该继续保留在 C++ 边界层；
- 真正的运行时状态机、配置、共享内存、诊断、决策层应迁入 Rust。

参考：

- https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices

### 4.2 Raw Input 不适合被 library 长期主导

Microsoft 文档对 `RegisterRawInputDevices` 有明确限制：

- 每个进程对每类 raw input 设备只能有一个目标窗口；
- 注册行为对宿主进程是全局性影响；
- 文档明确提示不应由 library 随意使用。

因此：

- RawInput 注册/消息绑定逻辑应保留在边界壳；
- 但对 RawInput 的**逻辑解释、路径裁决、通道投影**应迁入 Rust。

参考：

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices
- https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinput
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawkeyboard

### 4.3 `GetAsyncKeyState` / `GetKeyboardState` / `GetKeyState` 语义不同

Microsoft 文档说明：

- `GetAsyncKeyState` 是异步查询语义；
- `GetKeyboardState` 与线程消息队列相关；
- `GetKeyState` 又有不同的消息上下文语义。

因此：

- 不能继续把这些 API 当成“同一份状态的多个出口”各自硬补；
- 必须由统一核心状态机生成逻辑期望态，再由不同适配器投影。

参考：

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeyboardstate
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeystate

### 4.4 .NET 原生互操作强调“边界薄、布局准”

.NET 官方原生互操作最佳实践强调：

- interop 签名必须准确；
- 跨边界优先使用 blittable/POD 结构；
- 避免把复杂对象图直接跨语言传递；
- 内存映射、缓冲区和指针边界要清晰。

这与本项目后续方向完全一致：

- Rust 负责状态机、协议、诊断、配置、决策；
- C# 和 C++ 只保留薄边界；
- FFI 统一传递 POD struct、error code、ptr+len。

参考：

- https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices
- https://learn.microsoft.com/en-us/dotnet/standard/io/memory-mapped-files

### 4.5 Rust 官方 FFI 约束

Rust 官方 FFI 指南与 Nomicon 的要求也支持本项目当前路线：

- 不应在 C ABI 中传播复杂 Rust 类型；
- panic/unwind 不应穿越不安全 FFI 边界；
- 需要用清晰的 ABI struct 和错误码保护跨语言交互。

参考：

- https://doc.rust-lang.org/nomicon/ffi.html

---

## 5. 模块级评估结论

本节给出逐模块结论：哪些必须 Rust 化、哪些建议 Rust 化、哪些不建议迁移。

### 5.1 Injector：必须继续 Rust 化

#### 当前情况

[Injector/main.cpp](../Injector/main.cpp) 目前仍承担：

- 默认配置与配置解析回退；
- 路径归一化；
- successfile 判定；
- 共享内存心跳兜底；
- 窗口等待与重试状态机；
- 注入主流程协调；
- 日志输出与错误路径。

其中，只有最薄的一层 Win32 注入动作与进程/窗口边界真正必须留在 C++。

#### 结论

`Injector` 是后续最应该继续 Rust 化的部分之一。

#### 应迁入 Rust 的职责

建议新增/扩展 `game_injector_core` 承担：

1. 完整配置解析与默认值决策；
2. 运行计划构建：
   - 目标进程名
   - DLL 路径
   - 输出目录
   - 各类超时与轮询间隔
3. successfile 检测策略；
4. 共享内存心跳兜底策略；
5. 等待目标窗口/进程状态机；
6. 重试/退避/超时策略；
7. 多任务注入并发调度策略；
8. 统一错误码、诊断事件、日志摘要。

#### 保留在 C++ 的职责

继续保留：

1. EXE 入口；
2. Win32 进程句柄/线程句柄/窗口句柄获取；
3. 实际注入动作；
4. 极薄的 FFI 调用桥。

#### 推荐优先级

`P0`

原因：

- 它的业务边界清晰；
- 状态机线性；
- 纯逻辑测试价值高；
- 对现有稳定性帮助直接；
- 重构风险明显低于 `HelperMod`。

---

### 5.2 Payload / Sync：最高优先级继续 Rust 化

#### 当前情况

[Payload/modules/sync/SyncMod.cpp](../Payload/modules/sync/SyncMod.cpp) 仍然过大，虽然已有部分 Rust 决策接入，但仍集中保留了：

- 共享快照读取；
- RawInput / Win32 / DirectInput 适配；
- 路径决策桥接；
- projected state 管理；
- 方向键/逻辑边沿处理；
- pause/clear/neutralize 路径；
- 诊断日志与观测输出；
- 各类 fallback/bypass 逻辑。

#### 结论

`SyncMod.cpp` 仍是后续 Rust 化的**第一大核心目标**。

#### 已迁入 Rust 的部分

`game_payload_core` 已具备：

1. `RuntimeDecision`
2. `KeyDecision`
3. `LogicalKeyDecision`
4. `ChannelEmitDecision`
5. `PathDecision`
6. `DirectionConvergenceState`
7. `SnapshotCachePolicy`

这说明核心决策模型已经起步，但还没有完全接管状态层与适配器层。

#### 仍应迁入 Rust 的职责

建议优先继续迁入：

1. 统一逻辑键态模型
   - `logicalDesiredState`
   - `logicalEdgeCounter`
   - `logicalFlags`
2. 适配器投影状态
   - `rawProjectedState`
   - `win32ProjectedState`
   - `diProjectedState`
3. 适配器漂移检测与自恢复逻辑；
4. 路径观测与通道优先级策略；
5. `Clear / Paused / Alive / Bypass` 的统一状态机；
6. 诊断事件模型与事件缓冲；
7. 逻辑态到各输入路径的表驱动投影规则；
8. `WM_INPUT` / RawInput 前后台语义策略决策。

#### 保留在 C++ 的职责

继续保留：

1. Hook/trampoline 安装；
2. `DllMain`；
3. 具体 Win32 callback 入口；
4. 结构体搬运与 FFI 参数封装；
5. 极薄的 RawInput/DirectInput 数据提取与结果回填。

#### 推荐优先级

`P0`

原因：

- 它正对当前最重要的实机问题；
- 多路径一致性问题最严重；
- 当前代码体量最大；
- 也是现有 Rust 方案受益最大的地方。

---

### 5.3 控制端采集内核：必须新增 Rust crate

#### 当前情况

[GUI/Modules/Sync/Core/SyncController.cs](../GUI/Modules/Sync/Core/SyncController.cs) 目前不仅是 UI 控制器，还同时负责：

- 键态维护；
- edgeCounter 维护；
- auto pause / foreground grace；
- profile 解析结果应用；
- 共享内存写入节流；
- heartbeat；
- clear stuck keys；
- 日志与状态同步。

这已经超过“GUI 控制器”的合理职责。

#### 结论

当前仓库虽然已有计划中的 `game_control_core` 名称，但 workspace 还没有真正创建这个 crate。它应该作为后续 Rust 重构的**新增核心 crate**。

#### 建议新增 crate

- `game_control_core`

#### 应迁入 Rust 的职责

1. 控制端输入采集状态机；
2. 键态数组与 edgeCounter 演进；
3. target/block/mapping profile 归一化；
4. auto pause 与 foreground grace 状态机；
5. 快照生成与 heartbeat 发布策略；
6. clear/neutralize 发布策略；
7. 诊断事件输出；
8. 配置热重载后的“可执行 profile”构建。

#### 保留在 C# 的职责

1. WPF 页面；
2. 热键配置界面；
3. ViewModel；
4. 绑定与用户交互；
5. 纯展示型日志输出。

#### 推荐优先级

`P1`

原因：

- 这部分虽然不是注入端，但直接决定快照质量；
- 现在逻辑和 UI 混杂；
- 重构收益很高；
- 但优先级略低于 `SyncMod.cpp`，因为后者直接对实机卡方向更敏感。

---

### 5.4 Helper 核心：建议分层 Rust 化

#### 当前情况

[Payload/modules/helper/HelperMod.cpp](../Payload/modules/helper/HelperMod.cpp) 当前同时承担：

- 配置与默认值；
- 热键；
- 共享内存状态写入；
- 控制共享内存读取；
- 后台线程；
- 游戏对象状态判断；
- 内存 patch；
- 汇编调用桥；
- 若干功能状态机（透明、吸怪、召唤、全屏技能、倍攻等）。

#### 结论

`HelperMod.cpp` 不适合一次性整体 Rust 化，但非常适合**先分层抽出“协议/配置/共享内存/线程状态机”**。

#### 建议新增 crate

二选一：

1. 新建 `game_helper_core`
2. 或先并入 `game_payload_core` 的 helper 子模块，稳定后再拆 crate

#### 应迁入 Rust 的职责

1. `HelperStatusV5` / `HelperControlV4` 协议镜像与布局测试；
2. 配置解析与默认值；
3. 配置热重载判定；
4. 共享内存读写；
5. 控制命令快照应用状态机；
6. 后台任务调度模型；
7. 诊断事件与状态摘要输出。

#### 保留在 C++ 的职责

1. 具体裸地址读写；
2. patch 写入；
3. 汇编调用/调用约定桥；
4. 对目标进程私有结构和版本高度敏感的代码。

#### 推荐优先级

`P2`

原因：

- 收益高，但拆分难度也高；
- 需要先把“哪些是逻辑核，哪些是游戏私有边界”明确切开；
- 相比 Sync 和 Injector，不适合抢最前面的时序。

---

### 5.5 协议单一来源：建议继续 Rust 主导化

#### 当前情况

项目原则要求 `Shared/Protocols/` 是协议单一来源，但实际上：

- Rust 侧已经有 `game_core_protocols`
- C++/C# 侧仍保留各自镜像
- 目前靠文档 + 手工同步 + 测试收敛

#### 结论

后续应逐步把“协议真源”收口到 Rust 协议镜像与自动校验体系，至少实现“Rust 主镜像 + C++/C# 自动校验”。

#### 应推进的内容

1. 维持 `game_core_protocols` 为协议真源；
2. 对 C++/C# 补充自动 size/offset/version 校验；
3. 需要时生成：
   - 常量头文件
   - C# struct 注释或校验代码
4. 把 `Shared/Protocols/协议说明.md` 调整为“人工可读说明 + 生成/校验入口说明”。

#### 推荐优先级

`P2`

原因：

- 风险不低，但长期收益大；
- 不会立即解决实机输入问题；
- 更适合作为状态机迁移稳定后的收口工作。

---

### 5.6 GUI 展示层：不建议 Rust 化

#### 结论

以下部分默认不纳入 Rust 重构范围：

- `GameMasterGUI`
- `GameHelperGUI`
- `DNFSyncBox` 的 View / ViewModel / UI 交互

#### 原因

1. 当前主要问题不是 UI 框架本身；
2. WPF 生态与当前工程产物形态已稳定；
3. GUI 的价值在配置与展示，不在多路径输入状态一致性；
4. 重写 GUI 为 Rust 不会改善当前核心痛点，反而会显著放大交付风险。

---

## 6. 后续 crate 规划

基于上述评估，建议把 Rust workspace 演进为：

### 6.1 现有 crate（继续保留）

- `game_core_protocols`
- `game_core_common`
- `game_injector_core`
- `game_payload_core`

### 6.2 应新增 crate

#### `game_control_core`

职责：

- 控制端输入采集
- profile 应用
- 快照发布
- pause/clear/heartbeat 状态机

#### `game_input_adapters`

职责：

- 统一描述 Win32 / RawInput / DirectInput 适配器投影逻辑；
- 不负责 hook 安装，只负责“给定逻辑态时该如何投影”。

#### `game_input_observer`

职责：

- 路径观测；
- 最近活跃输入链统计；
- 当前主导输入路径决策；
- 诊断摘要。

#### `game_helper_core`

职责：

- Helper 配置与协议；
- 共享内存控制；
- 后台线程状态机；
- 诊断核心。

---

## 7. 详细实施顺序

### Phase A：完成 Injector Rust 化

#### 目标

把 `Injector/main.cpp` 压缩成薄 Win32 bridge。

#### 内容

1. `game_injector_core` 新增完整配置解析入口；
2. 新增运行计划构建；
3. 新增等待窗口/重试状态机；
4. 新增 successfile 与 heartbeat 复合判定；
5. 新增统一错误码与诊断事件；
6. C++ 只保留：
   - 入口
   - 注入动作
   - Win32 句柄与错误桥接。

#### 验收

1. 配置文件行为与当前一致；
2. successfile/heartbeat 兜底行为一致；
3. 等待/超时/重试行为一致；
4. Rust 单测覆盖状态机；
5. Windows `Injector.vcxproj` 构建与运行验证通过。

---

### Phase B：继续压缩 SyncMod.cpp

#### 目标

把 `SyncMod.cpp` 从“核心状态机 + 适配器 + hook 壳”压缩成“hook 壳 + FFI bridge”。

#### 内容

1. 在 `game_payload_core` 中补齐：
   - `LogicalInputState`
   - `AdapterProjectedState`
   - `InputPathObservation`
   - `DiagnosticEventBuffer`
2. 把 projected state 与 drift 检测迁入 Rust；
3. 把 pause/clear/alive/bypass 的完整状态转移迁入 Rust；
4. 把路径观测与优先级选择迁入 Rust；
5. C++ 中仅保留：
   - 原始 API 入口；
   - 结构体抽取；
   - FFI 调用；
   - 回填结果。

#### 验收

1. `SyncMod.cpp` 主流程明显收缩；
2. Rust 决策成为单一真值来源；
3. 方向键、pause、clear、mapping 路径具备表驱动测试；
4. 现有 `[EDGE]/[EMIT]/[GROUP]/[REPEAT]/[OBS]/[PAUSE]` 日志仍可用；
5. 实机不回归。

---

### Phase C：建立控制端 Rust 内核

#### 目标

新增 `game_control_core`，把 `SyncController.cs` 中的同步内核移出 GUI。

#### 内容

1. 建立控制端状态快照模型；
2. 迁移 profile 应用与键态推进；
3. 迁移 auto pause / foreground grace；
4. 迁移 heartbeat 和共享内存发布；
5. C# 改为：
   - 调用 Rust core
   - 展示状态
   - 配置交互。

#### 验收

1. `SyncController.cs` 明显瘦身；
2. 同步核心逻辑可脱离 WPF 做单测；
3. GUI 仅负责展示与绑定；
4. 配置热重载行为不退化。

---

### Phase D：Helper 核心拆层

#### 目标

把 `HelperMod.cpp` 拆成“Helper Core + 游戏边界壳”。

#### 内容

1. 协议镜像进 Rust；
2. 配置与热更新逻辑进 Rust；
3. 共享内存状态/控制通道进 Rust；
4. 后台任务调度进 Rust；
5. C++ 只保留：
   - patch
   - 裸地址读写
   - 调用约定桥
   - 极薄功能入口。

#### 验收

1. 配置/共享内存/线程状态机有单测；
2. C++ helper 代码体量显著下降；
3. 不影响现有功能开关与热键行为；
4. 功能日志与状态共享内存保持兼容。

---

### Phase E：协议主源与生成/校验体系

#### 目标

把协议主源收口为 Rust 协议镜像与自动校验链。

#### 内容

1. 继续强化 `game_core_protocols`；
2. 为 C++/C# 增加自动布局校验；
3. 视需要生成常量/头文件/注释代码；
4. 更新 `Shared/Protocols/` 文档。

#### 验收

1. 三端协议一致性可以自动验证；
2. 不再依赖人工对照字段顺序；
3. 新协议升级更安全可控。

---

## 8. 详细接口规划

### 8.1 Injector Core FFI

建议后续稳定为：

- `injector_core_default_view()`
- `injector_core_default_ini_text_utf8()`
- `injector_core_load_config_utf8(...)`
- `injector_core_build_runtime_plan(...)`
- `injector_core_tick(...)`
- `injector_core_consume_diagnostics(...)`

### 8.2 Payload Core FFI

建议后续稳定为：

- `payload_core_evaluate_runtime_header(...)`
- `payload_core_evaluate_logical_key_header(...)`
- `payload_core_decide_channel_emit(...)`
- `payload_core_observe_input_path(...)`
- `payload_core_collect_diagnostics(...)`
- `payload_core_apply_pause_clear(...)`

### 8.3 Control Core FFI

建议新增：

- `control_core_create(...)`
- `control_core_apply_profile(...)`
- `control_core_on_key_event(...)`
- `control_core_tick(...)`
- `control_core_copy_snapshot(...)`
- `control_core_collect_diagnostics(...)`

### 8.4 Helper Core FFI

建议新增：

- `helper_core_load_config(...)`
- `helper_core_tick(...)`
- `helper_core_apply_control_snapshot(...)`
- `helper_core_copy_status_snapshot(...)`
- `helper_core_collect_diagnostics(...)`

---

## 9. 测试与验收策略

### 9.1 Rust 单元测试

必须覆盖：

1. 协议尺寸/偏移；
2. Injector 配置与等待状态机；
3. Payload runtime/key/logical/path/channel decision；
4. 控制端快照发布状态机；
5. Helper 配置热更新与控制快照应用。

### 9.2 FFI 边界测试

必须覆盖：

1. struct 大小；
2. 错误码语义；
3. 指针+长度边界；
4. 空指针与非法参数；
5. panic 不穿越 ABI。

### 9.3 Windows 构建验证

至少保留：

1. `cargo test`
2. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
3. `Injector.vcxproj` Debug / Release
4. `Payload.vcxproj` Debug / Release
5. 需要时验证 GUI 对应 `.csproj`

### 9.4 实机回归

必须覆盖：

1. 高频方向键切换；
2. pause / clear / heartbeat 失活；
3. 后台从端连续同步；
4. Helper 控制开关与共享内存状态；
5. 注入等待、重试与 successfile/heartbeat 兜底。

---

## 10. 回退策略

后续每一阶段都必须具备：

1. Rust 接入可开关；
2. 诊断日志可观测；
3. 保留旧边界壳；
4. 出现问题时可退回上一阶段。

不允许的做法：

1. 一次性重写所有 native 核心；
2. 在没有测试的情况下切掉旧路径；
3. 为了“纯 Rust”而把 Win32/x86 边界也强行迁移。

---

## 11. 优先级总结

### P0

1. `Injector` 主状态机继续 Rust 化
2. `SyncMod.cpp` 继续瘦身，Rust 接管统一状态与适配器决策

### P1

3. 新建 `game_control_core`，收走 `SyncController.cs` 的控制内核

### P2

4. `HelperMod.cpp` 分层 Rust 化
5. 协议主源与生成/校验体系收口

### 默认不做

6. WPF UI 重写
7. `DllMain` / MinHook / trampoline 全量 Rust 化

---

## 12. 最终结论

结合当前仓库实现、现有 Rust 接入程度和官方文档约束，后续最需要 Rust 重构的部分是：

1. **Injector 主状态机**
2. **Payload / Sync 的统一状态与适配器决策层**
3. **控制端采集/快照发布内核**
4. **Helper 的配置/共享内存/后台线程状态机**
5. **协议主源与自动校验体系**

不应继续把主要精力放在：

1. WPF 展示层 Rust 化；
2. `DllMain` / hook 壳重写；
3. 直接依赖目标游戏私有 ABI 的脆弱 patch 层迁移。

后续正确路线应继续保持：

- **Rust 负责状态、协议、IPC、诊断、决策**
- **C++ 负责 Hook、Win32/x86 边界、patch 壳**
- **C# 负责 GUI 展示与配置交互**

这条路线与现有仓库结构、实机问题根因、Microsoft/Rust 官方最佳实践是一致的，也最符合当前项目“稳定优先、渐进迁移、可回退”的目标。
