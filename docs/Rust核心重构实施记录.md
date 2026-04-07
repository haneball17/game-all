# Rust 核心重构实施记录（第一批落地）

## 日期
- 2026-04-01

## 本次落地范围
本次不是一次性重写，而是先落地 **Rust 基础层 + 可回退核心抽象**：

1. 新增 `rust/` workspace
2. 新增协议镜像 crate：`game_core_protocols`
3. 新增通用基础设施 crate：`game_core_common`
4. 新增 Injector 核心 crate：`game_injector_core`
5. 新增 Payload 核心 crate：`game_payload_core`

## 已实现内容

### 1. 协议镜像与一致性测试
- 将以下协议以 Rust `#[repr(C, packed)]` 结构镜像到 `game_core_protocols`：
  - `HelperStatusV5`
  - `HelperControlV4`
  - `SharedKeyboardStateV2`
- 固定共享内存名称、版本号、大小常量。
- 增加单元测试校验：
  - 结构体大小
  - 关键字段偏移
  - 共享内存命名规则

### 2. 通用基础设施
- 抽出与现有 C++ 逻辑兼容的基础能力：
  - INI 简易解析
  - Windows 路径字符串归一化
  - 布尔值/数值兼容解析
- 这部分不依赖宿主平台文件系统行为，方便在 Linux 环境下先做纯逻辑测试。

### 3. Injector 核心逻辑（第一阶段）
- 落地 `InjectorConfig` Rust 模型。
- 对齐现有 C++ 默认配置。
- 实现：
  - 默认配置文本生成
  - INI 解析
  - 路径归一化
  - Helper 协议契约查询
- 新增最小 C ABI：
  - `injector_core_default_view`
  - `injector_core_helper_status_version`
  - `injector_core_helper_status_size`

### 4. Payload 同步核心抽象（第一阶段）
- 把“方向键释放收敛”抽成纯 Rust 状态机。
- 固化两条后续实现规则：
  1. 方向键松开后，允许额外抬起脉冲窗口。
  2. 方向键处于释放收敛窗口时，不允许继续复用旧快照缓存。
- 增加对应单元测试，作为后续替换 C++ Sync 核心的行为基线。

## 本次刻意不做的事
- 不直接替换现有 `Injector/main.cpp` 运行逻辑
- 不直接替换 `Payload/dllmain.cpp` 与 Hook 安装逻辑
- 不变更共享内存协议格式
- 不修改 GUI 读取路径

## 下一步建议
1. 在 Windows x86 构建链中接入 `game_injector_core` 的静态库导出。
2. 先用 Rust 替换 `Injector` 的配置/契约层，再逐步迁移等待与注入状态机。
3. 将 `Sync` 的共享内存读写、Clear/Paused/ActivePid/方向键收敛逐步迁入 `game_payload_core`。

---

## 第二批落地：Injector 接入 Rust 契约与默认配置（2026-04-01）

### 本次新增
- `Injector` 工程已开始直接链接 `game_injector_core` 静态库。
- `Injector` 默认配置模板改为由 Rust 提供 UTF-8 文本。
- `Injector` 数值默认配置改为优先读取 Rust 导出的 `InjectorConfigView`。
- `Injector` 对 `HelperStatusV5` 的版本/尺寸校验改为读取 Rust 导出的协议契约。

### 当前落地边界
- 已接入：
  - 默认配置模板
  - 数值默认配置视图
  - Helper 协议版本/尺寸契约
  - Windows 工程中的 Rust 静态库构建/链接入口
- 仍未接入：
  - Injector 的完整配置解析
  - 等待流程与注入状态机
  - successfile / 心跳流程主逻辑

### 当前价值
- 配置模板、默认值、协议契约开始以 Rust 为准，减少 C++/Rust 双写漂移。
- 后续可以继续把 `LoadInjectorConfig()`、等待流程和注入状态机逐步迁入 Rust，而不必重新设计接入边界。

### 当前验证说明
- Rust 侧已可继续用 `cargo test` / `cargo clippy` 验证。
- Windows x86 的 `.vcxproj` 集成已写入，但当前 Linux 环境无法直接完成 MSVC 构建验证，需在 Windows + VS + Rust MSVC x86 target 环境中补验证。

---

## 第三批落地：Payload 接入 Rust RuntimeDecision 与方向键收敛缓存仲裁（2026-04-01）

### 本次新增
- `Payload` 工程已开始直接链接 `game_payload_core` 静态库。
- `SyncMod` 接入 Rust 导出的 `PayloadRuntimeDecisionInterop`：
  - `IsSnapshotAlive / IsSnapshotAliveLite`
  - `IsBypassProcess / IsBypassProcessLite`
  - `ShouldSpoofFocus`
  这些运行时判定已改为优先使用 Rust 统一决策。
- `SyncMod` 的共享快照缓存接入 Rust 方向键收敛状态机：
  - 方向键状态变化会同步到 Rust `DirectionConvergenceState`
  - 快照缓存是否必须刷新，改为在原有 `cacheMs` 基础上再受 Rust 收敛窗口仲裁

### 当前落地边界
- 已接入：
  - Payload 工程中的 Rust 静态库构建/链接入口
  - 运行时状态统一判定（Alive / Paused / Clear / Bypass）
  - 方向键释放收敛对快照缓存的刷新仲裁
- 仍未接入：
  - Hook 安装与 MinHook 生命周期
  - RawInput / DirectInput / Win32 API 的统一 Rust 适配层
  - 控制端采集内核 Rust 化
  - 诊断事件总线导出

### 当前价值
- 执行端最核心的“是否存活/是否暂停/是否旁路”开始以 Rust 统一状态判断为准，减少多处手写判定漂移。
- 方向键释放窗口会主动压缩旧快照复用时间，直接对应《实机问题分析—同步延迟与方向键卡住.md》中“释放态晚一步”和“缓存放大问题”。
- Payload 仍保留 C++ Hook 外壳，因此整体风险低，可继续按计划逐段迁移。

### Windows 实测验证
已在 **Windows + VS 2022 Community + Rust `i686-pc-windows-msvc`** 环境完成以下验证：

1. `cargo build -p game_payload_core --target i686-pc-windows-msvc`
2. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /p:Configuration=Debug /p:Platform=Win32 /m`
3. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /p:Configuration=Debug /p:Platform=Win32 /m`
4. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Debug /p:Platform=x86 /m`
5. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Release /p:Platform=x86 /m`

结果：
- `game-payload.dll` 构建成功并复制到 `artifacts/run`
- `game-injector.exe` 构建成功并复制到 `artifacts/run`
- 顶层解决方案 `Debug|x86` 与 `Release|x86` 均构建成功

### 本次同步补强的计划对齐
- 对齐《DNF2012台湾服与多客户端同步重设计调研.md》
  - 保持“共享内存 + 被注入端执行”总架构不变
  - 先统一状态决策与缓存仲裁，再继续下探输入路径适配层
- 对齐《实机问题分析—同步延迟与方向键卡住.md》
  - 先处理方向键释放窗口与旧快照复用问题
  - 不贸然大改 Hook 面，优先把状态与收敛逻辑集中到 Rust

---

## 第四批落地：方向键强制抬起掩码接入 Win32 / RawInput / DirectInput（2026-04-01）

