using System;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace DNFSyncBox;

internal sealed class SyncHotkeyManager : IDisposable
{
    internal static SyncHotkeyDefinition DefaultHotkey { get; } = SyncHotkeyDefinition.CreateDefault();

    private readonly object _lock = new();
    private readonly Action<string>? _log;
    private readonly string _configPath;
    private readonly FileSystemWatcher _watcher;
    private readonly Timer _reloadTimer;
    private SyncHotkeyDefinition _current = DefaultHotkey;

    public SyncHotkeyManager(string baseDirectory, Action<string>? log)
    {
        _log = log;
        string configDir = Path.Combine(baseDirectory, "config");
        Directory.CreateDirectory(configDir);
        _configPath = Path.Combine(configDir, "sync_hotkey.ini");
        EnsureDefaultConfig();
        Reload();

        _reloadTimer = new Timer(_ => ReloadSafe(), null, Timeout.Infinite, Timeout.Infinite);
        _watcher = new FileSystemWatcher(configDir, Path.GetFileName(_configPath))
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size
        };
        _watcher.Changed += (_, _) => ScheduleReload();
        _watcher.Created += (_, _) => ScheduleReload();
        _watcher.Renamed += (_, _) => ScheduleReload();
        _watcher.EnableRaisingEvents = true;
    }

    public SyncHotkeyDefinition Current
    {
        get
        {
            lock (_lock)
            {
                return _current;
            }
        }
    }

    public void Dispose()
    {
        _watcher.Dispose();
        _reloadTimer.Dispose();
    }

    private void ScheduleReload()
    {
        _reloadTimer.Change(200, Timeout.Infinite);
    }

    private void ReloadSafe()
    {
        try
        {
            Reload();
        }
        catch (Exception ex)
        {
            _log?.Invoke($"热键配置重载失败：{ex.Message}");
        }
    }

    private void EnsureDefaultConfig()
    {
        if (File.Exists(_configPath))
        {
            return;
        }

        const string content = "[hotkey]\r\n" +
                              "; 切换同步热键（示例：Alt+. / Ctrl+F10）\r\n" +
                              "toggle_sync=Alt+.\r\n";
        File.WriteAllText(_configPath, content);
    }

    private void Reload()
    {
        string? raw = ReadHotkeyValue();
        if (!TryParseHotkey(raw, out var parsed, out var error))
        {
            _log?.Invoke($"热键配置无效，已回退默认值：{error}");
            parsed = DefaultHotkey;
        }

        lock (_lock)
        {
            if (_current.Equals(parsed))
            {
                return;
            }
            _current = parsed;
        }

        _log?.Invoke($"热键已更新：{parsed.Display}");
    }

    private string? ReadHotkeyValue()
    {
        if (!File.Exists(_configPath))
        {
            return null;
        }

        foreach (var line in File.ReadAllLines(_configPath))
        {
            var trimmed = line.Trim();
            if (trimmed.Length == 0 || trimmed.StartsWith(";") || trimmed.StartsWith("#"))
            {
                continue;
            }
            var index = trimmed.IndexOf('=');
            if (index <= 0)
            {
                continue;
            }
            var key = trimmed[..index].Trim();
            if (!key.Equals("toggle_sync", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            return trimmed[(index + 1)..].Trim();
        }

        return null;
    }

    private static bool TryParseHotkey(string? raw, out SyncHotkeyDefinition hotkey, out string error)
    {
        hotkey = DefaultHotkey;
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(raw))
        {
            error = "空值";
            return false;
        }

        bool alt = false;
        bool ctrl = false;
        bool shift = false;
        Keys key = Keys.None;

        var tokens = raw.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        foreach (var token in tokens)
        {
            var normalized = token.Trim();
            if (normalized.Length == 0)
            {
                continue;
            }

            if (normalized.Equals("alt", StringComparison.OrdinalIgnoreCase))
            {
                alt = true;
                continue;
            }
            if (normalized.Equals("ctrl", StringComparison.OrdinalIgnoreCase) ||
                normalized.Equals("control", StringComparison.OrdinalIgnoreCase))
            {
                ctrl = true;
                continue;
            }
            if (normalized.Equals("shift", StringComparison.OrdinalIgnoreCase))
            {
                shift = true;
                continue;
            }

            if (!TryParseKey(normalized, out key))
            {
                error = $"无法识别键值：{normalized}";
                return false;
            }
        }

        if (key == Keys.None)
        {
            error = "未指定主键";
            return false;
        }

        hotkey = new SyncHotkeyDefinition(key, alt, ctrl, shift);
        return true;
    }

    private static bool TryParseKey(string token, out Keys key)
    {
        key = Keys.None;
        if (token == "." || token.Equals("period", StringComparison.OrdinalIgnoreCase) ||
            token.Equals("oemperiod", StringComparison.OrdinalIgnoreCase))
        {
            key = Keys.OemPeriod;
            return true;
        }

        if (token.Length == 1)
        {
            char ch = token[0];
            if (char.IsLetter(ch))
            {
                key = (Keys)Enum.Parse(typeof(Keys), char.ToUpperInvariant(ch).ToString());
                return true;
            }
            if (char.IsDigit(ch))
            {
                key = (Keys)((int)Keys.D0 + (ch - '0'));
                return true;
            }
        }

        if (token.StartsWith("F", StringComparison.OrdinalIgnoreCase) && token.Length <= 3)
        {
            if (int.TryParse(token[1..], out int fKey) && fKey is >= 1 and <= 24)
            {
                key = Keys.F1 + (fKey - 1);
                return true;
            }
        }

        if (token.Equals("space", StringComparison.OrdinalIgnoreCase))
        {
            key = Keys.Space;
            return true;
        }
        if (token.Equals("tab", StringComparison.OrdinalIgnoreCase))
        {
            key = Keys.Tab;
            return true;
        }
        if (token.Equals("enter", StringComparison.OrdinalIgnoreCase))
        {
            key = Keys.Enter;
            return true;
        }
        if (token.Equals("esc", StringComparison.OrdinalIgnoreCase) || token.Equals("escape", StringComparison.OrdinalIgnoreCase))
        {
            key = Keys.Escape;
            return true;
        }

        return Enum.TryParse(token, true, out key);
    }
}

