using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;

namespace GameHelperGUI.Services;

public enum GuiLogLevel
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3
}

public static class GuiLogger
{
    private static readonly object SyncRoot = new();
    private static StreamWriter? _writer;
    private static GuiLogLevel _level = GuiLogLevel.Info;
    private static bool _consoleOutput;
    private static bool _initialized;
    private static string? _logPath;
    private static string? _baseDir;

    public static void Initialize(string configPath, string baseDir)
    {
        if (_initialized)
        {
            return;
        }
        _baseDir = baseDir;
        var ini = new IniFile(configPath);
        string levelValue = ini.ReadString("gui_log", "log_level", "INFO");
        string defaultLogPath = BuildDefaultLogPath(baseDir);
        string rawLogPath = ini.ReadString("gui_log", "log_path", "auto");
        bool consoleOutput = ini.ReadBool("gui_log", "console_output", false);

        string logPath = ResolveLogPath(rawLogPath, defaultLogPath);
        if (!Path.IsPathRooted(logPath))
        {
            logPath = Path.Combine(baseDir, logPath);
        }

        _level = ParseLevel(levelValue);
        _consoleOutput = consoleOutput;
        Directory.CreateDirectory(Path.GetDirectoryName(logPath) ?? baseDir);
        _writer = new StreamWriter(new FileStream(logPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
        {
            AutoFlush = true
        };
        _logPath = logPath;
        _initialized = true;
        Info("startup", "gui_logger_ready", new Dictionary<string, object?>
        {
            ["log_path"] = logPath,
            ["log_level"] = _level.ToString().ToUpperInvariant()
        });
    }

    public static void Debug(string evt, string message, Dictionary<string, object?>? extras = null)
    {
        Log(GuiLogLevel.Debug, evt, message, extras);
    }

    public static void Info(string evt, string message, Dictionary<string, object?>? extras = null)
    {
        Log(GuiLogLevel.Info, evt, message, extras);
    }

    public static void Warn(string evt, string message, Dictionary<string, object?>? extras = null)
    {
        Log(GuiLogLevel.Warn, evt, message, extras);
    }

    public static void Error(string evt, string message, Dictionary<string, object?>? extras = null)
    {
        Log(GuiLogLevel.Error, evt, message, extras);
    }

    public static void Log(GuiLogLevel level, string evt, string message, Dictionary<string, object?>? extras = null)
    {
        if (!_initialized)
        {
            return;
        }
        if (level < _level)
        {
            return;
        }
        var payload = new Dictionary<string, object?>
        {
            ["ts"] = DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss.fff", CultureInfo.InvariantCulture),
            ["level"] = level.ToString().ToUpperInvariant(),
            ["event"] = evt,
            ["message"] = message,
            ["pid"] = Environment.ProcessId
        };
        if (extras != null)
        {
            foreach (var item in extras)
            {
                payload[item.Key] = item.Value;
            }
        }

        string json = JsonSerializer.Serialize(payload);
        lock (SyncRoot)
        {
            _writer?.WriteLine(json);
        }
        if (_consoleOutput)
        {
            Console.WriteLine(json);
        }
    }

    private static GuiLogLevel ParseLevel(string value)
    {
        if (string.Equals(value, "DEBUG", StringComparison.OrdinalIgnoreCase))
        {
            return GuiLogLevel.Debug;
        }
        if (string.Equals(value, "WARN", StringComparison.OrdinalIgnoreCase))
        {
            return GuiLogLevel.Warn;
        }
        if (string.Equals(value, "ERROR", StringComparison.OrdinalIgnoreCase))
        {
            return GuiLogLevel.Error;
        }
        return GuiLogLevel.Info;
    }

    private static string BuildDefaultLogPath(string baseDir)
    {
        string sessionId = ReadSessionId(baseDir);
        return Path.Combine("logs", "helper", $"helper_{sessionId}_{Environment.ProcessId}.log.jsonl");
    }

    private static string ResolveLogPath(string rawLogPath, string defaultLogPath)
    {
        if (string.IsNullOrWhiteSpace(rawLogPath))
        {
            return defaultLogPath;
        }

        string normalized = rawLogPath.Trim();
        if (string.Equals(normalized, "auto", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(normalized, "default", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(normalized, "logs\\gui.log.jsonl", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(normalized, "gui.log.jsonl", StringComparison.OrdinalIgnoreCase))
        {
            return defaultLogPath;
        }

        return normalized;
    }

    private static string ReadSessionId(string baseDir)
    {
        try
        {
            string sessionFile = Path.Combine(baseDir, "logs", "session.current");
            if (File.Exists(sessionFile))
            {
                string content = File.ReadAllText(sessionFile).Trim();
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

    public static void ArchiveToSessionDir()
    {
        try
        {
            if (!_initialized || string.IsNullOrWhiteSpace(_logPath) || string.IsNullOrWhiteSpace(_baseDir))
            {
                return;
            }

            string sessionFile = Path.Combine(_baseDir, "logs", "session.current");
            if (!File.Exists(sessionFile))
            {
                return;
            }

            string sessionId = File.ReadAllText(sessionFile).Trim();
            if (string.IsNullOrWhiteSpace(sessionId))
            {
                return;
            }

            string sessionDir = Path.Combine(_baseDir, "logs", $"session_{sessionId}");
            Directory.CreateDirectory(sessionDir);

            string fileName = Path.GetFileName(_logPath);
            if (string.IsNullOrWhiteSpace(fileName))
            {
                return;
            }

            string dest = Path.Combine(sessionDir, fileName);
            File.Copy(_logPath, dest, true);
        }
        catch
        {
            // 归档失败不影响主流程。
        }
    }
}
