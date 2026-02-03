这是一个基于 **无锁环形缓冲区（Lock-Free Ring Buffer）** 和 **事件流（Event Sourcing）** 的重构方案。

该方案旨在彻底解决“吞键（长按松开后角色继续跑）”、“时序错乱”问题，并显著降低同步延迟。

## ✅ 优化版修订要点（结合当前项目实现）

> 以下为针对现有实现的修订建议（已确认采用 V3）：

1. **事件流写入目标键**  
   Mapping 逻辑在 GUI 端完成翻译，事件流只写入“目标键”。  
   这样从控只需回放目标键，不再处理映射源键，从根源降低 KeyUp 丢失风险。

2. **单消费者模型（禁止 thread_local）**  
   Hook 触发线程多，thread_local 会导致重复消费/错序消费。  
   必须改为 **进程内唯一 ReadIndex**，并加轻量锁保护。

3. **RawInput 回放策略保守**  
   不使用 PostMessage 强驱动队列消费，避免死循环或消息失效。  
   若队列积压，仍可通过 GetDeviceState/KeyboardState 反映最终状态。

4. **事件源可先用 KeyboardHook**  
   当前阶段可沿用 KeyboardHook 作为输入来源；  
   RawInput 可作为后续升级项，不影响 V3 协议落地。

5. **保留心跳对齐作为 KeyUp 兜底**  
   即使事件流漏掉抬起，心跳轮询仍能补发 KeyUp，避免粘键。

---

## ✅ 现网诊断结论与补强（基于最新日志）

> 结论与方案对齐，用于指导后续实现优先级与风险控制。

1. **RawInputData 为主通道**  
   单键测试日志显示方向键、A/D/Q 等均通过 RawInputData 进入；  
   Win32 主要是轮询通道，DirectInput 未成为主路径。

2. **KeyUp 缺失多为“长按误报”**  
   大量 `[KEYWARN] missing_keyup` 在长按期间触发，随后仍有 `down=0`；  
   需在方案中加入“长按刷新 down tick + KeyUp 超时可配置”机制，避免误判。

3. **同步异常优先修 RawInput 事件完整性**  
   因主通道已确认，修复重点应落在 RawInput 事件流对齐、KeyUp 补偿与心跳兜底；  
   Win32/DirectInput 只作为辅助兜底，而非主路径。

4. **诊断日志必须纳入方案**  
   已新增 `[KEY]/[KEYWARN]/[DIAG]` 与 KeyLog 配置项，  
   作为方案落地的验收依据（通道判断、KeyUp 漏失、补偿生效）。

建议在后续实现时，优先完成：  
**“RawInput 事件补偿 + 心跳对齐修正 + KeyUp 超时策略”**，再扩展 Win32/DI 兜底逻辑。

---

# SyncMod 核心重构方案：基于事件流的高性能同步

## 1. 核心设计理念变更

| **特性**         | **旧架构 (Snapshot V2)** | **新架构 (EventStream V3)**        |
| ---------------------- | ------------------------------ | ---------------------------------------- |
| **通讯模式**     | 状态快照覆盖 (State Overwrite) | **事件流队列 (Event Queue)**       |
| **数据一致性**   | 弱一致性 (竞态丢键)            | **强一致性 (严格时序回放)**        |
| **多客户端支持** | 所有客户端读同一状态           | **单生产者-多消费者 (SPMC)**       |
| **延迟处理**     | 依赖轮询频率 (1ms+)            | **零等待 (RawInput直通)**          |
| **防卡死机制**   | 依赖心跳超时                   | **快照看门狗 (Snapshot Watchdog)** |

---

## 2. 协议层重构 (SharedMemory Protocol)

我们需要在共享内存中开辟一块区域作为“磁带”，记录每一个按键动作。

### 2.1 数据结构定义

 **文件修改目标** : `SharedMemoryConstants.cs`, `SharedKeyboardState.cs`

**C#**

