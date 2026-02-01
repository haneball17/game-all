using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;

namespace GameAll.MasterGUI;

internal static class DebugFileLogger
{
    private static readonly object LogLock = new();
    private static readonly string LogDirectory = Path.Combine(AppContext.BaseDirectory, "logs", "master");
    private static readonly string LogPath = Path.Combine(
        LogDirectory,
        $"master_{SessionLogManager.SessionId}_{Environment.ProcessId}.log");

    // 仅在 Debug 构建输出日志，便于定位注入与心跳问题。
    [Conditional("DEBUG")]
    public static void Log(string message, [CallerMemberName] string? member = null)
    {
        try
        {
            lock (LogLock)
            {
                Directory.CreateDirectory(LogDirectory);
                string line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{member}] {message}";
                File.AppendAllText(LogPath, line + Environment.NewLine);
            }
        }
        catch
        {
            // Debug 日志失败不影响主流程。
        }
    }
}
