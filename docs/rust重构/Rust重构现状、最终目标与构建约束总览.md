# Rust 重构现状、最终目标与构建约束总览

## 1. 文档目的
本文档用于统一说明以下内容：

- 当前项目已经重构到什么阶段
- 最终重构目标是什么
- 为什么采用“Rust 负责核心真值、C++ 负责边界、C# 负责界面”的架构
- 为什么默认运行产物应使用 `Release`
- `Debug` 是否可能放大崩溃风险

后续所有 Rust / 重构相关文档，统一放在 `docs/rust重构/` 目录下。

---

## 2. 当前项目现状

### 2.1 当前整体阶段
项目当前已经脱离“先修构建链”的阶段，进入“按模块把核心真值收口到 Rust”的阶段。

已经完成的关键基础工作：

- `Injector` 已开始由 Rust 承接配置、运行计划、watch/retry 与成功判定逻辑
- `Sync` 已完成中段 Rust 化，开始从 C++ 热路径中移出核心状态机
- GUI 与 `Injector.vcxproj` 的构建图已解耦
- Windows `v142` 构建链已恢复到可持续开发状态

### 2.2 当前仍是重逻辑中心的代码
当前仓库里后续必须继续收口的几个核心文件：

- `Injector/main.cpp`
- `Payload/modules/sync/SyncMod.cpp`
- `Payload/modules/helper/HelperMod.cpp`
- `GUI/Modules/Sync/Core/SyncController.cs`

当前大致体量：

- `Injector/main.cpp`：约 1420 行
- `SyncMod.cpp`：约 5198 行
- `HelperMod.cpp`：约 3337 行
- `SyncController.cs`：约 774 行

### 2.3 当前已经完成的 Rust 化重点

#### Injector
已经进入 Rust 的内容：

- 默认配置与配置模板
- 运行计划
- watch_mode 状态机
- retry 状态机
- success / heartbeat 判定的大部分真值
- `inject_backend` / `success_observer_mode` 配置语义

#### Sync
已经进入 Rust 的内容：

- runtime / key / path decision
- state store
- projected state update
- pause / clear
- path observation
- drift summary
- diagnostics snapshot / event buffer
- transition reason
- mapping / direction selection strategy
- logical raw candidate planning

#### 仍未完成的部分

- `SyncMod.cpp` 仍然偏厚，Hook 热路径和部分执行包装仍在 C++
- `Injector/main.cpp` 还没有完全压成平台适配壳
- `HelperMod.cpp` 还没有真正拆层
- `SyncController.cs` 的同步内核还没迁入独立 Rust crate

---

## 3. 最终重构目标

最终目标不是“把 C++ 全部改成 Rust”，而是形成如下分层：

- `Rust`：核心真值层
- `C++`：Win32 / Hook / 注入 / 游戏 ABI 边界层
- `C#`：GUI 与交互层

### 3.1 Rust 最终负责什么

- `Injector` 的配置、运行计划、等待/重试、成功判定、诊断
- `Sync` 的逻辑键态、projected state、路径判断、pause/clear、selection/transition、diagnostics
- 控制端的键态推进、snapshot 构建、heartbeat、foreground grace
- `Helper` 的配置、共享内存、后台状态机
- 协议镜像、结构布局校验、版本一致性检查

### 3.2 C++ 最终负责什么

- `DllMain`
- MinHook / trampoline / Hook 安装
- RawInput / DirectInput / Win32 API 真正调用
- DLL 注入动作
- 游戏 patch、裸地址访问、汇编桥
- 薄日志壳与 FFI 调用层

### 3.3 C# 最终负责什么

- WPF 界面
- 配置编辑
- 状态展示
- 诊断导出
- 用户交互

换句话说，最终目标是：

**Rust 负责“怎么判断”，C++ 负责“怎么动手”，C# 负责“怎么展示”。**

---

## 4. 为什么这样设计

### 4.1 不是为了“语言更高级”
这个项目当前最麻烦的问题不是语法层面，而是：

- 同一语义在多个模块各算一遍
- 行为与诊断不在同一套模型里
- 多开时序很脆弱
- 出问题经常只能靠大量日志猜

所以重构目标不是“全面换语言”，而是“统一真值来源”。

### 4.2 为什么 Rust 适合做核心真值
因为 Rust 更适合承载：

- 明确输入
- 明确状态
- 明确输出
- 明确边界
- 明确测试

它最适合解决的是：

- 状态机一致性
- 协议一致性
- 诊断结构化
- 高频输入与多路径同步中的状态漂移问题

### 4.3 为什么 C++ 不能完全拿掉
因为项目有一大批逻辑天然是平台边界逻辑：

