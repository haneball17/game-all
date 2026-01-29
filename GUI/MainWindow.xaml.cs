using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using GameHelperGUI.Services;
using GameHelperGUI.Views;
using DNFSyncBox.Views;

namespace GameAll.MasterGUI;

public partial class MainWindow : Window
{
    private readonly SharedMemoryStatusReader _helperReader = new();
    private readonly MasterGuiSettings _settings;
    private readonly DispatcherTimer _statusTimer;
    private HelperView? _helperView;
    private SyncView? _syncView;
    private string _lastModuleSummary = string.Empty;
    private string _lastInjectionSummary = string.Empty;

    public MainWindow()
    {
        InitializeComponent();
        _settings = MasterGuiSettings.Load(AppContext.BaseDirectory);
        LoadModulePages();
        DebugFileLogger.Log("主窗口初始化完成");

        _statusTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(_settings.StatusIntervalMs)
        };
        _statusTimer.Tick += (_, _) => UpdateStatus();
        Loaded += (_, _) =>
        {
            UpdateStatus();
            _statusTimer.Start();
        };
        Closed += (_, _) => _statusTimer.Stop();
    }

    private void LoadModulePages()
    {
        bool helperLoaded = TryAttachHelper();
        bool syncLoaded = TryAttachSync();

        if (helperLoaded && syncLoaded)
        {
            StatusText.Text = "状态：模块已加载";
        }
        else if (helperLoaded || syncLoaded)
        {
            StatusText.Text = "状态：部分模块加载失败";
        }
        else
        {
            StatusText.Text = "状态：模块加载失败";
        }
    }

    private bool TryAttachHelper()
    {
        try
        {
            _helperView = new HelperView();
            HelperHost.Content = _helperView;
            return true;
        }
        catch (Exception ex)
        {
            HelperHost.Content = BuildErrorContent($"Helper 模块加载失败：{ex.Message}");
            return false;
        }
    }

    private bool TryAttachSync()
    {
        try
        {
            _syncView = new SyncView();
            SyncHost.Content = _syncView;
            return true;
        }
        catch (Exception ex)
        {
            SyncHost.Content = BuildErrorContent($"Sync 模块加载失败：{ex.Message}");
            return false;
        }
    }

    private static UIElement BuildErrorContent(string message)
    {
        return new Grid
        {
            Children =
            {
                new TextBlock
                {
                    Text = message,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                }
            }
        };
    }

    private void OnOpenInjectorFolder(object sender, RoutedEventArgs e)
    {
        string baseDir = AppContext.BaseDirectory;
        string injectorDir = Path.Combine(baseDir, "Injector");
        string target = Directory.Exists(injectorDir) ? injectorDir : baseDir;

        Process.Start(new ProcessStartInfo
        {
            FileName = target,
            UseShellExecute = true
        });
    }

    private void UpdateStatus()
    {
        ModuleStatus helperStatus = GetHelperStatus(out bool helperOk);
        ModuleStatus syncStatus = GetSyncStatus(out bool syncOk);

        string moduleSummary = $"状态：{FormatModuleStatus("Helper", helperStatus)} | {FormatModuleStatus("Sync", syncStatus)}";
        StatusText.Text = moduleSummary;
        if (!string.Equals(moduleSummary, _lastModuleSummary, StringComparison.Ordinal))
        {
            DebugFileLogger.Log(moduleSummary);
            _lastModuleSummary = moduleSummary;
        }

        string injectionSummary = BuildInjectionStatus(helperOk, syncOk, helperStatus, syncStatus);
        InjectorText.Text = injectionSummary;
        if (!string.Equals(injectionSummary, _lastInjectionSummary, StringComparison.Ordinal))
        {
            DebugFileLogger.Log(injectionSummary);
            _lastInjectionSummary = injectionSummary;
        }
    }

    private string BuildInjectionStatus(bool helperOk, bool syncOk, ModuleStatus helperStatus, ModuleStatus syncStatus)
    {
        bool injected = _settings.RequireBothModulesForInjected ? helperOk && syncOk : helperOk || syncOk;
        bool injectorRunning = IsAnyProcessRunning(_settings.InjectorProcessNames);

        string injectionState;
        if (injected)
        {
            injectionState = "已注入";
        }
        else if (helperStatus == ModuleStatus.ProtocolMismatch || syncStatus == ModuleStatus.ProtocolMismatch)
        {
            injectionState = "协议不匹配";
        }
        else if (helperStatus == ModuleStatus.HeartbeatTimeout || syncStatus == ModuleStatus.HeartbeatTimeout)
        {
            injectionState = "心跳超时";
        }
        else if (helperStatus == ModuleStatus.NotFound && syncStatus == ModuleStatus.NotFound)
        {
            injectionState = "未注入";
        }
        else
        {
            injectionState = "未连接";
        }

        string injectorState = injectorRunning ? "注入器运行中" : "注入器未运行";
        return $"注入：{injectionState}（{injectorState}）";
    }

    private static bool IsAnyProcessRunning(string[] names)
    {
        try
        {
            foreach (string name in names)
            {
                using var process = Process.GetProcessesByName(name).FirstOrDefault();
                if (process != null)
                {
                    return true;
                }
            }
            return false;
        }
        catch
        {
            return false;
        }
    }

    private ModuleStatus GetHelperStatus(out bool ok)
    {
        ok = false;
        bool foundProcess = false;
        bool stale = false;
        bool mismatch = false;

        foreach (string name in _settings.TargetProcessNames)
        {
            var processes = Array.Empty<Process>();
            try
            {
                processes = Process.GetProcessesByName(name);
                foreach (var process in processes)
                {
                    foundProcess = true;
                    var status = _helperReader.TryRead((uint)process.Id, out var snapshot);
                    if (status == SharedMemoryReadStatus.Ok)
                    {
                        ulong now = (ulong)Environment.TickCount64;
                        ulong delta = now >= snapshot.LastTickMs ? now - snapshot.LastTickMs : 0;
                        if (delta <= (ulong)_settings.HelperHeartbeatTimeoutMs)
                        {
                            ok = true;
                        }
                        else
                        {
                            stale = true;
                        }
                        break;
                    }
                    if (status == SharedMemoryReadStatus.VersionMismatch)
                    {
                        mismatch = true;
                    }
                }
            }
            catch
            {
                // 忽略单个进程读取异常，避免影响整体状态刷新
            }
            finally
            {
                foreach (var process in processes)
                {
                    process.Dispose();
                }
            }

            if (ok)
            {
                break;
            }
        }

        if (ok)
        {
            return ModuleStatus.Ok;
        }
        if (stale)
        {
            return ModuleStatus.HeartbeatTimeout;
        }
        if (mismatch)
        {
            return ModuleStatus.ProtocolMismatch;
        }
        if (foundProcess)
        {
            return ModuleStatus.NotFound;
        }
        return ModuleStatus.NoProcess;
    }

    private ModuleStatus GetSyncStatus(out bool ok)
    {
        ok = false;
        SyncReadStatus status = TryReadSyncLastTick(out ulong lastTick);
        if (status == SyncReadStatus.Ok)
        {
            ulong now = (ulong)Environment.TickCount64;
            ulong delta = now >= lastTick ? now - lastTick : 0;
            if (delta <= (ulong)_settings.SyncHeartbeatTimeoutMs)
            {
                ok = true;
                return ModuleStatus.Ok;
            }
            return ModuleStatus.HeartbeatTimeout;
        }
        return status switch
        {
            SyncReadStatus.VersionMismatch => ModuleStatus.ProtocolMismatch,
            SyncReadStatus.SizeMismatch => ModuleStatus.ReadFailed,
            SyncReadStatus.NotFound => ModuleStatus.NotFound,
            _ => ModuleStatus.ReadFailed
        };
    }

    private SyncReadStatus TryReadSyncLastTick(out ulong lastTick)
    {
        lastTick = 0;
        try
        {
            using var mapping = MemoryMappedFile.OpenExisting(_settings.SyncMappingName, MemoryMappedFileRights.Read);
            using var accessor = mapping.CreateViewAccessor(0, _settings.SyncMappingSize, MemoryMappedFileAccess.Read);
            if (accessor.Capacity < _settings.SyncMappingSize)
            {
                return SyncReadStatus.SizeMismatch;
            }

            accessor.Read(0, out uint version);
            if (version != _settings.SyncVersion)
            {
                return SyncReadStatus.VersionMismatch;
            }

            accessor.Read(24, out ulong tick);
            lastTick = tick;
            return SyncReadStatus.Ok;
        }
        catch (FileNotFoundException)
        {
            return SyncReadStatus.NotFound;
        }
        catch (UnauthorizedAccessException)
        {
            return SyncReadStatus.ReadFailed;
        }
        catch (IOException)
        {
            return SyncReadStatus.ReadFailed;
        }
    }

    private static string FormatModuleStatus(string label, ModuleStatus status)
    {
        string text = status switch
        {
            ModuleStatus.Ok => "正常",
            ModuleStatus.HeartbeatTimeout => "心跳超时",
            ModuleStatus.ProtocolMismatch => "协议不匹配",
            ModuleStatus.NotFound => "未连接",
            ModuleStatus.NoProcess => "未找到进程",
            _ => "读取失败"
        };
        return $"{label}={text}";
    }

    private enum ModuleStatus
    {
        Ok,
        HeartbeatTimeout,
        ProtocolMismatch,
        NotFound,
        NoProcess,
        ReadFailed
    }

    private enum SyncReadStatus
    {
        Ok,
        NotFound,
        VersionMismatch,
        SizeMismatch,
        ReadFailed
    }
}
