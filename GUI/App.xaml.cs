using System;
using System.Windows;
using System.Windows.Threading;

namespace GameAll.MasterGUI;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        SessionLogManager.EnsureSession();
        DebugFileLogger.Log("MasterGUI 启动");
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            DebugFileLogger.Log($"未处理异常: {args.ExceptionObject}");
        };
        DispatcherUnhandledException += (_, args) =>
        {
            DebugFileLogger.Log($"UI 未处理异常: {args.Exception}");
            args.Handled = true;
        };

        base.OnStartup(e);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SessionLogManager.ArchiveSessionLogs();
        base.OnExit(e);
    }
}