```
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct InputEvent
{
    public uint SequenceId;    // 全局唯一递增序号，用于检测丢包/重置
    public byte VirtualKey;    // 虚拟键码 (0-255)
    public byte IsDown;        // 1=按下, 0=抬起
    public byte Padding1;      // 对齐
    public byte Padding2;      // 对齐
    public long Timestamp;     // QPC 高精度时间戳 (用于回放延时补偿)
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct SharedMemoryLayoutV3
{
    // --- 头部信息 ---
    public uint Version;          // 协议版本 (3)
    public uint Magic;            // 校验魔数
    public volatile int WriteHead;// 生产者写指针 (只会增加，不回绕，取模使用)
    public uint BufferCapacity;   // 缓冲区大小 (建议 4096)

    // --- 全局状态快照 (保留作为看门狗/初始同步) ---
    // 即使使用事件流，仍需保留一份当前状态，
    // 用于新启动的游戏客户端初始化，或作为纠错兜底。
    public SharedKeyboardStateV2 Snapshot; 

    // --- 事件环形缓冲区 ---
    // 实际大小由 BufferCapacity 决定，定长数组
    public fixed byte EventBuffer[1]; // 占位符，实际大小 = sizeof(InputEvent) * Capacity
}
```

### 2.2 内存布局逻辑

