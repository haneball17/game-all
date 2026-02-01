using System;
using System.IO;
using System.Linq;

namespace GameAll.MasterGUI;

internal static class SessionLogManager
{
    private static readonly object SyncRoot = new();
    private static string? _sessionId;

    public static string SessionId
    {
        get
        {
            EnsureSession();
            return _sessionId ?? DateTime.Now.ToString("yyyyMMdd_HHmmss");
        }
    }

    public static void EnsureSession()
    {
        lock (SyncRoot)
        {
            if (!string.IsNullOrWhiteSpace(_sessionId))
            {
                return;
            }

            _sessionId = DateTime.Now.ToString("yyyyMMdd_HHmmss");
            var baseDir = AppContext.BaseDirectory;
            var logsDir = Path.Combine(baseDir, "logs");
            Directory.CreateDirectory(logsDir);

            var sessionFile = Path.Combine(logsDir, "session.current");
            File.WriteAllText(sessionFile, _sessionId);
        }
    }

    public static void ArchiveSessionLogs()
    {
        lock (SyncRoot)
        {
            if (string.IsNullOrWhiteSpace(_sessionId))
            {
                return;
            }

            var baseDir = AppContext.BaseDirectory;
            var logsDir = Path.Combine(baseDir, "logs");
            var sessionDir = Path.Combine(logsDir, $"session_{_sessionId}");
            Directory.CreateDirectory(sessionDir);

            var moduleDirs = new[]
            {
                Path.Combine(logsDir, "master"),
                Path.Combine(logsDir, "sync"),
                Path.Combine(logsDir, "sync", "gui"),
                Path.Combine(logsDir, "sync", "payload"),
                Path.Combine(logsDir, "helper"),
                Path.Combine(logsDir, "injector"),
                Path.Combine(logsDir, "payload"),
                Path.Combine(logsDir, "debug")
            };

            foreach (var dir in moduleDirs)
            {
                if (!Directory.Exists(dir))
                {
                    continue;
                }

                var files = Directory.EnumerateFiles(dir, $"*_{_sessionId}_*.log*", SearchOption.TopDirectoryOnly);
                foreach (var file in files)
                {
                    var name = Path.GetFileName(file);
                    if (string.IsNullOrWhiteSpace(name))
                    {
                        continue;
                    }

                    var dest = Path.Combine(sessionDir, name);
                    try
                    {
                        File.Copy(file, dest, true);
                    }
                    catch
                    {
                        // 归档失败不影响主流程。
                    }
                }
            }
        }
    }
}
