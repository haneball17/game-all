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
