好的，以下是针对 `game-all` 项目中存在的“EnumWindows 死锁”和“注入过早”问题的完整代码修改方案。

本方案直接针对 `Payload/modules/sync/SyncMod.cpp` 和 `Injector/main.cpp` 两个文件进行修改。

---

### 方案 1：修复 EnumWindows 死锁 (Payload)

**目标**：将所有 `EnumWindows` 调用移出 Hook 回调（UI 线程），改在后台工作线程中定期执行。

**修改文件**：`Payload/modules/sync/SyncMod.cpp`

#### 步骤 1：修改全局变量声明

找到 `// ------------------------------` 分隔线下的 `// 全局状态与计数器` 或 `static HWND g_selfWindowCache` 定义处。

**修改前**：

```cpp
static HWND g_selfWindowCache = nullptr;
static DWORD g_selfWindowCacheTick = 0;

```

**修改后**：

```cpp
// [修改] 使用 volatile 确保跨线程可见性，移除时间戳变量
static volatile HWND g_selfWindowCache = nullptr;

```

#### 步骤 2：添加更新缓存函数

在 `static BOOL CALLBACK EnumWindowsFindSelf(...)` 函数定义的**下方**，添加一个新的辅助函数 `UpdateSelfWindowCache`。

**新增代码**：

```cpp
// [新增] 专门在后台线程调用的窗口搜索函数
static void UpdateSelfWindowCache()
{
    WindowSearchContext ctx = {};
    ctx.pid = GetCurrentProcessId();
    // 此时在后台线程运行，调用 EnumWindows 是安全的
    EnumWindows(EnumWindowsFindSelf, reinterpret_cast<LPARAM>(&ctx));

    HWND result = ctx.best ? ctx.best : ctx.fallback;
    
    // 如果找到了有效窗口，则更新全局缓存
    if (result && IsWindow(result))
    {
        g_selfWindowCache = result;
    }
}

```

#### 步骤 3：重写 GetSelfMainWindow

修改原有的 `GetSelfMainWindow` 函数，将其逻辑简化为仅返回缓存。

**修改后**：

```cpp
// [修改] 极简版本，仅返回缓存句柄，绝不执行阻塞搜索
static HWND GetSelfMainWindow()
{
    return g_selfWindowCache;
}

```

#### 步骤 4：在工作线程中调度更新

找到 `static DWORD WINAPI WorkerThread(LPVOID)` 函数，在主循环中添加更新逻辑。

**修改代码**：

```cpp
static DWORD WINAPI WorkerThread(LPVOID)
{
    // ... (前面的初始化代码保持不变) ...

    InstallUser32Hooks();
    InstallDirectInputHook();

    LogInfo(L"Hook 安装完成，开始统计调用频率");

    // [新增] 线程启动时立即更新一次，尽可能减少空窗期
    UpdateSelfWindowCache();

    // ... (中间可能有的代码) ...

    while (InterlockedCompareExchange(&g_shouldStop, 0, 0) == 0)
    {
        Sleep(1000);
        
        // [新增] 每秒在后台安全地更新一次窗口句柄
        UpdateSelfWindowCache();
        
        LogCountersOnce();
    }

    LogInfo(L"工作线程退出");
    return 0;
}

```

---

### 方案 3：优化注入时机 (Injector)

**目标**：确保注入器只在目标进程创建了可见窗口（即初始化基本完成）后再进行注入。

**修改文件**：`Injector/main.cpp`

#### 步骤 1：添加等待窗口函数

在 `TryInjectProcess` 函数定义的**上方**，添加 `WaitForProcessWindow` 函数。

**新增代码**：

```cpp
// [新增] 检查指定 PID 是否拥有可见窗口的回调上下文
struct WindowCheckCtx {
    DWORD targetPid;
    bool found;
};

// [新增] 窗口遍历回调
static BOOL CALLBACK EnumWindowCheckProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<WindowCheckCtx*>(lParam);
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    
    // 检查 PID 匹配且窗口可见
    if (wndPid == ctx->targetPid && IsWindowVisible(hwnd)) {
        ctx->found = true;
        return FALSE; // 找到目标，停止遍历
    }
    return TRUE; // 继续寻找
}

// [新增] 等待进程创建窗口（带超时）
static bool WaitForProcessWindow(DWORD pid, DWORD timeout_ms) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start <= timeout_ms) {
        WindowCheckCtx ctx = { pid, false };
        EnumWindows(EnumWindowCheckProc, reinterpret_cast<LPARAM>(&ctx));

        if (ctx.found) {
            return true;
        }
        Sleep(500); // 每 500ms 轮询一次
    }
    return false;
}

```

#### 步骤 2：在主逻辑中调用等待 (wmain)

`main.cpp` 的 `wmain` 函数中有两处注入逻辑（一处是普通模式，一处是 `watch_mode` 循环模式），都需要修改。

**第一处：普通模式（非 Watch Mode）**
找到 `if (!config.watch_mode)` 代码块：

```cpp
    if (!config.watch_mode) {
        DWORD pid = 0;
        Log(L"等待目标进程...");
        while ((pid = FindProcessId(config.process_name)) == 0) {
            Sleep(config.scan_interval_ms);
        }
        Log(L"发现 PID: " + std::to_wstring(pid));

        // [新增] 等待窗口逻辑
        Log(L"等待目标进程窗口初始化...");
        if (!WaitForProcessWindow(pid, 30000)) { // 30秒超时
            Log(L"超时：目标进程未在规定时间内创建窗口，终止注入");
            return 3;
        }
        Log(L"检测到游戏窗口，准备注入");

        if (config.inject_delay_ms > 0) {
            Sleep(config.inject_delay_ms);
        }
        // ... (后续 TryInjectProcess 调用保持不变)

```

**第二处：常驻监听模式 (Watch Mode)**
找到 `watch_mode` 的 `for (;;)` 循环中处理新进程的部分：

```cpp
        // 处理新进程
        for (DWORD pid : pids) {
            if (states.find(pid) != states.end()) {
                continue;
            }
            ProcessState state;
            state.last_seen = now;
            states.emplace(pid, state);
            last_new_tick = now;

            Log(L"发现新进程: PID " + std::to_wstring(pid));

            // [新增] 等待窗口逻辑
            // 注意：这里不能直接 return，而是应该跳过本次循环的注入，或者阻塞当前线程（因为是控制台工具，阻塞是可以接受的）
            Log(L"等待目标进程窗口初始化...");
            if (!WaitForProcessWindow(pid, 30000)) {
                Log(L"超时：跳过该进程注入");
                states[pid].injected = false; // 标记为未注入或失败
                continue;
            }
            
            if (config.inject_delay_ms > 0) {
                Sleep(config.inject_delay_ms);
            }
            // ... (后续 TryInjectProcess 调用保持不变)

```

---

### 总结

1. **Payload (SyncMod.cpp)**：通过引入 `UpdateSelfWindowCache` 和修改 `GetSelfMainWindow`，彻底移除了 UI 线程中的同步阻塞风险。
2. **Injector (main.cpp)**：通过 `WaitForProcessWindow`，确保只在游戏画面出现（系统环境稳定）后才执行 APC 注入。

请按照上述步骤依次修改代码，并重新编译解决方案。