### 本次新增
- `SyncMod` 新增线程级 Rust 收敛状态：
  - `t_directionConvergenceState`
  - `t_directionLastState`
  - `t_forceReleaseMask`
- 共享快照刷新后，立即从 Rust 收敛状态机取出本轮 `force release mask`。
- 强制抬起掩码已真正接入以下执行路径：
  1. `Hook_GetAsyncKeyState`
  2. `Hook_GetKeyboardState`
  3. `Hook_GetRawInputBuffer`
  4. `Hook_GetRawInputData`
  5. `Hook_GetDeviceState`

### 当前价值
- 方向键进入释放窗口后，不再只是“促使缓存刷新”，而是会真正把后台读取到的键态压成抬起。
- 这一步比上一批更接近实机问题根因：后台窗口在 Win32 / RawInput / DirectInput 中残留“按住方向键”的概率进一步下降。
- 仍然保持 C++ Hook 外壳，风险控制在局部。

### Windows 再验证
本轮代码改动后，已再次完成：

1. `cargo test`
2. `cargo clippy --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Debug /p:Platform=x86 /m`
4. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Release /p:Platform=x86 /m`

结果：
- Rust 测试与静态检查全部通过
- 顶层解决方案 Debug / Release 均通过
- Payload 仅保留已有链接警告：
  - `LNK4075`
  - `LNK4098`
  暂未引入新的构建错误

---

## 第五批落地：单键决策收口到 Rust（2026-04-01）

### 本次新增
- `game_payload_core` 新增单键决策模型：
  - `KeyDecision`
  - `evaluate_key_state_header(...)`
- 新增 FFI：
  - `payload_core_evaluate_key_state_header(...)`
  - `PayloadKeyDecisionInterop`

### 当前接入范围
`SyncMod` 下列路径的“单键是否按下 / 是否拦截 / 是否应抬起”判断已优先走 Rust：

1. `Hook_GetAsyncKeyState`
2. `Hook_GetKeyboardState`
3. `Hook_GetRawInputBuffer`
4. `Hook_GetRawInputData`
5. `Hook_GetDeviceState`

### 当前价值
- 原来散落在 `SyncMod.cpp` 中的：
  - `alive`
  - `paused`
  - `targetMask`
  - `blockMask`
  - `force release`
  联合判断，开始由 Rust 统一输出单键决策结果。
- 这一步让 Win32 / RawInput / DirectInput 的按键解释更一致，属于“输入适配层 Rust 化”的继续推进。

### 本轮验证
已完成：

1. `cargo test`
2. `cargo clippy --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Debug /p:Platform=x86 /m`
4. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Release /p:Platform=x86 /m`

结果：
- Rust 测试新增 2 个 `KeyDecision` 用例后全部通过
- 顶层解决方案 Debug / Release 均通过

---

## 第六批落地：路径级决策收口到 Rust（2026-04-01）

### 本次新增
- `game_payload_core` 新增路径级决策模型：
  - `PathDecision`
  - `evaluate_path_decision_header(...)`
- 新增 FFI：
  - `payload_core_evaluate_path_decision_header(...)`
  - `PayloadPathDecisionInterop`

### 当前接入范围
`SyncMod` 下列路径级判断已优先走 Rust：

1. `ShouldSpoofFocus`
2. `Hook_GetRawInputBuffer` 中“是否走 Mapping 重写”
3. `Hook_GetRawInputData` 中“是否走 Mapping 重写”
4. `Hook_GetDeviceState` 中“是否旁路当前进程”

### 当前价值
- 焦点伪造不再由 C++ 散落判断 `alive / paused / bypass / active_pid`，而是改为 Rust 统一输出。
- RawInput 的“是否进入 Mapping 重写”开始统一基于 Rust 路径决策，减少执行端路径分支继续漂移。
- 这一步是从“单键决策 Rust 化”继续推进到“路径决策 Rust 化”，为后续继续收口 DirectInput / Focus / RawInput 适配层打基础。

### 本轮验证
已完成：

1. `cargo test`
2. `cargo clippy --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Debug /p:Platform=x86 /m`
4. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Release /p:Platform=x86 /m`

结果：
- Rust 测试与静态检查通过
- 顶层解决方案 Debug / Release 均通过
- Payload 仍只有既有链接警告：
  - `LNK4075`
  - `LNK4098`

---

## 第七批落地：原生工程输出目录按配置隔离（2026-04-01）

### 本次新增
- `Payload/Payload.vcxproj`
  - `OutDir` 改为 `artifacts\\bin\\payload\\$(Configuration)\\`
  - `IntDir` 改为 `artifacts\\obj\\payload\\$(Configuration)\\`
- `Injector/Injector.vcxproj`
  - `OutDir` 改为 `artifacts\\bin\\injector\\$(Configuration)\\`
  - `IntDir` 改为 `artifacts\\obj\\injector\\$(Configuration)\\`

### 解决的问题
- 原生工程此前把 Debug / Release 共用同一批 `artifacts/obj` 与 `artifacts/bin`，会导致：
  - Debug/Release 切换后 `.obj/.pdb` 串配置
  - 并行构建时 `vc145.pdb` 竞争
  - `main.obj` / `SyncMod.obj` 等中间产物被旧配置污染
- 这会直接影响 Rust 重构过程中的 Windows 验证稳定性。

### 本轮验证
本轮修复后，已再次完成：

1. `cargo test`
2. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Debug /p:Platform=x86 /m`
3. `MSBuild.exe E:\\code\\game-all\\game-all.sln /p:Configuration=Release /p:Platform=x86 /m`

结果：
- 顶层解决方案并行构建恢复稳定通过
- `Payload` / `Injector` 的 Debug 与 Release 中间产物已按配置隔离
- `artifacts/run` 仍保持原有统一运行产物布局

---

## 第八批落地：Injector 运行计划与 watch_mode 状态机下沉到 Rust（2026-04-07）

### 本次新增
- `game_injector_core` 新增 `runtime.rs`：
  - `InjectorRuntimePlan`
  - `InjectorWatchRuntime`
- 新增能力：
  - 基于 `injector.ini` 文本和基础目录构建**归一化运行计划**
  - watch_mode 下的任务状态机：
    - 进程发现
    - pending 任务收集
    - running 标记
    - finished 标记
    - 进程退出后的任务移除
    - idle 自动退出判定

### 新增 FFI
- `injector_core_load_config_utf8(...)`
- `injector_core_watch_runtime_create(...)`
- `injector_core_watch_runtime_destroy(...)`
- `injector_core_watch_runtime_observe_processes(...)`
- `injector_core_watch_runtime_collect_pending(...)`
- `injector_core_watch_runtime_mark_started(...)`
- `injector_core_watch_runtime_mark_finished(...)`
- `injector_core_watch_runtime_collect_removals(...)`
- `injector_core_watch_runtime_should_exit_idle(...)`
- `injector_core_watch_runtime_task_count(...)`

### 当前接入范围
`Injector/main.cpp` 已改为优先让 Rust 承担：

1. 配置文本解析与路径归一化
2. watch_mode 的任务表状态维护
3. pending / running / finished / removal 的任务生命周期决策
4. idle_exit_seconds 自动退出判定

当前仍保留在 C++ 的部分：

