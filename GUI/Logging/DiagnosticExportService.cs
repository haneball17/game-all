using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace GameAll.MasterGUI;

internal sealed class DiagnosticExportService
{
    private static readonly string[] LogDirectories =
    {
        "master",
        "injector",
        "sync",
        Path.Combine("sync", "gui"),
        Path.Combine("sync", "payload"),
        "helper",
        "payload",
        "debug"
    };

    private static readonly string[] ConfigFiles =
    {
        "injector.ini",
        "mastergui.json",
        "payload.ini",
        "game_helper.ini",
        "sync_debug.ini",
        "sync_hotkey.ini",
        "profiles.json",
        "params.json"
    };

    private static readonly string[] SyncFocusKeywords =
    {
        "[STATE]",
        "[PAUSE]",
        "[EDGE]",
        "[EMIT]",
        "[GROUP]",
        "[REPEAT]",
        "[KEY]",
        "[KEYFIX]",
        "[KEYWARN]",
        "[STAT]",
        "[RAW]",
        "[RAWB]",
        "[OBS]",
        "[RUSTDIAG]",
        "DirectInput",
        "Spoof",
        "protocol_mismatch"
    };

    private const int MaxInlineFileBytes = 2 * 1024 * 1024;

    public string Export(string baseDir, string moduleSummary, string injectionSummary)
    {
        string logsDir = Path.Combine(baseDir, "logs");
        Directory.CreateDirectory(logsDir);

        string sessionId = ReadSessionId(logsDir);
        string exportsDir = Path.Combine(logsDir, "exports");
        Directory.CreateDirectory(exportsDir);

        string exportPath = Path.Combine(
            exportsDir,
            $"diagnostic_{sessionId}_{DateTime.Now:yyyyMMdd_HHmmss}.log");

        var builder = new StringBuilder(capacity: 64 * 1024);
        builder.AppendLine("# game-all 诊断导出");
        builder.AppendLine($"导出时间：{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}");
        builder.AppendLine($"SessionId：{sessionId}");
        builder.AppendLine($"BaseDir：{baseDir}");
        builder.AppendLine($"状态摘要：{moduleSummary}");
        builder.AppendLine($"注入摘要：{injectionSummary}");
        builder.AppendLine();

        AppendProcessSnapshot(builder);
        AppendConfigSnapshot(builder, baseDir);
        AppendSuccessFiles(builder, logsDir);

        var collectedFiles = CollectLogFiles(logsDir, sessionId);
        AppendCollectedFiles(builder, collectedFiles);
        AppendSyncFocusLines(builder, collectedFiles);

        File.WriteAllText(exportPath, builder.ToString(), new UTF8Encoding(encoderShouldEmitUTF8Identifier: true));
        return exportPath;
    }

    private static string ReadSessionId(string logsDir)
    {
        try
        {
            string sessionPath = Path.Combine(logsDir, "session.current");
            if (File.Exists(sessionPath))
            {
                string text = ReadAllTextShared(sessionPath).Trim();
                if (!string.IsNullOrWhiteSpace(text))
                {
                    return text;
                }
            }
        }
        catch
        {
            // 读取失败时回退到当前时间，避免阻塞导出。
        }

        return DateTime.Now.ToString("yyyyMMdd_HHmmss");
    }

    private static void AppendProcessSnapshot(StringBuilder builder)
    {
        builder.AppendLine("## 进程快照");
        builder.AppendLine($"当前 GUI 进程：PID={Environment.ProcessId}");

        try
        {
            var interesting = Process.GetProcesses()
                .Where(p => !string.IsNullOrWhiteSpace(p.ProcessName))
                .OrderBy(p => p.ProcessName, StringComparer.OrdinalIgnoreCase)
                .ThenBy(p => p.Id)
                .Take(200)
                .ToArray();

            foreach (var process in interesting)
            {
                try
                {
                    builder.AppendLine($"- {process.ProcessName} ({process.Id})");
                }
                finally
                {
                    process.Dispose();
                }
            }
        }
        catch (Exception ex)
        {
            builder.AppendLine($"- 读取进程快照失败：{ex.Message}");
        }

        builder.AppendLine();
    }

    private static void AppendConfigSnapshot(StringBuilder builder, string baseDir)
    {
        builder.AppendLine("## 配置快照");
        string configDir = Path.Combine(baseDir, "config");

        foreach (string fileName in ConfigFiles)
        {
            string path = Path.Combine(configDir, fileName);
            builder.AppendLine($"### {fileName}");
            builder.AppendLine($"路径：{path}");

            if (!File.Exists(path))
            {
                builder.AppendLine("未找到");
                builder.AppendLine();
                continue;
            }

            try
            {
                builder.AppendLine("```text");
                builder.AppendLine(ReadAllTextShared(path));
                builder.AppendLine("```");
            }
            catch (Exception ex)
            {
                builder.AppendLine($"读取失败：{ex.Message}");
            }

            builder.AppendLine();
        }
    }

    private static void AppendSuccessFiles(StringBuilder builder, string logsDir)
    {
        builder.AppendLine("## SuccessFile");
        try
        {
            var files = Directory.Exists(logsDir)
                ? Directory.EnumerateFiles(logsDir, "successfile_*.txt", SearchOption.TopDirectoryOnly)
                    .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
                    .ToArray()
                : Array.Empty<string>();

            if (files.Length == 0)
            {
                builder.AppendLine("未找到 successfile");
            }
            else
            {
                foreach (string file in files)
                {
                    builder.AppendLine($"- {Path.GetFileName(file)}");
                }
            }
        }
        catch (Exception ex)
        {
            builder.AppendLine($"读取 successfile 失败：{ex.Message}");
        }

        builder.AppendLine();
    }

