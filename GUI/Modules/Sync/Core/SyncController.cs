using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;
using System.Windows.Threading;

namespace DNFSyncBox;

public sealed class SyncController : IDisposable
{
    private static readonly bool VerboseLogging = true;
    private static readonly object LogFileLock = new();
    private readonly string _logDirectory = Path.Combine(AppContext.BaseDirectory, "logs", "sync", "gui");
    private string _logFilePath = string.Empty;
    private const string DisableAutoPauseEnvName = "DNFSYNC_DISABLE_AUTOPAUSE";
    private const int ForegroundProbeIntervalMs = 200;
    private const int ForegroundGraceMs = 800;
    private const int RepeatLogIntervalMs = 200;

    // 统一保护核心状态（窗口快照、暂停状态、按键状态）。
    private readonly object _stateLock = new();
    // 事件流状态单独加锁，避免心跳与按键事件交叉写入。
    private readonly object _eventLock = new();
    private readonly WindowManager _windowManager = new("DNF Taiwan", "dnf.exe");
    private readonly KeyboardHook _keyboardHook = new();
    private readonly KeyStateTracker _keyState = new();
    private readonly SharedMemoryWriter _sharedMemory = new();
    private readonly KeyboardProfileManager _profileManager = new();
    private readonly DispatcherTimer _scanTimer;
    private readonly System.Threading.Timer _heartbeatTimer;
    private readonly System.Threading.Timer _foregroundTimer;

    private readonly byte[] _keyboardState = new byte[SharedMemoryConstants.KeyCount];
    private readonly uint[] _edgeCounter = new uint[SharedMemoryConstants.KeyCount];
    private readonly byte[] _targetMask = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _blockMask = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _mappingMask = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _mappingSourceMask = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _toggleState = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _inputMask = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _inputMappingSourceMask = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _physicalDown = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _lastEventState = new byte[SharedMemoryConstants.KeyCount];

    private WindowSnapshot _snapshot = WindowSnapshot.Empty;
    private KeyboardProfile _activeProfile;
    private bool _userPaused;
    private bool _autoPaused = true;
    private bool _disableAutoPause;
    private uint _effectiveForegroundPid;
    private bool _effectiveForegroundIsDnf;
    private int _lastForegroundPid;
    private long _lastForegroundTickMs;
    private bool _altDown;
    private bool _ctrlDown;
    private bool _shiftDown;
    private bool _hotkeyDown;
    private string _lastSnapshotSignature = string.Empty;
    private SyncHotkeyManager? _hotkeyManager;
    private int _lastRepeatLogKey = -1;
    private long _lastRepeatLogTickMs;

    /// <summary>
    /// 状态变化事件：用于 UI 展示。
    /// </summary>
    public event Action<SyncStatus>? StatusChanged;
    /// <summary>
    /// 日志事件：用于 UI 记录。
    /// </summary>
    public event Action<string>? LogAdded;

    /// <summary>
    /// 初始化扫描定时器（每秒刷新一次窗口列表）。
    /// </summary>
    public SyncController()
    {
        _activeProfile = _profileManager.ActiveProfile;
        _scanTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        _scanTimer.Tick += (_, _) => RefreshWindows();
        _heartbeatTimer = new System.Threading.Timer(HeartbeatTick, null, Timeout.Infinite, Timeout.Infinite);
        _foregroundTimer = new System.Threading.Timer(ForegroundProbeTick, null, Timeout.Infinite, Timeout.Infinite);
    }