1. `QueueUserAPC` 注入动作
2. `WaitForProcessWindow(...)`
3. successfile / heartbeat 的具体验证循环
4. Win32 句柄、线程与日志边界

### 当前价值
- `Injector/main.cpp` 不再直接维护 `unordered_map<DWORD, InjectTask>` 这类主状态表。
- watch_mode 下“哪些任务可启动、何时可移除、何时可触发 idle 退出”开始由 Rust 统一决策。
- 配置归一化从“Rust 解析 + C++ 再次手工补路径”收敛为“Rust 直接输出可执行配置”。
- 这一步为后续继续把 successfile / heartbeat / retry 状态机迁入 Rust 打下边界基础。

### 本轮验证
已完成：

1. `cargo test -p game_injector_core`
2. `cargo test --workspace`
3. `cargo clippy -p game_injector_core --all-targets --all-features -- -D warnings`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
5. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
6. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_injector_core` 新增 4 个 runtime 测试后全部通过
- Rust workspace 测试与静态检查通过
- Windows `Injector` Debug / Release 构建通过
- 当前改动未影响现有 GUI / Payload 构建边界

---

## 第九批落地：Injector 成功判定与重试状态机继续收口到 Rust（2026-04-07）

### 本次新增
- `game_injector_core` 新增：
  - `HelperHeartbeatDecision`
  - `InjectionRetryRuntime`
  - `InjectionRetryDecision`
- 新增 FFI：
  - `injector_core_evaluate_helper_heartbeat(...)`
  - `injector_core_retry_runtime_create(...)`
  - `injector_core_retry_runtime_destroy(...)`
  - `injector_core_retry_runtime_can_attempt(...)`
  - `injector_core_retry_runtime_current_attempt(...)`
  - `injector_core_retry_runtime_finish_attempt(...)`

### 当前接入范围
`Injector/main.cpp` 已改为优先让 Rust 统一负责：

1. `HelperStatusV5` 的协议/心跳有效性判断
2. 单次注入尝试是否成功：
   - successfile 成功
   - heartbeat 成功
3. 尝试失败后是否继续重试
4. 重试间隔与 finished 判定

当前仍保留在 C++：

1. `WaitForSuccessFile(...)` 的文件等待循环
2. `HasHelperHeartbeat(...)` 的共享内存读取壳
3. `QueueUserAPC` 注入动作
4. `WaitForProcessWindow(...)`

### 当前价值
- `TryInjectProcess(...)` 不再自己维护“attempt < max_retries / Sleep(retry_interval_ms)”这套重试决策。
- successfile 与 heartbeat 的成功信号开始统一汇总到 Rust `InjectionRetryRuntime`。
- 心跳判定不再由 C++ 直接写死协议/超时解释，而是开始由 Rust 核心输出结果。
- 这一步让 Injector 的“成功判定 + 重试策略”继续从 C++ 边界层抽离。

### 本轮验证
已完成：

1. `cargo test -p game_injector_core`
2. `cargo clippy -p game_injector_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
5. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
6. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_injector_core` 新增 3 个测试后全部通过
- Rust workspace 测试与静态检查通过
- Windows `Injector` Debug / Release 构建通过
- Phase 1 中“配置/运行计划/watch/retry/heartbeat 决策”已形成稳定 Rust 边界

---

## 第十批落地：Injector 注入后端抽象、successfile 目录通知与窗口就绪提示信号（2026-04-07）

### 本次新增
- `game_injector_core::config` 新增配置枚举：
  - `InjectionBackendKind`
  - `SuccessObserverMode`
- `InjectorConfig` / `InjectorConfigView` / `InjectorConfigInterop` 已新增：
  - `inject_backend`
  - `success_observer_mode`
- 默认配置与模板已更新：
  - `inject_backend=apc`
  - `success_observer_mode=notify`

### 当前接入范围
`Injector/main.cpp` 已新增并接入以下边界适配器：

1. `TryWaitForInputIdleHint(...)`
   - 仅作为 GUI 初始化提示信号，不替代窗口存在性确认
2. `ProbeProcessWindowReady(...)`
   - 先尝试 `WaitForInputIdle`
   - 再回到 `EnumWindows` 做最终窗口确认
3. `ObserveSuccessFileChange(...)`
   - 默认使用目录变更通知
   - 回退到原有时间戳轮询
4. `PerformInjectionWithBackend(...)`
   - 当前默认 `apc`
   - `fallback` 仅预留接口并记录日志

### 当前价值
- successfile 观察从固定 `Sleep(...)` 轮询升级为“目录通知优先、轮询回退”。
- 注入方式不再在主流程中写死为 APC，而是开始形成可切换的后端抽象。
- 窗口等待不再只依赖纯轮询，开始引入 `WaitForInputIdle` 作为**辅助提示信号**。
- 这一步与官方文档约束对齐：
  - `QueueUserAPC` 保留在 Win32 边界，不继续向 Rust 深推
  - `WaitForInputIdle` 仅作提示，不作为真值来源
  - successfile 观察开始减少固定轮询带来的空转

### 本轮验证
已完成：

1. `cargo test -p game_injector_core`
2. `cargo clippy -p game_injector_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
5. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
6. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust workspace 测试与静态检查通过
- Windows `Injector` Debug / Release 构建通过
- 当前仍保持 APC 为默认后端，未破坏既有运行方式

---

## 第十一批落地：Injector 结构化平台观测结果与 fallback 降级语义（2026-04-07）

### 本次新增
- 新增结构化 interop 结果类型：
  - `InjectorWindowProbeResultInterop`
  - `InjectorSuccessObservationResultInterop`
  - `InjectorHeartbeatObservationResultInterop`
  - `InjectorBackendExecutionResultInterop`
- 新增 FFI：
  - `injector_core_retry_runtime_finish_attempt_with_results(...)`
- `game_injector_core` 新增：
  - `WindowProbeResult`
  - `SuccessObservationResult`
  - `HeartbeatObservationResult`
  - `BackendExecutionResult`
  - `finish_attempt_with_observation(...)`

### 当前接入范围
`Injector/main.cpp` 中以下平台适配器已改为返回结构化结果，而不是单纯 `bool`：

1. `ProbeProcessWindowReady(...)`
2. `ObserveSuccessFileChange(...)`
3. `ObserveHelperHeartbeat(...)`
4. `PerformInjectionWithBackend(...)`

当前效果：

- `TryInjectProcess(...)` 不再自己拼接 backend/success/heartbeat 的布尔结果，
  而是统一把结构化观测结果交回 Rust 推进一次 attempt。
- `inject_backend=fallback` 的当前运行语义已固定为：
  - 自动降级为 `apc`
  - 记录 warning
  - 结构化结果中标记 `downgraded=1`
- `WaitForInputIdle` 的使用语义也已固定：
  - 仅作为窗口初始化 hint
  - 不作为窗口 ready 的真值来源

### 当前价值
- `Injector/main.cpp` 的平台边界开始具备统一形状，为后续继续收口到 Rust 状态机打下稳定接口。
- successfile / heartbeat / backend 的观测结果不再散落为多个 `bool`，诊断能力更强。
- `fallback` 不再是“配置后直接失败”的不确定状态，而是明确降级为 `apc`。
- 这一步让 Injector 的边界模型更接近后续 `Sync` 侧要采用的“Rust 决策 + C++ 观测器”模式。