    private static IReadOnlyList<string> CollectLogFiles(string logsDir, string sessionId)
    {
        var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        string sessionArchiveDir = Path.Combine(logsDir, $"session_{sessionId}");
        if (Directory.Exists(sessionArchiveDir))
        {
            foreach (string file in Directory.EnumerateFiles(sessionArchiveDir, "*.log*", SearchOption.TopDirectoryOnly))
            {
                result.Add(file);
            }
        }

        foreach (string relativeDir in LogDirectories)
        {
            string fullDir = Path.Combine(logsDir, relativeDir);
            if (!Directory.Exists(fullDir))
            {
                continue;
            }

            int beforeCount = result.Count;
            foreach (string file in Directory.EnumerateFiles(fullDir, $"*{sessionId}*.log*", SearchOption.TopDirectoryOnly))
            {
                result.Add(file);
            }

            if (result.Count == beforeCount)
            {
                foreach (string file in Directory.EnumerateFiles(fullDir, "*.log*", SearchOption.TopDirectoryOnly)
                             .OrderByDescending(File.GetLastWriteTime)
                             .Take(3))
                {
                    result.Add(file);
                }
            }
        }

        string syncPayloadDir = Path.Combine(logsDir, "sync", "payload");
        AddCurrentSessionPreferredLogs(result, syncPayloadDir, sessionId, "sync_payload_");

        string syncGuiDir = Path.Combine(logsDir, "sync", "gui");
        AddCurrentSessionPreferredLogs(result, syncGuiDir, sessionId, "sync_gui_");

        return result
            .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static void AddCurrentSessionPreferredLogs(HashSet<string> result, string fullDir, string sessionId, string prefix)
    {
        if (!Directory.Exists(fullDir))
        {
            return;
        }

        foreach (string file in Directory.EnumerateFiles(fullDir, $"{prefix}{sessionId}_*.log*", SearchOption.TopDirectoryOnly))
        {
            result.Add(file);
        }
    }

    private static void AppendCollectedFiles(StringBuilder builder, IReadOnlyList<string> files)
    {
        builder.AppendLine("## 原始日志");
        if (files.Count == 0)
        {
            builder.AppendLine("未找到匹配日志");
            builder.AppendLine();
            return;
        }

        foreach (string file in files)
        {
            builder.AppendLine($"### {Path.GetFileName(file)}");
            builder.AppendLine($"路径：{file}");
            try
            {
                var info = new FileInfo(file);
                builder.AppendLine($"大小：{info.Length} bytes");
                builder.AppendLine("```text");
                builder.Append(ReadLogFileForReport(file, info.Length));
                builder.AppendLine();
                builder.AppendLine("```");
            }
            catch (Exception ex)
            {
                builder.AppendLine($"读取失败：{ex.Message}");
            }

            builder.AppendLine();
        }
    }

    private static void AppendSyncFocusLines(StringBuilder builder, IReadOnlyList<string> files)
    {
        builder.AppendLine("## Sync 关键摘录");
        bool found = false;

        foreach (string file in files)
        {
            string[] lines;
            try
            {
                lines = ReadAllLinesShared(file);
            }
            catch
            {
                continue;
            }

            var matched = lines
                .Where(line => SyncFocusKeywords.Any(keyword => line.Contains(keyword, StringComparison.OrdinalIgnoreCase)))
                .ToArray();

            if (matched.Length == 0)
            {
                continue;
            }

            found = true;
            builder.AppendLine($"### {Path.GetFileName(file)}");
            builder.AppendLine("```text");
            foreach (string line in matched)
            {
                builder.AppendLine(line);
            }
            builder.AppendLine("```");
            builder.AppendLine();
        }

        if (!found)
        {
            builder.AppendLine("未匹配到关键摘录");
            builder.AppendLine();
        }
    }

    private static string ReadLogFileForReport(string path, long fileLength)
    {
        string text = ReadAllTextShared(path);
        if (fileLength <= MaxInlineFileBytes)
        {
            return text;
        }

        int totalLength = text.Length;
        int headLength = totalLength / 5;
        int tailStart = totalLength / 5 * 2;
        if (tailStart >= totalLength)
        {
            tailStart = totalLength / 2;
        }

        string head = text[..Math.Min(headLength, totalLength)];
        string tail = text[Math.Min(tailStart, totalLength)..];
        return head + Environment.NewLine + "... truncated ..." + Environment.NewLine + tail;
    }

    private static string ReadAllTextShared(string path)
    {
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream, detectEncodingFromByteOrderMarks: true);
            return reader.ReadToEnd();
        }
        catch (IOException)
        {
            string tempPath = Path.Combine(Path.GetTempPath(), $"game-all-diag-{Guid.NewGuid():N}.tmp");
            File.Copy(path, tempPath, overwrite: true);
            try
            {
                return File.ReadAllText(tempPath, Encoding.UTF8);
            }
            finally
            {
                try
                {
                    File.Delete(tempPath);
                }
                catch
                {
                    // 忽略临时文件删除失败，不影响诊断导出。
                }
            }
        }
    }

    private static string[] ReadAllLinesShared(string path)
    {
        string text = ReadAllTextShared(path);
        return text.Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Split('\n');
    }
}