    /// <summary>
    /// 启动同步控制器：安装钩子、扫描窗口、启动定时器。
    /// </summary>
    public void Start()
    {
        var sessionId = ReadSessionId();
        _logFilePath = Path.Combine(_logDirectory, $"sync_gui_{sessionId}_{Environment.ProcessId}.log");
        _disableAutoPause = IsAutoPauseDisabled();
        if (_disableAutoPause)
        {
            Log("已禁用自动暂停（DNFSYNC_DISABLE_AUTOPAUSE=1 或 Debug 默认关闭）");
        }

        _hotkeyManager ??= new SyncHotkeyManager(AppContext.BaseDirectory, message => Log($"[热键] {message}"));
        Log($"热键配置：{_hotkeyManager.Current.Display}");

        _keyboardHook.KeyEvent += OnKeyEvent;
        _keyboardHook.Install();

        TryInitializeSharedMemory();
        ReloadProfileIfNeeded(force: true);
        Log($"配置文件路径：{_profileManager.ConfigPath}");

        RefreshWindows();
        _scanTimer.Start();
        _heartbeatTimer.Change(0, SharedMemoryConstants.HeartbeatIntervalMs);
        _foregroundTimer.Change(0, ForegroundProbeIntervalMs);
    }

    /// <summary>
    /// 刷新窗口快照并根据前台状态触发自动暂停/恢复。
    /// </summary>
    public void RefreshWindows()
    {
        ReloadProfileIfNeeded(force: false);

        var snapshot = _windowManager.Refresh();
        var now = Environment.TickCount64;
        if (_disableAutoPause)
        {
            var hasDnf = snapshot.TotalCount > 0;
            var effectivePidLocal = snapshot.ForegroundProcessId;
            if (effectivePidLocal == 0 && snapshot.MasterHandle != IntPtr.Zero)
            {
                effectivePidLocal = GetProcessId(snapshot.MasterHandle);
            }

            lock (_stateLock)
            {
                _snapshot = snapshot;
                _effectiveForegroundPid = effectivePidLocal;
                _effectiveForegroundIsDnf = hasDnf;
                _autoPaused = false;
            }

            RaiseStatusChanged();
            LogSnapshotIfNeeded(snapshot);
            return;
        }
        if (snapshot.ForegroundIsDnf && snapshot.ForegroundProcessId != 0)
        {
            Interlocked.Exchange(ref _lastForegroundPid, unchecked((int)snapshot.ForegroundProcessId));
            Interlocked.Exchange(ref _lastForegroundTickMs, now);
        }

        var lastPid = (uint)Interlocked.CompareExchange(ref _lastForegroundPid, 0, 0);
        var lastTick = Interlocked.Read(ref _lastForegroundTickMs);
        var graceActive = lastPid != 0 && now - lastTick <= ForegroundGraceMs;
        var effectiveForegroundIsDnf = snapshot.ForegroundIsDnf || graceActive;
        var effectivePid = snapshot.ForegroundIsDnf ? snapshot.ForegroundProcessId : (graceActive ? lastPid : 0u);
        var autoPausedChanged = false;
        bool autoPaused;

        lock (_stateLock)
        {
            _snapshot = snapshot;
            _effectiveForegroundPid = effectivePid;
            _effectiveForegroundIsDnf = effectiveForegroundIsDnf;
            // 前台不是 DNF 时进入自动暂停，防止误同步。
            var newAutoPaused = !effectiveForegroundIsDnf;
            if (newAutoPaused != _autoPaused)
            {
                _autoPaused = newAutoPaused;
                autoPausedChanged = true;
            }
            autoPaused = _autoPaused;
        }

        if (autoPausedChanged && autoPaused)
        {
            if (!_sharedMemory.IsReady)
            {
                TryInitializeSharedMemory();
            }
            // 自动暂停时先清键，避免卡键。
            ClearStuckKeys();
            Log("前台非 DNF，已自动暂停同步");
        }
        else if (autoPausedChanged && !autoPaused)
        {
            Log("前台已回到 DNF，自动暂停解除");
            if (!_sharedMemory.IsReady)
            {
                TryInitializeSharedMemory();
            }
            // 解除自动暂停后立即发布，避免等待心跳或按键事件。
            PublishSnapshot(forceClear: false);
        }

        LogSnapshotIfNeeded(snapshot);
        RaiseStatusChanged();
    }