### 本轮验证
已完成：

1. `cargo test -p game_injector_core`
2. `cargo clippy -p game_injector_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
5. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_injector_core` 新增 1 个结构化观测测试后全部通过
- Rust clippy 通过
- Windows `Injector` Debug / Release 构建通过
- 当前变更未破坏既有 `Payload` / GUI 构建链

---

## 第十二批落地：Sync 路径观测与适配器投影状态模型进入 Rust（2026-04-07）

### 本次新增
- `game_payload_core::runtime` 新增：
  - `InputChannelKind`
  - `InputPathObservation`
  - `AdapterProjectedState`
- 新增能力：
  - `observe_input_path(...)`
  - `evaluate_adapter_projected_state(...)`

### 新增 FFI
- `payload_core_observe_input_path(...)`
- `payload_core_evaluate_adapter_projected_state(...)`
- `PayloadInputPathObservationInterop`
- `PayloadAdapterProjectedStateInterop`

### 当前接入范围
`SyncMod.cpp` 已开始改为由 Rust 输出路径观测摘要：

1. `[OBS]` 日志不再由 C++ 手写 `ResolveInputChannel(...)` 直接决定
2. 现在改为：
   - 由 Rust 根据 `RawInput / DirectInput / Win32` 计数输出 `channel_kind`
   - 同时输出 `mixed_inputs / raw_active / direct_input_active / win32_active`

当前仍保留在 C++：

1. 实际 Hook 计数器采集
2. `[OBS]` 日志格式化输出
3. projected state 的真实数组存储

### 当前价值
- 路径观测开始从“C++ 零散日志拼接”转为“Rust 统一模型 + C++ 输出壳”。
- 现有 `ResolveInputChannel` 的优先级语义（RawInput > DirectInput > Win32）已在 Rust 测试中固化。
- 后续继续做 drift 检测、projected state 收口时，已具备统一的状态结构，不需要再在 C++ 里临时拼新字段。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
5. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`
6. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Debug -p:PlatformTarget=x86`
7. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Release -p:PlatformTarget=x86`

结果：
- `game_payload_core` 新增 2 个测试后全部通过
- Rust workspace 测试通过
- Windows `Payload` Debug / Release 构建通过
- `GameMasterGUI` Debug / Release 构建通过

---

## 第十三批落地：Sync drift 汇总继续收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::runtime` 新增：
  - `AdapterDriftSummary`
  - `summarize_adapter_drift(...)`
- `game_payload_core_ffi` 新增：
  - `PayloadAdapterDriftSummaryInterop`
  - `payload_core_summarize_adapter_drift(...)`

### 当前接入范围
`SyncMod.cpp` 中用于 `[OBS]` 输出的 256 键 drift 统计，已从 C++ 循环改为 Rust 汇总：

1. `raw_drift_count`
2. `win32_drift_count`
3. `direct_input_drift_count`

当前 `[OBS]` 日志已变为：
- 路径通道摘要由 Rust 输出
- mixed/raw/di/win32 活跃态由 Rust 输出
- drift 汇总也由 Rust 输出

### 当前价值
- `SyncMod.cpp` 不再自己逐键遍历统计 drift，观测摘要进一步从“日志拼接”收口到 Rust 核心模型。
- 这一步让后续继续做 `projected state` / `drift detection` / `adapter diagnostics` 时，Rust 已经具备可复用的摘要接口。
- 为后续把 `[OBS]` 扩展成更高信号的适配器观测事件总线打下基础。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_payload_core` 新增 1 个 drift summary 测试后全部通过
- Rust clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第十四批落地：Sync logical/projected state store 进入 Rust（2026-04-07）

### 本次新增
- `game_payload_core::sync` 新增：
  - `ProjectedChannelKind`
  - `SyncStateStore`
- 新增能力：
  - `set_logical_desired(...)`
  - `logical_desired(...)`
  - `set_projected(...)`
  - `projected(...)`
  - `clear_logical_desired()`
  - `clear_all_projected()`
  - `clear_projected_channel(...)`

### 新增 FFI
- `payload_core_state_store_create(...)`
- `payload_core_state_store_destroy(...)`
- `payload_core_state_store_set_logical_desired(...)`
- `payload_core_state_store_get_logical_desired(...)`
- `payload_core_state_store_set_projected(...)`
- `payload_core_state_store_get_projected(...)`
- `payload_core_state_store_clear_logical_desired(...)`
- `payload_core_state_store_clear_all_projected(...)`
- `payload_core_state_store_clear_projected_channel(...)`

### 当前接入范围
`SyncMod.cpp` 已开始改为“Rust store 为主、C++ 数组为镜像”：

1. `g_payloadStateStore` 新增为全局 Rust state store 句柄
2. `g_lastLogicalDesiredState / g_lastRawKeyboardState / g_lastWin32State / g_lastDIState`
   已开始退化为镜像缓存
3. 以下关键路径已优先改为读写 Rust store：
   - `ApplyClearIfNeeded(...)`
   - `ApplyRawClearIfNeeded(...)`
   - `EvaluateLogicalKeyDecision(...)`
   - `EvaluateChannelEmitDecision(...)`
   - `TryPickSilentRawTransition(...)`
   - `RecordWin32KeyEventIfNeeded(...)`
   - `RecordDirectInputKeyEventIfNeeded(...)`
   - 部分 RawInput projected state 推进点

### 当前价值
- `Sync` 的 logical desired state 和三个 adapter projected state，终于开始从 C++ 全局数组迁入 Rust 真值存储。
- 这一步是从“观测与摘要 Rust 化”进入“状态真值 Rust 化”的关键切换点。
- 后续继续做 pause/clear reset、projected state 收口、adapter drift 诊断时，已经不需要再以 C++ 数组作为唯一真值来源。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_payload_core` 新增 2 个 state store 测试后全部通过
- Rust clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第十五批落地：Sync pause release plan 开始收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::sync` 新增：
  - `PauseReleaseReason`
  - `PauseReleaseDecision`
  - `SyncStateStore::pick_pause_release(...)`
- `game_payload_core_ffi` 新增：
  - `PayloadPauseReleaseDecisionInterop`
  - `payload_core_state_store_pick_pause_release(...)`

### 当前接入范围
`SyncMod.cpp` 的 `TryPickSilentRawTransition(...)` 已开始改为：

1. 由 Rust state store 决定 pause/静默态应该优先释放哪个键
2. C++ 只负责：
   - 把 Rust 返回的 `vkey / reason / had_projected` 投影到真实 RawInput 路径
   - 输出现有 `[PAUSE]` 日志

当前 Rust 已接管的释放顺序语义：

1. preferred key
2. direction pair
3. any direction key
4. stale projected key
5. neutralize preferred key

### 当前价值
- `pause` 期间的释放顺序不再散落在 C++ lambda 中，而是开始由 Rust state store 决策。
- 这一步把 `clear/pause reset` 收口工作的第一块关键逻辑迁入了 Rust。
- 后续继续做 `ApplyClearIfNeeded(...)` / `ApplyRawClearIfNeeded(...)` 的 Rust 收口时，已经有可复用的状态机接口。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_payload_core` 新增 2 个 pause release 测试后全部通过
- Rust clippy 通过
- Windows `Payload` Debug 构建通过