* **生产者 (C#)** ：维护本地 `WriteIndex`，写入 `EventBuffer[WriteIndex % Capacity]`，然后原子递增 `Shared->WriteHead`。
* **消费者 (C++ DLL)** ：每个注入的 DLL 维护自己**私有**的 `LocalReadIndex`。
* 当 `LocalReadIndex < Shared->WriteHead` 时，循环读取并回放事件。
* **防溢出机制** ：如果 `Shared->WriteHead - LocalReadIndex > Capacity`，说明客户端卡顿太久导致缓冲区被覆盖。此时客户端必须 **丢弃所有积压事件** ，直接从 `Snapshot` 重置状态，并将 `LocalReadIndex` 追平到 `WriteHead`。

---

## 3. 控制端重构 (GUI / C#)

 **目标** ：从 `KeyboardHook` (低速/易被拦截) 切换到 `RawInput` (高速)，并写入事件流。

### 3.1 引入 RawInput 监听

在 `SyncController.cs` 中，移除 `KeyboardHook`，改用 `RawInput`。可以使用 `SharpDX.RawInput` 或 P/Invoke 实现。

**C#**

```
// 伪代码示例
private void OnRawInput(RawInputEventArgs e)
{
    // 1. 过滤：只处理键盘消息
    if (e.Type != DeviceType.Keyboard) return;
  
    // 2. 转换：获取 VKCode 和 状态 (Make/Break)
    int vk = e.Keyboard.VirtualKey;
    bool isDown = (e.Keyboard.Flags & KeyState.Break) == 0;
  
    // 3. 写入事件流 (关键路径)
    _sharedMemoryWriter.PushEvent(vk, isDown, Stopwatch.GetTimestamp());
  
    // 4. 更新快照 (作为兜底)
    _keyStateTracker.Update(vk, isDown);
    _sharedMemoryWriter.UpdateSnapshot(...); 
}
```

### 3.2 改造 SharedMemoryWriter

 **文件修改目标** : `SharedMemoryWriter.cs`

新增 `PushEvent` 方法，实现无锁写入。

**C#**

```
public unsafe void PushEvent(int vk, bool isDown, long timestamp)
{
    // 获取当前写位置
    var layout = (SharedMemoryLayoutV3*)_basePtr;
    var currentHead = layout->WriteHead; // 读取 volatile
  
    // 计算环形缓冲区偏移
    var capacity = layout->BufferCapacity;
    var offset = currentHead % capacity;
  
    // 定位到具体的 Event 结构体内存地址
    var eventPtr = (InputEvent*)(_basePtr + _eventBufferOffset + (offset * sizeof(InputEvent)));
  
    // 写入数据
    eventPtr->SequenceId = (uint)currentHead;
    eventPtr->VirtualKey = (byte)vk;
    eventPtr->IsDown = (byte)(isDown ? 1 : 0);
    eventPtr->Timestamp = timestamp;
  
    // 内存屏障，确保数据写入完成后再更新 Head
    Thread.MemoryBarrier();
  
    // 原子递增写指针，通知所有消费者有新消息
    Interlocked.Increment(ref layout->WriteHead);
}
```

---

## 4. 注入端重构 (Payload / C++)

 **目标** ：从“读取状态”改为“消费事件”。

### 4.1 消费者逻辑实现

 **文件修改目标** : `SyncMod.cpp`

新增一个内部函数 `ProcessEventQueue()`，在每次 Hook 回调（如 `GetDeviceState` 或 `GetRawInputData`）的开头调用。

**C++**

```
// 线程局部变量，每个游戏线程维护自己的读取进度
static thread_local int g_localReadIndex = -1; 

void ProcessEventQueue()
{
    SharedMemoryLayoutV3* layout = GetSharedMemory();
  
    // 1. 初始化检查
    if (g_localReadIndex == -1) {
        // 首次连接，直接跳到最新，并从快照同步状态
        g_localReadIndex = layout->WriteHead;
        SyncFromSnapshot(layout->Snapshot);
        return;
    }

    int currentHead = layout->WriteHead;
  
    // 2. 溢出检查 (客户端卡死过久)
    if (currentHead - g_localReadIndex >= layout->BufferCapacity) {
        Log(L"Buffer overflow, resetting state.");
        g_localReadIndex = currentHead;
        SyncFromSnapshot(layout->Snapshot); // 强制重置
        return;
    }

    // 3. 消费循环
    while (g_localReadIndex < currentHead) {
        int offset = g_localReadIndex % layout->BufferCapacity;
        InputEvent* evt = GetEventPtr(layout, offset);
      
        // --- 核心：回放事件 ---
        // 这里不再依赖 IsSnapshotAlive 的状态覆盖，而是动作执行
        ApplyKeyEvent(evt->VirtualKey, evt->IsDown);
      
        g_localReadIndex++;
    }
}
```

### 4.2 状态维护与按键模拟

在 `SyncMod.cpp` 中维护一个 `g_internalKeyState[256]`。

* `ApplyKeyEvent` 函数不做系统 API 调用，而是更新 `g_internalKeyState` 数组。
* `Hook_GetDeviceState` 和 `Hook_GetRawInputData` 直接返回 `g_internalKeyState` 中的数据。
* **解决吞键问题** ：因为 `ApplyKeyEvent` 是循环执行的。如果在一次 Hook 调用中，队列里有 `Down -> Up` 两个事件：
* 循环 1：`g_internalKeyState` 变 Down。
* 循环 2：`g_internalKeyState` 变 Up。
* **关键点** ：对于 `GetRawInputData` (基于事件的 API)，我们需要在循环中**生成对应的 RawInput 消息**并塞给游戏。
* 对于 `GetDeviceState` (基于状态的 API)，则只返回最终状态。

### 4.3 针对 RawInput 的特殊处理

`GetRawInputData` 是事件驱动的。如果在 `ProcessEventQueue` 中发现有新的事件，我们需要修改当前的 `RAWINPUT` 结构体来欺骗游戏。

* **难点** ：一次 `GetRawInputData` 调用只能返回一个事件。
* **策略** ：如果队列里积压了多个事件，我们在本次 Hook 调用中返回**第一个**事件，并在 Hook 函数结束前，**手动 PostMessage(WM_INPUT)** 给自己窗口，触发下一次 Hook 调用，直到队列消费完毕。这样可以瞬间排空积压的事件，且不丢失任何中间状态。

---

## 5. 实施步骤

1. **协议升级** ：

* 新建 `SharedMemoryConstantsV3.cs`，定义新的结构体。
* 确保 C# 和 C++ 的结构体 `Pack=1` 对齐完全一致。

1. **C# 端改造** ：

* 修改 `SharedMemoryWriter`，分配更大的内存空间（Header + Snapshot + Events）。
* 在 `SyncController` 中接入 `RawInput` 库。
* 实现 `PushEvent` 逻辑。

1. **C++ 端改造** ：

* 修改 `SyncMod.cpp` 的 `EnsureSharedMemory`，适配 V3 结构。
* 实现 `ProcessEventQueue` 逻辑。
* 重写 `Hook_GetRawInputData`：
  * 优先检查 `EventBuffer`。
  * 如果有事件，填充 `RAWINPUT` 数据，返回成功。
  * 如果无事件，返回 `Snapshot` 的状态或原始输入（取决于是否是 Master）。

1. **调试与验证** ：

* 开启 `DebugFileLogger`。
* 在 C# 端狂按键盘（制造高并发事件）。
* 在 C++ 端验证 `SequenceId` 是否连续，确保无丢包。

## 6. 预期收益

* **按键零丢失** ：环形缓冲区保证了即使物理 Up 事件在 0.1ms 内发生，也会被记录并被游戏回放。
* **连发完全一致** ：主窗口的连发器产生多少次 Down/Up，共享内存就记录多少次，从窗口完全复刻，不再出现“连发只按一次”的问题。
* **超低延迟** ：RawInput + 无锁队列去除了 Windows 消息泵和快照轮询的开销，延迟可降低至 1-3ms 级别。