internal readonly struct SyncHotkeyDefinition
{
    public Keys Key { get; }
    public bool RequireAlt { get; }
    public bool RequireCtrl { get; }
    public bool RequireShift { get; }
    public string Display { get; }

    public SyncHotkeyDefinition(Keys key, bool requireAlt, bool requireCtrl, bool requireShift)
    {
        Key = key;
        RequireAlt = requireAlt;
        RequireCtrl = requireCtrl;
        RequireShift = requireShift;
        Display = BuildDisplay(key, requireAlt, requireCtrl, requireShift);
    }

    public static SyncHotkeyDefinition CreateDefault()
    {
        return new SyncHotkeyDefinition(Keys.OemPeriod, true, false, false);
    }

    public bool MatchesModifiers(bool altDown, bool ctrlDown, bool shiftDown)
    {
        return altDown == RequireAlt && ctrlDown == RequireCtrl && shiftDown == RequireShift;
    }

    private static string BuildDisplay(Keys key, bool alt, bool ctrl, bool shift)
    {
        var parts = new System.Collections.Generic.List<string>();
        if (ctrl) parts.Add("Ctrl");
        if (alt) parts.Add("Alt");
        if (shift) parts.Add("Shift");
        parts.Add(KeyToDisplay(key));
        return string.Join("+", parts);
    }

    private static string KeyToDisplay(Keys key)
    {
        return key switch
        {
            Keys.OemPeriod => ".",
            Keys.Space => "Space",
            Keys.Tab => "Tab",
            Keys.Enter => "Enter",
            Keys.Escape => "Esc",
            _ => key.ToString()
        };
    }
}