---

## 第十六批落地：Sync clear reset 开始收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::sync` 新增：
  - `ClearResetDecision`
  - `SyncStateStore::apply_clear_reset(...)`
- `game_payload_core_ffi` 新增：
  - `PayloadClearResetDecisionInterop`
  - `payload_core_state_store_apply_clear_reset(...)`

### 当前接入范围
`SyncMod.cpp` 中以下清理路径已开始改为由 Rust state store 决策：

1. `ApplyClearIfNeeded(...)`
   - 先由 Rust 决定是否清 logical desired
2. `ApplyRawClearIfNeeded(...)`
   - 先由 Rust 决定是否清 projected state

当前仍保留在 C++：

1. `g_lastEdgeCounter` 的镜像更新
2. 实际镜像数组清零
3. Hook 路径中的状态投影与日志

### 当前价值
- `clear` 不再只是 C++ 直接 `memset/赋零`，而是开始通过 Rust state store 统一处理逻辑态与 projected state 的清理语义。
- 这一步把 `clear/pause reset` 收口的第二块关键逻辑迁入了 Rust。
- 后续继续做“projected state 生命周期”和“adapter diagnostics snapshot”时，clear 语义已经和 store 真值统一在一处。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_payload_core` 新增 1 个 clear reset 测试后全部通过
- Rust clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第十七批落地：Sync observation snapshot 进入 Rust（2026-04-07）

### 本次新增
- `game_payload_core::diagnostics` 新增：
  - `SyncObservationSnapshot`
  - `build_sync_observation_snapshot(...)`
- `game_payload_core_ffi` 新增：
  - `PayloadSyncObservationSnapshotInterop`
  - `payload_core_build_sync_observation_snapshot(...)`

### 当前接入范围
`SyncMod.cpp` 的诊断输出已开始改为：

1. 先由 Rust 产出：
   - `InputPathObservation`
   - `AdapterDriftSummary`
   - `SyncObservationSnapshot`
2. C++ 只负责：
   - 读取 Hook 计数器
   - 读取共享快照基础字段
   - 把 Rust 快照格式化成现有 `[OBS]` 日志

### 当前价值
- 现有 `[OBS]` 已不再只是临时拼装字段，而是开始消费 Rust 统一诊断快照。
- 路径观测、drift 汇总、alive/paused/activePid 等诊断信息开始在 Rust 内部形成统一快照模型。
- 这一步为后续继续做 adapter diagnostics event/snapshot 总线打下稳定接口。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_payload_core` 新增 1 个 observation snapshot 测试后全部通过
- Rust clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第十八批落地：Sync adapter diagnostics event/buffer 开始收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::diagnostics` 新增：
  - `AdapterDiagnosticsEventKind`
  - `AdapterDiagnosticsEvent`
  - `AdapterDiagnosticsBuffer`
- `game_payload_core_ffi` 新增：
  - `PayloadAdapterDiagnosticsEventInterop`
  - `payload_core_diagnostics_buffer_create(...)`
  - `payload_core_diagnostics_buffer_destroy(...)`
  - `payload_core_diagnostics_buffer_push_event(...)`
  - `payload_core_diagnostics_buffer_latest(...)`
  - `payload_core_diagnostics_buffer_copy_latest_n(...)`

### 当前接入范围
`SyncMod.cpp` 已开始把以下高信号事件推入 Rust diagnostics buffer：

1. `LogicalChanged`
   - 来源：`LogLogicalEdge(...)`
2. `AdapterEmitted`
   - 来源：`LogAdapterEmit(...)`
3. `PauseRelease`
   - 来源：`LogPauseInterception(...)`
4. `ClearApplied`
   - 来源：`ApplyClearIfNeeded(...)` / `ApplyRawClearIfNeeded(...)`

当前 `Sync` diagnostics 总线已形成：

- snapshot：
  - `SyncObservationSnapshot`
- event：
  - `AdapterDiagnosticsEvent`
- buffer：
  - `AdapterDiagnosticsBuffer`

### 当前价值
- `Sync` 不再只有单条 `[OBS]` 快照日志，已经开始形成“snapshot + event buffer”双轨诊断模型。
- `logical desired / projected state / pause / clear / emit` 开始进入同一条 Rust diagnostics 通道。
- 这为后续继续做：
  - adapter diagnostics 导出
  - GUI 读取最近事件
  - minidump / 日志联合复盘
  提供了统一基础。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_payload_core` 新增 1 个 diagnostics buffer 测试后全部通过
- Rust clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第十九批落地：Sync Rust diagnostics 导出链接入（2026-04-07）

### 本次新增
- `SyncMod.cpp` 已开始输出稳定的 `[RUSTDIAG]` 段：
  - `snapshot`
  - `latest N events`
- `DiagnosticExportService` 已把 `[RUSTDIAG]` 纳入 `SyncFocusKeywords`

### 当前接入范围
现在导出诊断文件时，`Sync` 关键摘录不再只有：

- `[OBS]`
- `[EMIT]`
- `[PAUSE]`
- `[EDGE]`
- `[GROUP]`
- `[REPEAT]`

而是还会纳入：

- `[RUSTDIAG] snapshot ...`
- `[RUSTDIAG] event ...`

当前 `Sync` diagnostics 导出链已形成：

1. Rust 侧维护：
   - `SyncObservationSnapshot`
   - `AdapterDiagnosticsBuffer`
2. C++ 侧负责：
   - 从 Rust 读取 snapshot / latest N events
   - 写入 payload 日志
3. GUI 导出器负责：
   - 把 `[RUSTDIAG]` 段纳入单文件导出