    /// <summary>
    /// 手动暂停/恢复；暂停时强制清键。
    /// </summary>
    public void TogglePause()
    {
        bool isPausedNow;
        lock (_stateLock)
        {
            _userPaused = !_userPaused;
            isPausedNow = _userPaused;
        }

        // 确保共享内存就绪，避免暂停状态写入被延迟。
        if (!_sharedMemory.IsReady)
        {
            TryInitializeSharedMemory();
        }

        if (isPausedNow)
        {
            // 手动暂停同样需要清键。
            ClearStuckKeys();
            Log("已手动暂停同步");
        }
        else
        {
            Log("已恢复同步");
            PublishSnapshot(forceClear: false);
        }

        RaiseStatusChanged();
    }

    /// <summary>
    /// 处理键盘钩子事件：热键、过滤、共享内存写入。
    /// </summary>
    private void OnKeyEvent(Keys key, bool isDown)
    {
        var isAltKey = IsAltKey(key);
        var isCtrlKey = IsCtrlKey(key);
        var isShiftKey = IsShiftKey(key);
        if (isAltKey)
        {
            lock (_stateLock)
            {
                _altDown = isDown;
            }
            LogVerbose($"热键辅助键 Alt：{(isDown ? "按下" : "抬起")}");
        }
        if (isCtrlKey)
        {
            lock (_stateLock)
            {
                _ctrlDown = isDown;
            }
            LogVerbose($"热键辅助键 Ctrl：{(isDown ? "按下" : "抬起")}");
        }
        if (isShiftKey)
        {
            lock (_stateLock)
            {
                _shiftDown = isDown;
            }
            LogVerbose($"热键辅助键 Shift：{(isDown ? "按下" : "抬起")}");
        }

        var hotkey = _hotkeyManager?.Current ?? SyncHotkeyManager.DefaultHotkey;
        if (key == hotkey.Key)
        {
            var shouldToggle = false;
            var altDown = false;
            var ctrlDown = false;
            var shiftDown = false;
            lock (_stateLock)
            {
                altDown = _altDown;
                ctrlDown = _ctrlDown;
                shiftDown = _shiftDown;
                if (isDown && hotkey.MatchesModifiers(altDown, ctrlDown, shiftDown) && !_hotkeyDown)
                {
                    _hotkeyDown = true;
                    shouldToggle = true;
                }
                else if (!isDown)
                {
                    _hotkeyDown = false;
                }
            }

            if (shouldToggle)
            {
                LogVerbose($"热键触发：{hotkey.Display}");
                TogglePause();
            }

            if (hotkey.MatchesModifiers(altDown, ctrlDown, shiftDown))
            {
                // 热键组合时不写入目标键，避免误触同步。
                return;
            }
        }

        var vKey = (int)(key & Keys.KeyCode);
        if (vKey < 0 || vKey >= SharedMemoryConstants.KeyCount)
        {
            LogVerbose($"忽略超范围键：{key}");
            return;
        }

        WindowSnapshot snapshot;
        bool paused;
        bool effectiveForeground;
        bool changed;

        lock (_stateLock)
        {
            snapshot = _snapshot;
            paused = IsPausedLocked();
            effectiveForeground = _effectiveForegroundIsDnf;

            // 仅在“有效前台”状态下处理按下事件，避免后台误同步。
            // 抬起事件即使在后台也允许处理，用于清理可能的卡键状态。
            if (isDown && (paused || !effectiveForeground))
            {
                return;
            }

            changed = _keyState.SetState(vKey, isDown);
        }

        if (!changed)
        {
            var now = Environment.TickCount64;
            if (_lastRepeatLogKey != vKey || now - _lastRepeatLogTickMs >= RepeatLogIntervalMs)
            {
                _lastRepeatLogKey = vKey;
                _lastRepeatLogTickMs = now;
                LogVerbose($"忽略重复按键：{key}");
            }
            return;
        }

        LogVerbose($"键盘事件：{key} {(isDown ? "按下" : "抬起")} | 暂停={paused} | 前台DNF={effectiveForeground}");
        PublishSnapshot(forceClear: false);
    }

    /// <summary>
    /// 清理所有按下状态并通知共享内存，避免卡键。
    /// </summary>
    private void ClearStuckKeys()
    {
        lock (_stateLock)
        {
            _keyState.Clear();
        }

        PublishSnapshot(forceClear: true);
    }

