# SyncMod 输入状态覆盖实现方案（参考 dnfinput.cpp）

> 目标：在**保持按键映射**的前提下，彻底消除 KeyUp 丢失导致的“粘键/持续移动”。
> 核心策略：**在被注入进程内改为“状态驱动覆盖”**，不要依赖主控的 KeyDown/KeyUp 事件完整性。

## 1. 背景与问题
现状：
- 高速输入（方向键）时，从控出现“持续移动”，日志显示主控已抬起但从控仍保持按下。
- 事件驱动链条：`Hook -> GUI -> 共享内存 -> Payload 注入`，其中任何环节丢失 KeyUp 都会造成快照停留。

参考实现（`references/dnf-syn/dnfinput/src/dnfinput.cpp`）稳定原因：
- 在目标进程内 Hook 多条输入 API（Win32/RawInput/DirectInput/消息泵）。
- **每次 API 调用时都用共享内存状态重写结果**（状态驱动），不依赖事件完整性。
- 有**清键/失联兜底**，避免旧状态长时间残留。

## 2. 方案概述
在 `Payload/modules/sync/SyncMod.cpp` 内实现“状态覆盖 + 清键兜底”的输入伪造模型：

1) **Win32 输入覆盖**
- `GetAsyncKeyState / GetKeyboardState` 返回共享内存快照中的状态。

2) **RawInput 输入覆盖**
- `GetRawInputData / GetRawInputBuffer` 必须**强制重写** Make/Break，确保 RawInput 通道也保持一致。
- Mapping 模式下通过 **快照状态驱动**生成目标键序列，而不是依赖事件队列。

3) **DirectInput 输入覆盖**
- `GetDeviceState / GetDeviceData` 使用快照状态改写，避免 DInput 走“真实键盘”导致脱离同步。

4) **失联/暂停清键**
- 心跳超时或暂停时强制清键（RawInput/Win32/DI 一致），避免残留按下。

## 3. 关键改动点清单（文件/函数级别）
### 3.1 Payload/modules/sync/SyncMod.cpp
**A. 共享快照读取与清键兜底**
- `ApplyClearIfNeeded`：确保每次读取快照后都执行清键逻辑（Win32/DI/RawInput 全覆盖）。
- `ApplyRawClearIfNeeded`：RawInput 通道的专用清键，防止 RawInputBuffer 侧残留按下。
- **调整心跳超时**：将 `kDefaultSharedTimeoutMs` 建议从 500ms 降到 200ms（与参考一致）。

**B. RawInput 覆盖逻辑（核心）**
- `TryPickMappingRawKey`：
  - 改为**状态驱动扫描**（参考 `dnfinput.cpp`），直接用 `snapshot.keyboardState` 与 `g_lastRawKeyboardState` 对比。
  - 不再依赖事件队列或 edgeCounter 的复杂组合，避免遗漏 KeyUp。
  - 若无变化：
    - 优先复用仍按下的目标键，保持连续移动；
    - 若没有按下键，仍输出一个目标键的抬起，防止穿透。

- `Hook_GetRawInputBuffer` / `Hook_GetRawInputData`：
  - Mapping 模式下优先走 `TryPickMappingRawKey` 输出目标键事件。
  - **无论原始 RawInput 状态是否一致，直接重写 Make/Break**，确保从控始终与快照一致。
  - 非 Mapping 模式下：若目标键处于同步范围，**强制按快照状态修正 Make/Break**。

**C. Win32 API 覆盖**
- `Hook_GetAsyncKeyState` / `Hook_GetKeyboardState`：
  - 当 `snapshot.targetMask[vKey] != 0` 时，直接以快照状态覆盖结果。
  - 非目标键仅在 `ShouldBlockKey` 时强制抬起。

**D. DirectInput 覆盖**
- `Hook_GetDeviceState` / `Hook_GetDeviceData`：
  - 将 DIK 状态与快照完全对齐。
  - 对目标键强制设置按下/抬起，与 Win32/RawInput 保持一致。

## 4. 关键设计理由（与问题对应）
| 问题 | 对应策略 | 解释 |
| --- | --- | --- |
| KeyUp 丢失导致粘键 | 状态驱动覆盖 | 每次 API 调用都重写结果，不依赖事件是否完整 |
| RawInput 通道不同步 | RawInput 强制重写 | 游戏走 RawInput 时也能同步 |
| DirectInput 通道不同步 | Hook DI | DNF 使用 DI 读状态，必须强制覆盖 |
| 共享内存迟滞 | 心跳超时清键 | 旧状态不会长时间残留 |

## 5. 风险与控制
**风险**：覆盖面大，可能导致输入延迟或异常。 
**控制策略**：
- 仅在“同步有效”时覆盖；暂停/失联立即清键。
- 记录覆盖统计与原始输入计数，便于回溯。
- 显式绕过 `activePid`（主控进程不被覆盖）。

## 6. 回滚策略
- 保留旧的“事件驱动”分支（编译宏或配置开关）。
- 如出现异常，可切换为旧逻辑进行对照。 

## 7. 验证清单
1) 高频方向键操作 10s 内不再粘键。
2) Mapping 模式下映射键仍能稳定输出（Q→Oem4 等）。
3) 暂停/失联时，所有按键立即抬起。
4) DInput/RawInput/Win32 三通道日志一致（对照 `latest.log`）。

---

## 8. 后续实施顺序（建议）
1) 先在 RawInput 路径落地状态驱动（影响最大）。
2) 再统一 Win32 API 返回逻辑（GetAsync / GetKeyboardState）。
3) 最后补齐 DirectInput 的状态对齐。

> 以上方案严格对齐 `references/dnf-syn/dnfinput/src/dnfinput.cpp` 的行为模式，确保 KeyUp 不再依赖事件流。