### 当前价值
- Rust diagnostics 不再只存在于 Payload 进程内存中，而是开始真正进入导出链。
- 多开、偶发卡方向、pause/clear 异常等问题，后续可以直接从导出文件里看到 Rust 结构化诊断结果。
- 这一步让方向 1“先做 diagnostics 导出/消费链”真正成立，为后续继续做 projected state 生命周期深收口提供更强观测面。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`
5. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Debug -p:PlatformTarget=x86`
6. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Release -p:PlatformTarget=x86`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第二十六批落地：Injector attempt 结果解释继续收口到 Rust（2026-04-08）

### 本次新增
- `game_injector_core::runtime` 已新增：
  - `AttemptOutcomeCode`
  - `AttemptOutcomeSummary`
  - `summarize_attempt_outcome(...)`
- `game_injector_core_ffi` 已新增：
  - `InjectorAttemptOutcomeSummaryInterop`
  - `injector_core_summarize_attempt_outcome(...)`

### 本次收口
- `TryInjectProcess(...)` 不再自己根据：
  - `backend.started`
  - `success.observed`
  - `heartbeat.observed`
  - `mapping_found / contract_ok`
  去解释本轮结果来源。
- Rust 现在统一给出：
  - 本轮是否成功
  - 是否应重试
  - 成功来源：
    - `successfile`
    - `heartbeat`
  - 失败原因：
    - backend 未启动
    - heartbeat 映射缺失
    - heartbeat 协议不匹配
    - successfile / heartbeat 均超时

### 当前价值
- `Injector` 的“结果解释层”继续从 C++ 胶水逻辑中拿掉。
- `TryInjectProcess(...)` 现在更接近：
  - 执行平台探针
  - 把探针结果交给 Rust
  - 按 Rust 结果记录日志
- 为下一步继续压缩 successfile / heartbeat 等待与结果汇总提供了更清晰的落脚点。

### 本轮验证
已完成：

1. `cargo test -p game_injector_core`
2. `cargo clippy -p game_injector_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Injector` Debug / Release 构建通过

---

## 第二十八批落地：新增 game_control_core 最小内核与 C ABI（2026-04-08）

### 本次新增
- 新增 crate：
  - `rust/crates/game_control_core`
- 新增控制端纯逻辑模型：
  - `WindowSnapshotInput`
  - `ForegroundTracker`
  - `ForegroundDecision`
  - `PublishHeader`
  - `PublishHeaderInput`
- 新增控制端纯逻辑函数：
  - `evaluate_foreground_state(...)`
  - `build_publish_header(...)`
- 新增 C ABI：
  - `game_control_core_evaluate_foreground(...)`
  - `game_control_core_build_publish_header(...)`
- 新增头文件：
  - `rust/include/game_control_core_ffi.h`

### 当前覆盖的逻辑
- 从 `SyncController.cs` 中抽出了最适合纯逻辑 Rust 化的第一批内容：
  - 前台 DNF 判定
  - foreground grace
  - disable auto pause 行为
  - 共享快照头部 flags / active_pid / profile 元数据计算

### 当前价值
- `game_control_core` 不再只是计划中的名字，已经有可测试、可复用、可接 FFI 的最小内核。
- 这一步为后续把 `SyncController.cs` 的控制内核逐步迁出 GUI 打下了第一块稳定地基。
- 目前还没有接到 C#，但它已经具备被 GUI 调用的边界形状。

### 本轮验证
已完成：

1. `cargo test -p game_control_core`
2. `cargo clippy -p game_control_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`

结果：
- `game_control_core` 自测通过
- Rust workspace 全量测试与 clippy 通过

---

## 第二十九批落地：新增 game_helper_core 最小协议与控制计划内核（2026-04-08）

### 本次新增
- 新增 crate：
  - `rust/crates/game_helper_core`
- 当前已实现的最小 Helper 核心包括：
  - `HelperStatus` 协议契约校验
  - `HelperControl` 协议契约校验
  - `ActionMask` -> 控制应用计划解码
- 已收口的动作位包括：
  - fullscreen attack
  - fullscreen skill
  - auto transparent
  - hotkey enabled
  - attract enabled / mode / positive
  - gather items
  - damage enabled / multiplier
  - invincible enabled

### 当前价值
- `Helper` 这条线不再只有 `game_core_protocols` 的结构镜像，已经开始有真正的“控制计划解释层”。
- 后续替换 `HelperMod.cpp` 中的协议校验、`ActionMask` 解析与共享内存控制应用时，不需要从零开始抽象。
- 这是 `Helper` 从“纯 C++ 胶水”走向“Rust 状态机/协议内核”的第一步。

### 本轮验证
已完成：

1. `cargo test -p game_helper_core`
2. `cargo clippy -p game_helper_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`

结果：
- `game_helper_core` 自测通过
- Rust workspace 全量测试与 clippy 通过

---

## 第三十批落地：game_helper_core 开始接回 HelperMod 控制路径（2026-04-08）

### 本次新增
- `game_helper_core` 已新增 C ABI：
  - `game_helper_core_evaluate_status_contract(...)`
  - `game_helper_core_evaluate_control_contract(...)`
  - `game_helper_core_decode_control_apply_plan(...)`
- `rust/include/` 已新增：
  - `game_helper_core_ffi.h`

### 本次接回
- `Payload/Payload.vcxproj` 现在已同时构建并链接：
  - `game_payload_core`
  - `game_helper_core`
- `HelperMod.cpp` 目前已开始用 Rust 承接：
  - `HelperControlV4` 协议契约校验
  - `ActionMask` -> 控制应用计划解码
- `ControlReaderTick()` 不再自己直接判断 control 协议头是否匹配。
- `ApplyControlActions(...)` 已开始消费 Rust 解码后的 `HelperControlApplyPlanInterop`，不再自己直接解 `action_mask` 位。

### 当前价值
- `Helper` 这条线第一次真正把 Rust 逻辑接回了被注入执行端。
- 现在 `Helper` 已经不仅有“协议镜像”和“独立 crate”，而是开始实际替换 `HelperMod.cpp` 中的协议/控制解析层。
- 这为后续继续收口：
  - 状态快照写入
  - 控制读写状态机
  - 后台线程诊断
  提供了可持续的接入路径。

### 本轮验证
已完成：

1. `cargo test -p game_helper_core`
2. `cargo clippy -p game_helper_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
5. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
6. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust crate 与 workspace 全量验证通过
- Windows `Payload` Debug / Release 构建通过

---

## 第三十一批落地：game_helper_core 开始接回 HelperStatus 快照生成（2026-04-08）

### 本次新增
- `game_helper_core` 已新增：
  - `HelperStatusSnapshotInput`
  - `build_helper_status_snapshot(...)`
- `game_helper_core_ffi` 已新增：
  - `HelperStatusSnapshotInputInterop`
  - `game_helper_core_build_status_snapshot(...)`

### 本次接回
- `WriteSharedMemorySnapshot()` 不再在 C++ 中手工组装全部 `HelperStatusV5` 数值字段。
- Rust 现在负责生成 `HelperStatusV5` 的数值快照主体，包括：
  - `last_tick_ms`
  - `pid`
  - `process_alive`
  - auto transparent
  - fullscreen attack target / patch on
  - attract mode / positive
  - gather items
  - damage / multiplier
  - invincible
  - summon
  - fullscreen skill
  - hotkey enabled
- C++ 继续保留：
  - 读取 patch 字节
  - 读取玩家名
  - 最终共享内存写入

### 当前价值
- `HelperStatus` 快照生成开始从 `HelperMod.cpp` 中迁出。
- `Helper` 现在已经同时有：
  - control 协议校验
  - action mask 解码
  - status snapshot 生成
  这三类 Rust 内核能力。
- 后续继续推进共享内存写入状态机时，C++ 只需保留真正依赖游戏内存/本地 API 的边界读取。

### 本轮验证
已完成：