    /// <summary>
    /// 汇总当前状态并通知 UI。
    /// </summary>
    private void RaiseStatusChanged()
    {
        WindowSnapshot snapshot;
        bool paused;
        bool autoPaused;
        bool effectiveForeground;

        lock (_stateLock)
        {
            snapshot = _snapshot;
            paused = IsPausedLocked();
            autoPaused = _autoPaused;
            effectiveForeground = _effectiveForegroundIsDnf;
        }

        StatusChanged?.Invoke(new SyncStatus
        {
            IsPaused = paused,
            IsAutoPaused = autoPaused,
            ForegroundIsDnf = effectiveForeground,
            MasterHandle = snapshot.MasterHandle,
            SlaveCount = snapshot.SlaveHandles.Count,
            TotalCount = snapshot.TotalCount
        });
    }

    /// <summary>
    /// 发布共享内存快照（含心跳、方案与按键状态）。
    /// </summary>
    private void PublishSnapshot(bool forceClear)
    {
        if (!_sharedMemory.IsReady)
        {
            return;
        }

        WindowSnapshot snapshot;
        KeyboardProfile profile;
        bool paused;
        uint activePid;

        lock (_stateLock)
        {
            snapshot = _snapshot;
            profile = _activeProfile;
            paused = IsPausedLocked();
            activePid = _effectiveForegroundIsDnf ? _effectiveForegroundPid : 0u;

            UpdateToggleState();

            if (paused)
            {
                Array.Clear(_keyboardState, 0, _keyboardState.Length);
                _keyState.CopyEdgeCounters(_edgeCounter);
                profile.BuildMask(_targetMask);
                profile.BuildBlockMask(_blockMask);
            }
            else
            {
                _keyState.ApplyProfile(profile, _toggleState, _keyboardState, _edgeCounter, _targetMask, Environment.TickCount64);
                profile.BuildBlockMask(_blockMask);
            }
        }

        var flags = paused ? SharedMemoryConstants.FlagPaused : 0u;
        if (forceClear)
        {
            flags |= SharedMemoryConstants.FlagClear;
        }

        var tick = (ulong)Environment.TickCount64;
        var eventTimestamp = Stopwatch.GetTimestamp();

        // Replace 映射依赖 RawInput 事件路径；上报 Mapping 模式可触发注入端生成映射事件。
        var reportedMode = profile.Mode;
        if (reportedMode != KeyboardProfileMode.Mapping &&
            profile.MappingBehavior == KeyboardMappingBehavior.Replace)
        {
            reportedMode = KeyboardProfileMode.Mapping;
        }

        if (reportedMode == KeyboardProfileMode.Mapping)
        {
            Array.Clear(_mappingMask, 0, _mappingMask.Length);
            profile.BuildMappingMask(_mappingMask);
            Array.Clear(_mappingSourceMask, 0, _mappingSourceMask.Length);
            profile.BuildMappingSourceMask(_mappingSourceMask);
            for (var i = 0; i < _blockMask.Length; i++)
            {
                if (_mappingMask[i] != 0)
                {
                    _blockMask[i] |= 0x02;
                }
                if (_mappingSourceMask[i] != 0)
                {
                    _blockMask[i] |= 0x01;
                }
            }
        }

        EmitEventsForStateChange(_keyboardState, eventTimestamp);

        _sharedMemory.PublishSnapshot(
            flags,
            activePid,
            profile.ProfileId,
            (uint)reportedMode,
            tick,
            _keyboardState,
            _edgeCounter,
            _targetMask,
            _blockMask);
    }

    private void EmitEventsForStateChange(byte[] keyboardState, long timestamp)
    {
        lock (_eventLock)
        {
            for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
            {
                var isDown = (keyboardState[i] & 0x80) != 0;
                var wasDown = (_lastEventState[i] & 0x80) != 0;
                if (isDown == wasDown)
                {
                    continue;
                }

                _sharedMemory.PushEvent(i, isDown, timestamp);
                _lastEventState[i] = (byte)(isDown ? 0x80 : 0x00);
            }
        }
    }

