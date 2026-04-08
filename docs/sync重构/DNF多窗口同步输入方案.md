# DNF 多窗口输入同步器 — 技术方案

> **版本**：v1.0 · 2026-04-08
> **目标环境**：2012 年台服基底 DNF（无 TP/TenProtect 反作弊），Windows 单机多开
> **实现语言**：Rust 为主（DLL、控制器、GUI），必要时 C++ 辅助

---

## 目录

1. [方案概述](#1-方案概述)
2. [架构设计](#2-架构设计)
3. [已确认的技术选型](#3-已确认的技术选型)
4. [项目结构](#4-项目结构)
5. [共享内存数据结构](#5-共享内存数据结构)
6. [阶段一：MVP Demo（2 窗口验证）](#6-阶段一mvp-demo2-窗口验证)
7. [阶段二：完整 Hook 链](#7-阶段二完整-hook-链)
8. [阶段三：控制器完善](#8-阶段三控制器完善)
9. [阶段四：Tauri GUI](#9-阶段四tauri-gui)
10. [关键依赖](#10-关键依赖)
11. [验证计划](#11-验证计划)
12. [参考资料](#12-参考资料)

---

## 1. 方案概述

### 目标

在同一台 Windows 机器上实现多个 DNF 窗口的**键盘输入同步**。用户操作一个主控窗口，其他窗口同步接收相同的按键输入。

### 核心思路

1. **注入 DLL** 到每个 DNF 进程（APC 注入，注入器已有）
2. **Hook DirectInput** 的 `GetDeviceState`，在返回的键盘状态缓冲区中注入伪造输入
3. **Hook 焦点 API**，让后台窗口"以为"自己是前台
4. **Hook WndProc**，屏蔽窗口停用/失焦消息
5. 通过**命名共享内存**在控制器和各 DLL 间传递键盘状态
6. **Tauri GUI** 控制同步的启动/停止

### 前提条件

- 2012 年台服基底 DNF，无 TP/TenProtect 反作弊
- DLL 注入和 API Hook 不受阻碍
- 所有窗口在同一台 Windows 物理机上

---

## 2. 架构设计

```
┌──────────────────────────────────────────────────────────────────┐
│  dnf-sync-ui (Tauri v2)                                          │
│  ├── 启动/停止同步                                                │
│  ├── 显示注入状态                                                 │
│  └── 调用 dnf-sync-controller (Sidecar)                          │
├──────────────────────────────────────────────────────────────────┤
│  dnf-sync-controller (Rust console app → Tauri Sidecar)          │
│  ├── 检测 DNF 窗口（FindWindow / EnumWindows）                    │
│  ├── 调用 APC 注入器注入 DLL 到各 DNF 进程                        │
│  ├── 全局键盘钩子捕获输入 → 写入共享内存                           │
│  ├── 检测前台窗口是否为 DNF → 非法窗口时停止同步                    │
│  └── 通过 stdout (JSON) 与 Tauri 通信                            │
├──────────────────────────────────────────────────────────────────┤
│  dnf-sync-dll (Rust cdylib → 注入到 DNF 进程)                    │
│  ├── Hook GetForegroundWindow/GetFocus → 返回自身 HWND           │
│  ├── Hook WndProc → 屏蔽 WM_KILLFOCUS/WM_ACTIVATE(WA_INACTIVE)  │
│  ├── Hook DirectInput GetDeviceState (retour) → 从共享内存读按键  │
│  ├── Hook SetCooperativeLevel → DISCL_BACKGROUND                  │
│  └── 后台线程：打开共享内存，读取控制器发来的键盘状态              │
├──────────────────────────────────────────────────────────────────┤
│  Windows Named Shared Memory ("Local\DNFSyncInput")              │
│  └── SyncInputState { active, keys[256], timestamp, ... }        │
│  Windows Named Mutex ("Local\DNFSyncMutex")                      │
└──────────────────────────────────────────────────────────────────┘
```

### 数据流

```
用户键鼠输入
    │
    ▼
┌─────────────────────────────┐
│ Controller (WH_KEYBOARD_LL) │
│ 捕获 vkCode + scanCode       │
│ MapVirtualKeyExW → DIK 码   │
│ 写入共享内存                  │
└─────────────┬───────────────┘
              │ Named Shared Memory
     ┌────────┼────────┐
     ▼        ▼        ▼
┌────────┐┌────────┐┌────────┐
│DNF DLL1││DNF DLL2││DNF DLLn│
│Hook:    ││Hook:    ││Hook:    │
│GetDevice││GetDevice││GetDevice│
│State    ││State    ││State    │
│← 读共享 ││← 读共享 ││← 读共享 │
│  内存   ││  内存   ││  内存   │
└────────┘└────────┘└────────┘
```

---

## 3. 已确认的技术选型

| 决策项 | 选择 | 理由 |
|---|---|---|
| DirectInput Hook | **retour inline hook** | Rust 原生库，自动处理指令重定位；hook 函数本身（非 vtable 指针），对所有设备实例生效 |
| DLL 入口 | **自定义 DllMain** | APC 注入后 LoadLibrary 触发时立即执行，时机最精确 |
| 共享内存 IPC | **windows crate 直调** | CreateFileMappingW + CreateMutexW，零额外依赖，完全控制 |
| 键盘捕获 | **WH_KEYBOARD_LL** | 回调直接拿到 scanCode，延迟最低，能捕获所有键盘输入 |
| GUI 框架 | **Tauri v2** | Rust 原生后端，轻量前端，支持 Sidecar 管理子进程 |

### DirectInput Hook 实现要点

retour hook 的是 `GetDeviceState` 在 dinput8.dll 中的函数代码（通过创建临时设备获取函数地址），不是替换 vtable 指针。这意味着**所有设备实例**的 GetDeviceState 调用都会经过 hook，在我们的场景下这是优势——无需逐个设备 hook。

```rust
// retour static_detour 宏定义 hook
static_detour! {
    static GetDeviceStateHook: unsafe extern "system" fn(*mut c_void, u32, *mut c_void) -> i32;
}
```

### WH_KEYBOARD_LL 安装注意事项

controller 作为 .exe 安装 WH_KEYBOARD_LL 时，钩子回调依赖 Windows 消息泵运行。需要在 controller 中启动一个隐藏窗口 + GetMessage 循环来驱动钩子回调。

---

## 4. 项目结构

```
dnf-sync/
├── Cargo.toml                    # workspace root
├── crates/
│   ├── dnf-sync-dll/             # 注入 DLL (cdylib)
│   │   ├── Cargo.toml
│   │   ├── src/
│   │   │   ├── lib.rs            # DllMain 入口 + 工作线程
│   │   │   ├── hooks.rs          # 焦点 API Hook (retour)
│   │   │   ├── dinput.rs         # DirectInput GetDeviceState Hook
│   │   │   ├── wndproc.rs        # WndProc 子类化（屏蔽停用消息）
│   │   │   ├── shared_mem.rs     # 共享内存读取
│   │   │   └── types.rs          # 内部类型定义
│   │   └── build.rs
│   ├── dnf-sync-controller/      # 控制器 (bin)
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── main.rs           # 入口 + 消息循环
│   │       ├── keyboard_hook.rs  # WH_KEYBOARD_LL 全局键盘钩子
│   │       ├── process.rs        # DNF 进程检测 + 注入调度
│   │       ├── shared_mem.rs     # 共享内存写入端
│   │       └── monitor.rs        # 前台窗口监控
│   ├── dnf-sync-common/          # 共享类型定义（DLL 和 Controller 共用）
│   │   ├── Cargo.toml
│   │   └── src/
│   │       └── lib.rs            # SyncInputState 结构体 + 常量
│   └── dnf-sync-ui/              # Tauri v2 应用
│       ├── Cargo.toml
│       ├── tauri.conf.json
│       ├── capabilities/
│       │   └── default.json      # Shell 插件权限配置
│       ├── src-tauri/
│       │   └── src/
│       │       └── lib.rs        # Tauri commands
│       └── src/                  # 前端 (HTML/JS/CSS)
│           ├── main.js
│           └── styles.css
└── injector/                     # APC 注入器 (已有，用户代码)
```

---

## 5. 共享内存数据结构

`SyncInputState` 定义在 `dnf-sync-common` crate 中，DLL 和 Controller 共用。

```rust
/// 共享内存布局：Controller 写入，各 DLL 读取
/// 总大小约 620 字节，一页内存足够
#[repr(C)]
pub struct SyncInputState {
    pub magic: u32,              // 0x444E4653 ("DNFS") 校验值
    pub version: u32,            // 结构体版本号，DLL/Controller 版本不匹配时拒绝操作
    pub active: u32,             // 1=同步中, 0=停止
    pub source_hwnd: u64,        // 主控窗口 HWND（DLL 侧据此判断是否为自己，避免回写）
    pub keyboard: [u8; 256],     // DirectInput 扫描码格式，0x80=按下，0x00=松开
    pub mouse_x: i32,            // 鼠标增量 X
    pub mouse_y: i32,            // 鼠标增量 Y
    pub mouse_buttons: u8,       // 位图: bit0=左键, bit1=右键, bit2=中键
    pub mouse_scroll: i32,       // 滚轮增量
    pub tick: u64,               // 更新计数器（DLL 侧用此判断数据新鲜度）
    pub _reserved: [u8; 128],    // 预留空间
}

// 共享内存和互斥体的名称常量
pub const SHMEM_NAME: &str = "Local\\DNFSyncInput";
pub const MUTEX_NAME: &str = "Local\\DNFSyncMutex";
pub const SHMEM_SIZE: usize = std::mem::size_of::<SyncInputState>();
pub const MAGIC: u32 = 0x444E4653;
pub const VERSION: u32 = 1;
```

### 同步机制

| 操作 | Controller（写入端） | DLL（读取端） |
|---|---|---|
| 获取锁 | `WaitForSingleObject(mutex, INFINITE)` | `WaitForSingleObject(mutex, INFINITE)` |
| 操作数据 | 写入 keyboard[256]、tick++ | 拷贝到本地缓冲区 |
| 释放锁 | `ReleaseMutex(mutex)` | `ReleaseMutex(mutex)` |
| 持锁时间 | < 1μs（memcpy 256 字节 + 几个整数） | < 1μs（memcpy） |

### DLL 侧新鲜度检测

DLL 在每次 GetDeviceState Hook 中检查 `tick`：
- tick 变化 → 数据新鲜，应用覆盖
- tick 连续 N 次（约 100ms）未变化且 active=1 → 清零所有按键（避免按键卡住）
- active=0 → 不覆盖，返回真实设备状态

---

## 6. 阶段一：MVP Demo（2 窗口验证）

### 目标

验证核心思路可行：**注入 DLL → Hook DirectInput → 从共享内存读取按键 → 后台窗口响应**。

MVP 阶段**不实现**焦点 API Hook 和 WndProc Hook，先用前台窗口测试 DirectInput Hook 本身是否工作。

### 步骤

#### 6.1 创建 dnf-sync-common crate

- 定义 `SyncInputState` 结构体
- 定义共享内存名称、互斥体名称等常量
- 提供 `is_valid()` 校验方法

**依赖**：
```toml
[dependencies]
windows = { version = "0.58", features = ["Win32_System_Memory", "Win32_System_Threading"] }
```

#### 6.2 创建 dnf-sync-dll crate（MVP 最小版）

只实现最核心的功能：
1. DllMain → 创建工作线程
2. 工作线程 → 打开共享内存（OpenFileMappingW）
3. 获取 DirectInput GetDeviceState 函数地址
4. 用 retour 安装 inline hook
5. Hook 函数中：从共享内存读取 keyboard[256]，覆盖返回缓冲区

**核心实现**：`dinput.rs`

```
1. DirectInput8Create() → 创建临时 IDirectInput8
2. CreateDevice(GUID_SysKeyboard) → 创建临时设备
3. 读取设备 vtable[9] → GetDeviceState 函数地址
4. retour::initialize(addr, hook_fn) → 安装 hook
5. 释放临时设备和 IDirectInput8
```

```
Hook 函数伪代码：
  GetDeviceStateHook.call(pThis, cbData, lpvData)  // 调用原始函数
  if 失败(DIERR_NOTACQUIRED):
      pThis->Acquire()
      重试原始调用
  if 成功或强制:
      从共享内存拷贝 keyboard[256] 到 lpvData
  return DI_OK
```

**依赖**：
```toml
[lib]
crate-type = ["cdylib"]

[dependencies]
windows = { version = "0.58", features = [
    "Win32_Devices_HumanInterfaceDevice",
    "Win32_Foundation",
    "Win32_System_LibraryLoader",
    "Win32_System_Memory",
    "Win32_System_Threading",
    "Win32_UI_WindowsAndMessaging",
]}
retour = { version = "0.4", features = ["static-detour"] }
dnf-sync-common = { path = "../dnf-sync-common" }
```

#### 6.3 创建 dnf-sync-controller crate（MVP 最小版）

控制台程序，只做两件事：
1. 安装 WH_KEYBOARD_LL 钩子捕获所有键盘事件
2. 将按键状态写入共享内存

**核心实现**：`keyboard_hook.rs`

```
钩子回调：
  KBDLLHOOKSTRUCT 中包含 vkCode 和 scanCode
  MapVirtualKeyExW(vkCode, MAPVK_VK_TO_VSC) → 映射为 DIK 扫描码
  keyDown: keyboard[DIK_x] = 0x80
  keyUp:   keyboard[DIK_x] = 0x00
  tick++
```

**消息循环**：
```
创建隐藏窗口 → RegisterClassExW + CreateWindowExW
WH_KEYBOARD_LL 钩子需要消息泵驱动
while GetMessage(...) { TranslateMessage + DispatchMessage }
```

**依赖**：
```toml
[dependencies]
windows = { version = "0.58", features = [
    "Win32_Foundation",
    "Win32_System_Memory",
    "Win32_System_Threading",
    "Win32_UI_WindowsAndMessaging",
]}
dnf-sync-common = { path = "../dnf-sync-common" }
```

#### 6.4 测试流程

```
1. 编译 dnf-sync-dll → 生成 dnf_sync_dll.dll
2. 编译 dnf-sync-controller → 生成 dnf-sync-controller.exe
3. 启动 2 个 DNF 测试端窗口
4. 启动 controller（创建共享内存，安装键盘钩子）
5. 用 APC 注入器将 DLL 注入到两个 DNF 进程
6. 验证：操作一个窗口，另一个窗口是否同步响应
```

**MVP 验证清单**：
- [ ] DLL 成功加载（用日志文件确认）
- [ ] DirectInput hook 安装成功
- [ ] 共享内存连接成功
- [ ] 在窗口 A 按键，窗口 B 同步响应
- [ ] 方向键（扩展键）正确映射
- [ ] 高频连按不丢键

---

## 7. 阶段二：完整 Hook 链

MVP 验证通过后，添加三层欺骗机制，让后台 DNF 窗口完全以为自己处于前台。

### 7.1 焦点 API Hook

Hook 三个 Win32 API，让 DLL 所在进程始终"以为"自己是前台：

| 目标函数 | Hook 后行为 | 实现方式 |
|---|---|---|
| `GetForegroundWindow()` | 返回自身游戏窗口 HWND | retour static_detour |
| `GetFocus()` | 返回自身游戏窗口 HWND | retour static_detour |
| `GetActiveWindow()` | 返回自身游戏窗口 HWND | retour static_detour |

**初始化时序**：DLL 工作线程启动后，先 EnumWindows 找到自身进程的可见窗口 HWND，然后安装三个 Hook。

### 7.2 WndProc Hook（窗口过程子类化）

用 `SetWindowLongPtrW(GWLP_WNDPROC)` 替换游戏的窗口过程，拦截并丢弃所有"你失焦了"的消息：

| 消息 | wParam 条件 | 处理 |
|---|---|---|
| `WM_ACTIVATE` | `LOWORD(wParam) == WA_INACTIVE` | 丢弃，return 0 |
| `WM_KILLFOCUS` | 任何 | 丢弃，return 0 |
| `WM_NCACTIVATE` | `wParam == FALSE` | 丢弃，return 0 |
| `WM_ACTIVATEAPP` | `wParam == FALSE` | 丢弃，return 0 |
| 其他 | — | 传给原始 WndProc |

**恢复时**：DLL 卸载前将 WndProc 恢复为原始函数指针。

### 7.3 DirectInput SetCooperativeLevel Hook

Hook `SetCooperativeLevel`（vtable 索引 13），将游戏请求的 `DISCL_FOREGROUND` 强制改为 `DISCL_BACKGROUND | DISCL_NONEXCLUSIVE`：

```
原始调用: SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND)
Hook 后:  SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)
```

**约束**：键盘设备不允许 `DISCL_BACKGROUND | DISCL_EXCLUSIVE` 组合，必须同时改为 NONEXCLUSIVE。

**注意**：此 Hook 需要在游戏创建 DInput 设备时生效。由于我们用 retour hook 的是 dinput8.dll 内部的函数代码（不是 vtable 指针），需要在游戏创建设备之前就安装好 Hook。DLL 注入时机需要早于 DInput 初始化——APC 注入在进程启动早期执行，通常满足此条件。

### 7.4 DirectInput Acquire/Unacquire Hook（可选增强）

- **Hook Unacquire**（vtable 索引 8）：窗口失焦时 DirectInput 自动调用 Unacquire 释放设备。Hook 后直接返回 DI_OK，不执行真实释放。
- **Hook Acquire**（vtable 索引 7）：后台获取失败时伪装成功。

这两个 Hook 是 SetCooperativeLevel Hook 的补充保障，确保即使合作级别修改不够早，设备也不会在后台被意外释放。

---

## 8. 阶段三：控制器完善

### 8.1 前台窗口检测

控制器定时（每 100ms）检查 `GetForegroundWindow()`：

| 前台窗口状态 | 处理 |
|---|---|
| 是 DNF 窗口 | 共享内存 `active` = 1，正常同步 |
| 不是 DNF 窗口 | 共享内存 `active` = 0，DLL 停止注入 |
| 切回 DNF 窗口 | `active` = 1，自动恢复同步 |

DLL 侧检测到 `active == 0` 时，GetDeviceState Hook 返回真实设备状态（不注入伪造输入）。

**DNF 窗口识别方式**（按优先级）：
1. 窗口标题匹配（"地下城与勇士" 或 "DNF"）
2. 进程名匹配（DNF.exe）
3. 窗口类名匹配（EnumWindows + GetClassNameA 枚举后验证）

### 8.2 高频方向键处理

方向键是游戏最关键的操作，高频切换时需要确保不丢键、不卡键：

**控制器侧**：
- 钩子回调中直接写入共享内存，不经过任何队列
- keyDown → `keyboard[DIK_x] = 0x80`，keyUp → `keyboard[DIK_x] = 0x00`
- 每次写入递增 `tick`

**DLL 侧**：
- 检查 `tick` 是否变化（判断数据新鲜度）
- tick 未变且超过 100ms → 清零所有按键状态（防止按键卡住）
- 直接覆盖 256 字节缓冲区，不逐键合并

**锁机制**：Windows Named Mutex 保护共享内存读写
- 控制器：Lock → 写入 → Unlock
- DLL：Lock → 拷贝到本地 → Unlock → 应用到缓冲区
- 持锁时间 < 1μs（memcpy 256 字节），不影响游戏帧率

### 8.3 VK → DIK 扫描码映射

控制器捕获的是 Windows VirtualKeyCode，需要映射为 DirectInput ScanCode：

**推荐方案：调用 MapVirtualKeyExW**

```rust
let scan_code = unsafe {
    MapVirtualKeyExW(vk_code, MAPVK_VK_TO_VSC, GetKeyboardLayout(0))
};
```

理由：`MapVirtualKeyExW` 是用户态调用（不涉及内核切换），延迟可忽略。系统级映射准确，自动处理键盘布局差异。

**备选方案：静态映射表**

预编译一份 VK → DIK 的对照表。优势是不依赖系统 API、确定性高；劣势是需要手动维护所有按键的映射。

### 8.4 进程注入调度

控制器检测到新的 DNF 窗口时：
1. 获取窗口对应的 PID
2. 调用 APC 注入器注入 DLL
3. 等待 DLL 日志确认 hook 安装成功
4. 将窗口信息推送给 Tauri 前端

DNF 窗口消失时：
1. 共享内存不再被该进程读取（无影响）
2. 从窗口列表中移除
3. 通知 Tauri 前端更新状态

---

## 9. 阶段四：Tauri GUI

### 9.1 项目初始化

```bash
npm create tauri-app@latest
# 选择 Rust 后端 + 前端框架（Vanilla / Vue / React 等）
```

### 9.2 功能设计

| 功能 | 实现方式 |
|---|---|
| 启动同步 | Tauri Command → 启动 controller sidecar |
| 停止同步 | Tauri Command → 向 controller 发送停止信号（stdin 写入 `stop\n`） |
| 注入状态显示 | controller stdout JSON → Tauri Event → 前端渲染 |
| DNF 窗口列表 | controller 枚举后通过 Event 推送 |
| 日志滚动显示 | controller stderr → Event → 前端滚动日志 |
| 热键控制 | Tauri 全局快捷键注册 |

### 9.3 Controller ↔ Tauri 通信协议

Controller 通过 stdout 输出 JSON 事件（每行一条）：

```json
{"type": "status", "syncing": true, "windows": 2, "active_hwnd": "DNF"}
{"type": "window_list", "windows": [{"pid": 1234, "hwnd": "0x1234", "title": "地下城与勇士"}]}
{"type": "injected", "pid": 1234, "success": true}
{"type": "warning", "msg": "Foreground window changed to Chrome, sync paused"}
{"type": "error", "msg": "Failed to inject DLL into PID 5678"}
```

Tauri 通过 stdin 发送命令：

```
start\n        → 开始同步
stop\n         → 停止同步
inject\n       → 重新扫描并注入
```

### 9.4 Tauri 配置要点

**tauri.conf.json**：
```json
{
  "bundle": {
    "externalBin": ["binaries/dnf-sync-controller", "binaries/injector"]
  }
}
```

**capabilities/default.json**（Shell 插件权限）：
```json
{
  "permissions": [
    {
      "identifier": "shell:allow-spawn",
      "allow": [
        { "name": "binaries/dnf-sync-controller", "sidecar": true },
        { "name": "binaries/injector", "sidecar": true }
      ]
    }
  ]
}
```

---

## 10. 关键依赖

```toml
# dnf-sync-common
windows = { version = "0.58", features = [
    "Win32_System_Memory",
    "Win32_System_Threading",
]}

# dnf-sync-dll
[lib]
crate-type = ["cdylib"]

[dependencies]
windows = { version = "0.58", features = [
    "Win32_Devices_HumanInterfaceDevice",
    "Win32_Foundation",
    "Win32_System_LibraryLoader",
    "Win32_System_Memory",
    "Win32_System_Threading",
    "Win32_UI_WindowsAndMessaging",
]}
retour = { version = "0.4", features = ["static-detour"] }
dnf-sync-common = { path = "../dnf-sync-common" }

# dnf-sync-controller
[dependencies]
windows = { version = "0.58", features = [
    "Win32_Foundation",
    "Win32_System_Memory",
    "Win32_System_Threading",
    "Win32_UI_WindowsAndMessaging",
]}
serde = { version = "1", features = ["derive"] }
serde_json = "1"
dnf-sync-common = { path = "../dnf-sync-common" }

# dnf-sync-ui
[dependencies]
tauri = { version = "2" }
tauri-plugin-shell = "2"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
```

---

## 11. 验证计划

### MVP 验证（阶段一）

| 序号 | 验证项 | 通过标准 |
|---|---|---|
| 1 | DLL 加载成功 | 日志文件中出现 "DLL_PROCESS_ATTACH" |
| 2 | DInput Hook 安装成功 | 日志中出现 "GetDeviceState hooked at 0x..." |
| 3 | 共享内存连接成功 | 日志中出现 "Shared memory opened" |
| 4 | 基本按键同步 | 窗口 A 按 J，窗口 B 角色释放技能 |
| 5 | 方向键映射正确 | WASD/方向键移动正常，不被误识别为小键盘 |
| 6 | 高频连按不丢键 | 快速连按攻击键（如 X），两侧角色同步攻击 |

### 完整验证（阶段二~四）

| 序号 | 验证项 | 通过标准 |
|---|---|---|
| 7 | 后台窗口响应输入 | 切换到窗口 B 后，窗口 A（后台）仍响应同步输入 |
| 8 | 非法窗口自动暂停 | 切换到 Chrome，同步自动停止 |
| 9 | 切回自动恢复 | 切回 DNF 窗口，同步自动恢复 |
| 10 | 3+ 窗口同步 | 4 个 DNF 窗口同步稳定运行 30 分钟 |
| 11 | Tauri GUI 控制 | 通过 GUI 启动/停止同步正常工作 |
| 12 | 无内存泄漏 | 运行 2 小时后内存占用稳定 |
| 13 | DLL 卸载正常 | 停止同步后 DNF 进程不崩溃 |

---

## 12. 参考资料

### Rust 库

| 库 | 用途 | 链接 |
|---|---|---|
| `retour` | Rust inline hook | [GitHub](https://github.com/Hpmason/retour-rs) |
| `libmem` | 备选 hook 库（含 VMT hook） | [GitHub](https://github.com/rdbo/libmem) |
| `vtable-rs` | COM vtable 操作辅助 | [GitHub](https://github.com/tremwil/vtable-rs) |
| `windows` | Microsoft 官方 Windows API 绑定 | [GitHub](https://github.com/microsoft/windows-rs) |
| `tauri` | 桌面应用框架 | [官网](https://v2.tauri.app/) |

### 参考项目

| 项目 | 说明 | 链接 |
|---|---|---|
| Rust DirectX 11 Hook | Rust DLL + D3D hook 完整示例 | [GitHub](https://github.com/Zazama/Rust-DirectX11-FPS-Limiter-Example) |
| DirectX Wrappers | C++ DirectInput 全套 proxy DLL | [GitHub](https://github.com/elishacloud/DirectX-Wrappers) |
| directinput-rs | Rust DirectInput 封装 | [GitHub](https://github.com/mbilker/directinput-rs) |
| rustdllproxy | Rust 代理 DLL 生成器 | [GitHub](https://github.com/JohnSwiftC/rustdllproxy) |
| IvanSynchronizer | Java DNF 网络同步器（架构参考） | [GitHub](https://github.com/JoivanJostar/IvanSynchronizer) |

### 技术教程

| 资源 | 说明 | 链接 |
|---|---|---|
| Guided Hacking - DirectInput Hook | vtable hook 详细教程 | [链接](https://guidedhacking.com/threads/how-to-hook-directinput-emulate-key-presses.14011/) |
| Guided Hacking - VMT Hook Tutorial | VMT hook 基础 | [链接](https://guidedhacking.com/threads/vtable-hooking-vmt-hook-tutorial.3979/) |
| Rust DirectX Hook 博客 | Rust DLL + minhook 实战 | [链接](https://zazama.de/blog/creating-an-fps-limiter-in-rust-by-hooking-directx) |
| Kenny Kerr - Rust DLL | Rust 编写 Windows DLL 入门 | [链接](https://kennykerr.ca/rust-getting-started/creating-your-first-dll.html) |
| MSDN - IDirectInputDevice8 | DirectInput 接口官方文档 | [链接](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417897(v=vs.85)) |
| MSDN - SetCooperativeLevel | DInput 合作级别说明 | [链接](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417921(v=vs.85)) |
| Tauri Sidecar 文档 | Sidecar 进程管理 | [链接](https://v2.tauri.app/develop/sidecar/) |