1. `cargo test -p game_helper_core`
2. `cargo clippy -p game_helper_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
5. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
6. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- `game_helper_core` 与 Rust workspace 全量验证通过
- Windows `Payload` Debug / Release 构建通过

---

## 第三十二批落地：game_control_core 开始接回 SyncController（2026-04-08）

### 本次接回
- `DNFSyncBox.csproj` 现在会在构建前自动编译 `game_control_core`，并把生成的 `game_control_core.dll` 复制到输出目录。
- `SyncController.cs` 当前已开始直接消费 Rust 控制内核：
  - `RefreshWindows()` 的 foreground / grace / auto pause 判定
  - `PublishSnapshot()` 的共享快照头部构建
- `NativeMethods.cs` 已新增 `game_control_core` 的 P/Invoke 边界与互操作结构。

### 当前覆盖范围
- 已迁出的控制端逻辑包括：
  - disable auto pause 行为
  - foreground grace
  - effective foreground pid
  - `auto_paused` 计算
  - `flags / active_pid / profile_id / profile_mode / last_tick` 头部生成

### 当前价值
- `game_control_core` 已从“独立 Rust crate”升级为“真实接回 GUI 的控制内核”。
- `SyncController.cs` 中最适合纯逻辑抽离的前台/暂停/发布头部逻辑已开始减少。
- 上层 GUI 构建产物中已能看到 `game_control_core.dll`，说明后续继续迁移剩余控制逻辑具备实际接入路径。

### 本轮验证
已完成：

1. `cargo test --workspace`
2. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
3. `dotnet build E:\\code\\game-all\\GUI\\Modules\\Sync\\DNFSyncBox.csproj -c Debug -p:PlatformTarget=x86`
4. `dotnet build E:\\code\\game-all\\GUI\\Modules\\Sync\\DNFSyncBox.csproj -c Release -p:PlatformTarget=x86`
5. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Debug -p:PlatformTarget=x86`
6. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Release -p:PlatformTarget=x86`

结果：
- Rust workspace 全量测试与 clippy 通过
- `DNFSyncBox` Debug / Release 构建通过
- `GameMasterGUI` Debug / Release 构建通过
- 输出目录已包含 `game_control_core.dll`

---

## 第三十三批落地：game_control_core 开始接回发布模式与 block mask 收口（2026-04-08）

### 本次新增
- `game_control_core` 已新增：
  - `finalize_publish_profile(...)`
- `game_control_core_ffi` 已新增：
  - `game_control_core_finalize_publish_profile(...)`

### 本次接回
- `SyncController.cs` 中原本本地执行的这段逻辑已开始交给 Rust：
  - `reportedMode` 计算
  - Replace 模式提升为 Mapping 模式
  - `mapping source mask` 合并到 `block mask`
- 当前 `PublishSnapshot()` 已经把：
  - 前台/自动暂停判定
  - 共享头部构建
  - publish mode / block mask 收口
  这三类逻辑的一部分交给 `game_control_core`

### 当前价值
- `SyncController.cs` 中和共享快照头部直接相关的控制逻辑进一步减少。
- `game_control_core` 已经不仅能算“是否暂停”，还能开始干预共享快照的最终发布形态。
- 这为后续继续迁出 profile 应用和 heartbeat/foreground 状态机打下了更完整的边界。

### 本轮验证
已完成：

1. `cargo test -p game_control_core`
2. `cargo clippy -p game_control_core --all-targets --all-features -- -D warnings`
3. `cargo test --workspace`
4. `cargo clippy --workspace --all-targets --all-features -- -D warnings`
5. `dotnet build E:\\code\\game-all\\GUI\\Modules\\Sync\\DNFSyncBox.csproj -c Debug -p:PlatformTarget=x86`
6. `dotnet build E:\\code\\game-all\\GUI\\Modules\\Sync\\DNFSyncBox.csproj -c Release -p:PlatformTarget=x86`
7. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Debug -p:PlatformTarget=x86`
8. `dotnet build E:\\code\\game-all\\GUI\\GameMasterGUI.csproj -c Release -p:PlatformTarget=x86`

结果：
- Rust workspace 全量测试与 clippy 通过
- `DNFSyncBox` Debug / Release 构建通过
- `GameMasterGUI` Debug / Release 构建通过

---

## 第二十七批落地：Injector attempt 失败原因开始统一由 Rust 汇总（2026-04-08）

### 本次新增
- `game_injector_core::runtime` 已新增：
  - `AttemptOutcomeCode`
  - `AttemptOutcomeSummary`
  - `summarize_attempt_outcome(...)`
- `game_injector_core_ffi` 已新增：
  - `InjectorAttemptOutcomeSummaryInterop`
  - `injector_core_summarize_attempt_outcome(...)`

### 本次收口
- `TryInjectProcess(...)` 不再在 C++ 中自己根据：
  - `backend.started`
  - `success.observed`
  - `heartbeat.mapping_found`
  - `heartbeat.contract_ok`
  - `timed_out`
  去解释本轮失败原因。
- Rust 现在统一输出：
  - 本轮是否成功
  - 是否应重试
  - 成功来源：
    - `successfile`
    - `heartbeat`
  - 失败原因：
    - backend 未启动
    - heartbeat 映射缺失
    - heartbeat 协议不匹配
    - successfile / heartbeat 均超时

### 当前价值
- `Injector` 的“平台观测结果解释层”进一步从 C++ 中拿掉。
- `main.cpp` 更接近“采集平台观测 -> 调 Rust 汇总 -> 记录结果”。
- 为下一步继续压缩 `ProbeProcessWindowReady / ObserveSuccessFileChange / ObserveHelperHeartbeat` 留出了更清晰的接口边界。

### 本轮验证
已完成：

1. `cargo test -p game_injector_core`
2. `cargo clippy -p game_injector_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Injector\\Injector.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Injector` Debug / Release 构建通过

---

## 第二十五批落地：方向键 group 诊断开始统一由 Rust 输出（2026-04-07）

### 本次新增
- `game_payload_core::runtime` 已新增：
  - `DirectionGroupReason`
  - `DirectionGroupDecision`
  - `evaluate_direction_group_decision(...)`
- `game_payload_core_ffi` 已新增：
  - `PayloadDirectionGroupDecisionInterop`
  - `payload_core_evaluate_direction_group_decision(...)`

### 本次收口
- `EvaluateLogicalKeyDecision(...)` 不再在 C++ 中自己推导方向键 pair conflict 的：
  - winner
  - loser
  - `edge_counter / edge_tie_release`
- Rust 现在直接根据：
  - `desired_down`
  - `pair_conflict`
  - pair 键目标态
  - pair 键 force release
  统一返回 direction group 决策。

### 当前价值
- `LogicalKeyDecision` 之后的方向键 group 诊断不再散落在 C++。
- `Sync` 路径上的 edge / group / selection / transition 四类诊断继续向 Rust 汇总。
- `EvaluateLogicalKeyDecision(...)` 这个包装层再次变薄。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第二十四批落地：logical raw emit 决策开始统一由 Rust 输出（2026-04-07）

### 本次新增
- `game_payload_core::runtime` 已新增：
  - `LogicalRawTransitionDecision`
  - `decide_logical_raw_transition_with_store(...)`
- `game_payload_core_ffi` 已新增：
  - `PayloadLogicalRawTransitionInterop`
  - `payload_core_state_store_decide_logical_raw_transition(...)`

### 本次收口
- `TryPickLogicalRawTransition(...)` 不再在 C++ 中逐个 candidate 调 `EvaluateChannelEmitDecision(...)` 并自己筛选最终 emit。
- Rust 现在直接返回：
  - 是否应 emit
  - `vkey`
  - `emit_action`
  - `projected_down_before / projected_down_after`
  - `selection_reason`
  - `transition_reason`
  - `pressed_edge / released_edge`
  - 首个 `repeat_suppressed` 诊断信息
- C++ 只负责：
  - 读取 Rust decision
  - 写 RawInput 结构
  - 记录 `[EDGE] / [EMIT] / [REPEAT]`