    /// <summary>
    /// 读取当前系统键盘切换态（Caps/Num/Scroll 等）。
    /// </summary>
    private void UpdateToggleState()
    {
        if (!NativeMethods.GetKeyboardState(_toggleState))
        {
            Array.Clear(_toggleState, 0, _toggleState.Length);
            return;
        }

        for (var i = 0; i < _toggleState.Length; i++)
        {
            _toggleState[i] &= 0x01;
        }
    }

    private void HeartbeatTick(object? state)
    {
        try
        {
            AlignKeyStateWithPhysicalInput();
            PublishSnapshot(forceClear: false);
        }
        catch (Exception ex)
        {
            Log($"共享内存心跳异常：{ex.Message}");
        }
    }

    /// <summary>
    /// 按物理键状态对齐内部按键状态，修复丢失的抬起/按下。
    /// </summary>
    private void AlignKeyStateWithPhysicalInput()
    {
        KeyboardProfile profile;
        bool paused;
        bool effectiveForeground;

        lock (_stateLock)
        {
            profile = _activeProfile;
            paused = IsPausedLocked();
            effectiveForeground = _effectiveForegroundIsDnf;
        }

        BuildInputMask(profile);

        for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
        {
            _physicalDown[i] = _inputMask[i] == 0 ? (byte)0 : (byte)(IsPhysicallyDown(i) ? 1 : 0);
        }

        var anyChanged = false;
        lock (_stateLock)
        {
            for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
            {
                if (_inputMask[i] == 0)
                {
                    continue;
                }

                var physicalDown = _physicalDown[i] != 0;
                // 仅在前台且未暂停时接受“按下”补偿；抬起补偿不受前台/暂停限制。
                if (!physicalDown && _keyState.SetState(i, false))
                {
                    anyChanged = true;
                }
                else if (physicalDown && !paused && effectiveForeground && _keyState.SetState(i, true))
                {
                    anyChanged = true;
                }
            }
        }

        if (anyChanged)
        {
            LogVerbose("物理键状态校准触发，已修正按键状态");
        }
    }

    private void BuildInputMask(KeyboardProfile profile)
    {
        Array.Clear(_inputMask, 0, _inputMask.Length);
        profile.BuildMask(_inputMask);

        if (profile.Mode == KeyboardProfileMode.Mapping ||
            profile.MappingBehavior == KeyboardMappingBehavior.Replace)
        {
            Array.Clear(_inputMappingSourceMask, 0, _inputMappingSourceMask.Length);
            profile.BuildMappingSourceMask(_inputMappingSourceMask);
            for (var i = 0; i < _inputMask.Length; i++)
            {
                if (_inputMappingSourceMask[i] != 0)
                {
                    _inputMask[i] = 1;
                }
            }
        }
    }

    private static bool IsPhysicallyDown(int vKey)
    {
        return (NativeMethods.GetAsyncKeyState(vKey) & 0x8000) != 0;
    }

    private void ForegroundProbeTick(object? state)
    {
        try
        {
            if (_disableAutoPause)
            {
                return;
            }

            if (_windowManager.TryGetForegroundInfo(out var pid, out var isDnf) && isDnf)
            {
                Interlocked.Exchange(ref _lastForegroundPid, unchecked((int)pid));
                Interlocked.Exchange(ref _lastForegroundTickMs, Environment.TickCount64);
            }
        }
        catch (Exception ex)
        {
            LogVerbose($"前台快速检测异常：{ex.Message}");
        }
    }

    private void ReloadProfileIfNeeded(bool force)
    {
        var changed = _profileManager.ReloadIfChanged(out var message);
        if (!force && !changed)
        {
            return;
        }

        lock (_stateLock)
        {
            _activeProfile = _profileManager.ActiveProfile;
        }

        if (!string.IsNullOrWhiteSpace(message))
        {
            Log(message);
        }

        PublishSnapshot(forceClear: true);
    }

    private void TryInitializeSharedMemory()
    {
        try
        {
            _sharedMemory.Initialize();
            Log("共享内存已初始化");
        }
        catch (Exception ex)
        {
            Log($"共享内存初始化失败：{ex.Message}");
        }
    }