- `DllMain`
- Hook 安装
- MinHook / trampoline
- RawInput / DirectInput / Win32
- DLL 注入
- 游戏内存 patch
- 汇编桥

这些逻辑不是不重要，而是不适合成为“核心真值中心”。

### 4.4 为什么 GUI 继续保留在 C#
因为 GUI 当前的主要问题不是语言，而是职责边界。

WPF 继续保留在 C# 的好处：

- 不需要重写界面资产
- 不引入新的 GUI 技术栈风险
- 只需要把同步/控制内核迁出，而不是重写展示层

---

## 5. 默认构建与运行约束

### 5.1 默认产物应为 Release
默认运行产物应统一使用：

- `Release`

原因：

- 本项目是注入式、Hook 式、跨模块、跨运行库、强时序依赖程序
- 对这种项目来说，`Debug` 不只是“慢一些”，还会改变时序、CRT、优化和内存布局
- 多开场景下，时序变化本身就可能放大脆弱问题

所以：

- 日常运行默认使用 `Release`
- 实机稳定性验证默认以 `Release` 为准

### 5.2 Debug 的定位
`Debug` 应只用于：

- 本地单步
- 加日志
- 定点排查
- 验证某个具体状态机问题

不应作为默认多开运行产物。

---

## 6. Debug 是否可能引发或放大崩溃

结论：

- `Debug` 不一定是崩溃的根因
- 但 `Debug` 的确更容易放大崩溃风险

### 6.1 原因一：时序更容易暴露边界问题
当前项目最脆弱的边界包括：

- `DllMain -> CreateThread`
- APC 注入
- RawInput / DirectInput / Win32 多路径 Hook
- 注入后短时间内的大量线程与 Hook 初始化

这些本来就是时序敏感区。`Debug` 让它们更容易暴露问题。

### 6.2 原因二：当前 Debug 存在已知运行库风险
`Payload/Payload.vcxproj` 当前配置是：

- `Debug`：`RuntimeLibrary=MultiThreadedDebug` (`/MTd`)
- `Release`：`RuntimeLibrary=MultiThreaded` (`/MT`)

但在 `v142` 兼容路径下，`Debug` 构建仍会优先链接现成的 `Release` 版 MinHook 静态库：

- `Payload/lib/minhook/build/VC16/lib/Release`
- `Payload/lib/minhook/lib/Release`

因此当前 `Debug` 已知会出现：

- `LNK4075`
- `LNK4098`

其中 `LNK4098` 表示默认运行库与其他库使用的运行库存在冲突。

所以：

- `Debug` 态不是“必崩”
- 但它比 `Release` 更容易放大运行库不一致、时序和注入边界问题

### 6.3 实用结论

- 默认运行：`Release`
- 默认实机稳定性验证：`Release`
- `Debug`：只作为诊断/排查用途

---

## 7. 当前最关键的结构性问题

当前项目里，最可能和多开偶发崩溃、输入异常、状态漂移相关的高风险点包括：

- `DllMain` 生命周期边界
- APC 注入不确定性
- RawInput 混合拦截、读取与改写
- 注入后每个进程都要快速启动大量线程与 Hook
- Helper/Sync 同时对目标进程做较强侵入

这也是为什么后续重构优先级必须是：

1. 先把 `Sync` 真值继续收口
2. 再把 `Injector` 真值和平台边界彻底拆开
3. 然后再处理控制端与 `Helper`

---

## 8. 当前最近阶段之后的主线任务

当前主线仍然是：

### 第一优先级：继续完成 Sync
目标：

- 让 `SyncMod.cpp` 在 Raw 路径上尽量只剩：
  - 读取 snapshot
  - 调 Rust
  - 改写 RawInput / Win32 / DI 结构
  - 打日志和 diagnostics

### 第二优先级：完成 Injector Phase 1 收尾
目标：

- 把 `Injector/main.cpp` 压成平台适配器壳
- 让 success / retry / finish 真值完全留在 Rust

### 第三优先级：新增 `game_control_core`
目标：

- 把 `SyncController.cs` 中的控制内核迁出 GUI

### 第四优先级：拆层 Helper
目标：

- 把配置、共享内存、后台状态机从 `HelperMod.cpp` 中迁出

### 第五优先级：协议校验收口
目标：

- 建立三端 version / size / offset 自动校验

---

## 9. 后续文档约定

从本文件开始，后续所有重构相关文档统一放在：

- `docs/rust重构/`

包括但不限于：

- 总体计划
- 阶段性收口计划
- 稳定性专项文档
- Debug / Release 约束文档
- Injector / Sync / Helper / Control Core 专项重构文档

旧文档保留，不强制迁移；但新增文档不再散落到 `docs/` 根目录。
