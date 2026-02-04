---
# SyncMod V2 回退方案（Payload + 控制端/配置）

## 一、背景
- 当前 V3 同步在游戏内大量丢失按键/不同步，表现为短按无响应、切前台后失效等。
- 已确认需要“先恢复可用性”，再逐步优化。

## 二、目标
- **第一阶段：完全回到 V2 行为**，确保同步稳定可用。
- **第二阶段：在 V2 核心保持不变的前提下再做优化**（本方案仅做第一阶段）。

## 三、范围与边界
### 1) 本次改动范围
- 回退 `Payload/modules/sync/SyncMod.cpp` 中 **同步核心逻辑** 至 V2。
- 回退 **控制端与配置** 至 V2（确保共享内存结构与版本一致）。
- 保留当前 **配置读取方式、日志路径、调试开关** 等非核心逻辑。

### 2) 明确不做
- 不修改 `Injector/` 代码。
- 不引入 V3 事件流相关逻辑（事件队列、内部键盘态、SRWLock 处理等）。
- 不做性能优化与结构性重构。

## 四、前置条件（重要）
- **控制端与配置必须同步回退到 V2**（共享内存名称 `Local\\DNFSyncBox.KeyboardState.V2`，版本号 `2`）。
- 若控制端仍为 V3：Payload 将无法连接共享内存，表现为“完全不同步”。
- 需要重新编译 GUI 与 Payload，确保协议一致。

## 五、回退原则
1. **快照驱动**：仅以 `keyboardState/edgeCounter/targetMask/blockMask` 输出按键状态。
2. **行为对齐**：以提交 `2abb0183d0e33846f82a37a8e9116b8acfdafa07` 的 V2 行为为对照基线。
3. **最小变更**：不影响日志路径与当前诊断配置，避免引入新变量。

## 六、实施步骤
### Step 1：控制端/协议回退到 V2
- `Shared/Protocols/` 内的同步协议说明与结构体定义回退到 V2（名称与版本一致）。
- `GUI/Modules/Sync/Core/SharedMemoryConstants.cs` 回退到 V2 的共享内存名与版本号。
- `GUI/config/MasterGuiSettings.cs` 默认 `SyncVersion=2`。
- `GUI/config/mastergui.json` 与 `config-templates/mastergui.json` 的 `SyncVersion` 回退到 `2`。

### Step 2：Payload 同步核心回退到 V2
- `kSharedMemoryName` 改回 `Local\\DNFSyncBox.KeyboardState.V2`
- `kSharedVersion` 改回 `2`
- 移除 `SharedKeyboardStateV3/InputEventV3/eventBuffer` 等结构
- `EnsureSharedMemory/ReadSharedSnapshot` 恢复为 V2 结构读法

### Step 3：移除 V3 事件流与内部状态
- 删除 `ProcessEventQueueV3`、`g_internalKeyState`、`SRWLock`、RawEvent 队列等
- `GetAsyncKeyState/GetKeyboardState/DirectInput/RawInput` 全部回到“快照输出”

### Step 4：保留现有日志/配置框架
- 保留当前日志目录：`logs/sync/payload`
- 保留 `sync_debug.ini` 与诊断开关结构（但不影响 V2 主路径）

### Step 5：编译与验证
- 以 **Win32 Debug** 编译 payload
- 重新编译 GUI（net8.0-windows）
- 在控制端切回 V2 后进行验证

## 七、验收标准（必须满足）
1. **稳定性**：持续运行 30 分钟以上无明显丢同步
2. **前台切换**：主控/从控切换后按键仍能稳定同步
3. **短按连击**：快速点击（如 W/A/S/D 连点）不丢边沿
4. **日志**：Payload 日志中不出现“共享内存版本不一致”错误

## 八、风险与对策
- **风险**：控制端仍为 V3 导致 Payload 连接失败
  - **对策**：控制端/配置/协议统一回退到 V2
- **风险**：回退过程中误删非核心逻辑
  - **对策**：以 `2abb0183...` 为基线比对，仅替换同步核心

## 九、回滚策略
- 若回退后异常，可通过 Git 直接回滚至当前版本
- 改动涉及 Payload + GUI + Shared/Protocols，回滚需同步恢复这些文件

## 十、后续优化路线（下一阶段）
- 只在 V2 稳定后推进，且 **必须可开关**：
  1. 增加诊断统计（默认关闭）
  2. 快照读缓存（毫秒级）
  3. RawInput 兜底策略与粘键处理优化

---