    /// <summary>
    /// 记录一条时间戳日志。
    /// </summary>
    private void Log(string message)
    {
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {message}";
        LogAdded?.Invoke(line);
        AppendLogFile(line);
    }

    private void AppendLogFile(string line)
    {
        try
        {
            lock (LogFileLock)
            {
                Directory.CreateDirectory(_logDirectory);
                File.AppendAllText(_logFilePath, line + Environment.NewLine);
            }
        }
        catch
        {
            // 文件日志失败不影响主流程，避免影响同步稳定性。
        }
    }

    private static string ReadSessionId()
    {
        try
        {
            var baseDir = AppContext.BaseDirectory;
            var sessionFile = Path.Combine(baseDir, "logs", "session.current");
            if (File.Exists(sessionFile))
            {
                var content = File.ReadAllText(sessionFile).Trim();
                if (!string.IsNullOrWhiteSpace(content))
                {
                    return content;
                }
            }
        }
        catch
        {
            // 读取失败则回退时间戳
        }

        return DateTime.Now.ToString("yyyyMMdd_HHmmss");
    }

    private void LogVerbose(string message)
    {
        if (!VerboseLogging)
        {
            return;
        }

        Log($"[调试] {message}");
    }

    /// <summary>
    /// 计算综合暂停状态（手动暂停或自动暂停）。
    /// </summary>
    private bool IsPausedLocked() => _userPaused || _autoPaused;

    /// <summary>
    /// Alt 键判定（系统菜单键）。
    /// </summary>
    private static bool IsAltKey(Keys key)
    {
        return key is Keys.Menu or Keys.LMenu or Keys.RMenu;
    }

    private static bool IsCtrlKey(Keys key)
    {
        return key is Keys.ControlKey or Keys.LControlKey or Keys.RControlKey;
    }

    private static bool IsShiftKey(Keys key)
    {
        return key is Keys.ShiftKey or Keys.LShiftKey or Keys.RShiftKey;
    }

    private static bool IsAutoPauseDisabled()
    {
#if DEBUG
        const bool defaultValue = true;
#else
        const bool defaultValue = false;
#endif
        var value = Environment.GetEnvironmentVariable(DisableAutoPauseEnvName);
        if (string.IsNullOrWhiteSpace(value))
        {
            return defaultValue;
        }

        return value == "1" || value.Equals("true", StringComparison.OrdinalIgnoreCase);
    }

    private static uint GetProcessId(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
        {
            return 0;
        }

        NativeMethods.GetWindowThreadProcessId(handle, out var pid);
        return pid;
    }

    private void LogSnapshotIfNeeded(WindowSnapshot snapshot)
    {
        if (!VerboseLogging)
        {
            return;
        }

        var master = snapshot.MasterHandle == IntPtr.Zero
            ? "无"
            : $"0x{snapshot.MasterHandle.ToInt64():X}";
        var slaves = snapshot.SlaveHandles.Count == 0
            ? "无"
            : string.Join(", ", snapshot.SlaveHandles.Select(h => $"0x{h.ToInt64():X}"));
        var signature = $"{master}|{snapshot.ForegroundIsDnf}|{snapshot.ForegroundProcessId}|{slaves}";

        if (signature == _lastSnapshotSignature)
        {
            return;
        }

        _lastSnapshotSignature = signature;
        LogVerbose($"窗口扫描：总数={snapshot.TotalCount} 主控={master} 从控={slaves} 前台DNF={snapshot.ForegroundIsDnf} PID={snapshot.ForegroundProcessId}");
    }

    /// <summary>
    /// 停止扫描并卸载钩子。
    /// </summary>
    public void Dispose()
    {
        _scanTimer.Stop();
        _keyboardHook.KeyEvent -= OnKeyEvent;
        _keyboardHook.Dispose();
        _heartbeatTimer.Change(Timeout.Infinite, Timeout.Infinite);
        _heartbeatTimer.Dispose();
        _foregroundTimer.Change(Timeout.Infinite, Timeout.Infinite);
        _foregroundTimer.Dispose();
        _sharedMemory.Dispose();
        _hotkeyManager?.Dispose();
    }
}