### 当前价值
- Raw 路径上“候选顺序 + emit 执行判断 + 诊断输出”这三层已经基本串成一条 Rust 真值链。
- `SyncMod.cpp` 在 logical raw 路径上继续退化为边界执行壳。
- 现在 Rust 已经能直接回答：
  - 最终选中了哪个键
  - 为什么选它
  - 是 press 还是 release
  - 为什么 suppress repeat

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第二十三批落地：logical raw 候选顺序与 selection reason 开始收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::runtime` 已新增：
  - `LogicalRawSelectionReason`
  - `LogicalRawCandidate`
  - `LogicalRawPlan`
  - `build_logical_raw_plan(...)`
- `game_payload_core_ffi` 已新增：
  - `PayloadLogicalRawCandidateInterop`
  - `PayloadLogicalRawPlanInterop`
  - `payload_core_build_logical_raw_plan(...)`

### 本次收口
- `TryPickLogicalRawTransition(...)` 不再在 C++ 里硬编码候选顺序：
  - `direction_release_first`
  - `logical_release`
  - `logical_press`
  - `group_winner_press`
  - `logical_emit`
- Rust 现在负责给出 logical raw 的 candidate plan：
  - 候选 `vkey`
  - `observed_down`
  - `required_action`
  - `selection_reason`
- C++ 只负责：
  - 读取 Rust plan
  - 调用现有 `EvaluateChannelEmitDecision(...)`
  - 改写 RawInput 结构
  - 输出日志与 diagnostics

### 当前价值
- Raw 路径上的候选顺序与选择原因不再散落在 `SyncMod.cpp`。
- `SyncMod.cpp` 在 logical raw 这条热路径上继续退化成：
  - 读取 snapshot
  - 调 Rust
  - 改写结构
  - 打日志
- 现在 Rust 已经覆盖了：
  - mapping selection
  - direction selection
  - logical raw candidate order
  只剩最后一层“按候选计划执行 emit 决策”仍通过 C++ 包装函数调用。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第二十二批落地：mapping / direction selection 开始收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::sync` 已新增：
  - `MappingTransitionReason`
  - `DirectionSelectionReason`
  - `MappingTransitionDecision`
  - `DirectionTransitionDecision`
  - `SyncStateStore::select_mapping_transition(...)`
  - `SyncStateStore::select_direction_transition(...)`
- `game_payload_core_ffi` 已新增：
  - `PayloadMappingTransitionDecisionInterop`
  - `PayloadDirectionTransitionDecisionInterop`
  - `payload_core_state_store_select_mapping_transition(...)`
  - `payload_core_state_store_select_direction_transition(...)`

### 本次收口
- `TryPickMappingRawTransition(...)` 不再在 C++ 里扫描 `targetMask/keyboardState` 并自己决定：
  - `mapping_edge_down`
  - `mapping_edge_up`
  - `raw_neutral_suppressed`
- `TryPickDirectionTransition(...)` 不再在 C++ 里构造方向键期望态并自己决定：
  - `preferred_release`
  - `force_release_mask`
  - `stale_raw_down`
  - `preferred_press`
  - `group_winner_press`
- 上述选择顺序、方向键对冲、scan cursor 推进与 Raw projected 更新现在都由 Rust store 驱动。

### 当前价值
- `SyncMod.cpp` 又减少了一批“自己算选择策略、自己改状态”的逻辑。
- `mapping` 与 `direction` 这两类 transition 的 selection reason 开始有统一 Rust 真值。
- 方向键期望态构造与对冲裁决不再散落在 C++ 层。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过

---

## 第二十一批落地：adapter transition reason 收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::sync` 已新增：
  - `ChannelTransitionReason`
  - `ProjectedStateUpdate.transition_reason`
- `game_payload_core::runtime` 已新增：
  - `ChannelEmitDecision.transition_reason`
- `game_payload_core_ffi` / `game_payload_core_ffi.h` 已同步新增：
  - `PayloadChannelEmitDecisionInterop.transition_reason`
  - `PayloadProjectedStateUpdateInterop.transition_reason`

### 本次收口
- Raw 通道的 `decide_channel_emit_with_store(...)` 现在会由 Rust 返回：
  - `desired_press`
  - `desired_release`
  - `blocked_release`
  - `repeat_suppressed`
- Win32 / DirectInput 的 projected update 现在会由 Rust 返回：
  - `observed_press`
  - `observed_release`
- `SyncMod.cpp` 已改为：
  - `[EMIT]` 日志直接消费 Rust `transition_reason`
  - `AdapterDiagnosticsEvent.reason_code` 对 `Raw/Win32/DirectInput` 统一写入 Rust reason
  - C++ 不再自己推断 `emit_action` 对应的原因文本

### 当前价值
- Raw / Win32 / DirectInput 的通道级 transition reason 开始共用同一套 Rust 枚举。
- diagnostics buffer 中的 `reason_code` 不再只是 `emit_action`，而是真正可解释状态变化来源的 Rust 决策结果。
- `SyncMod.cpp` 继续退化为：
  - Hook 边界
  - FFI 调用
  - 日志输出壳

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过
- `GameMasterGUI` Debug / Release 构建通过

---

## 第二十批落地：projected state 更新入口继续收口到 Rust（2026-04-07）

### 本次新增
- `game_payload_core::sync` 已新增：
  - `ProjectedStateUpdate`
  - `SyncStateStore::update_projected(...)`
- `game_payload_core_ffi` 已新增：
  - `PayloadProjectedStateUpdateInterop`
  - `payload_core_state_store_update_projected(...)`

### 本次收口
- `SyncMod.cpp` 内剩余的 projected state 更新入口开始统一走 Rust store：
  - `TryPickMappingRawTransition(...)`
  - `TryPickDirectionTransition(...)`
  - `RecordWin32KeyEventIfNeeded(...)`
  - `RecordDirectInputKeyEventIfNeeded(...)`
- `TryPickLogicalRawTransition(...)` 不再在 `decide_channel_emit_with_store(...)` 之后重复写一次 projected state。
- `emit before/after` 现在直接使用 Rust 返回的：
  - `projected_down_before`
  - `projected_down_after`
  避免 C++ 在状态已推进后再回读 store，把 `before` 记成错误值。

### 本轮修正的结构问题
- `Win32` / `DirectInput` projected state 的推进不再依赖 `key log` 开关。
- 现在即使关闭按键日志，Rust state store 仍会持续更新这两个通道的 projected 状态。
- 这样 `[OBS]` / `[RUSTDIAG]` 中的 drift 和 snapshot 才不会因为日志级别不同而失真。

### 当前价值
- `projected_before / projected_after` 的真值开始进一步从 C++ 热路径收走。
- Win32 / DirectInput 生命周期与 RawInput 一样，开始统一走 Rust store。
- `SyncMod.cpp` 继续从“自己推进状态 + 调 Rust”收缩为“调用 Rust 推进状态 + 镜像日志壳”。

### 本轮验证
已完成：

1. `cargo test -p game_payload_core`
2. `cargo clippy -p game_payload_core --all-targets --all-features -- -D warnings`
3. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v142 /m`
4. `MSBuild.exe E:\\code\\game-all\\Payload\\Payload.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v142 /m`

结果：
- Rust 测试与 clippy 通过
- Windows `Payload` Debug / Release 构建通过
