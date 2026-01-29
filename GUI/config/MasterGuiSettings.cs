using System;
using System.IO;
using System.Text.Json;

namespace GameAll.MasterGUI;

public sealed class MasterGuiSettings
{
    public string[] TargetProcessNames { get; set; } = { "DNF", "dnf" };
    public int HelperHeartbeatTimeoutMs { get; set; } = 2000;
    public int SyncHeartbeatTimeoutMs { get; set; } = 2000;
    public int StatusIntervalMs { get; set; } = 1000;
    public string SyncMappingName { get; set; } = "Local\\DNFSyncBox.KeyboardState.V2";
    public uint SyncVersion { get; set; } = 2;
    public int SyncMappingSize { get; set; } = 1824;
    public string[] InjectorProcessNames { get; set; } = { "game-injector", "Injector" };
    public bool RequireBothModulesForInjected { get; set; } = false;

    public static MasterGuiSettings Load(string baseDirectory)
    {
        var settings = new MasterGuiSettings();
        string configPath = Path.Combine(baseDirectory, "config", "mastergui.json");
        if (!File.Exists(configPath))
        {
            return settings;
        }

        try
        {
            var options = new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true
            };
            var loaded = JsonSerializer.Deserialize<MasterGuiSettings>(File.ReadAllText(configPath), options);
            if (loaded == null)
            {
                return settings;
            }

            if (loaded.TargetProcessNames is { Length: > 0 })
            {
                settings.TargetProcessNames = loaded.TargetProcessNames;
            }
            if (loaded.HelperHeartbeatTimeoutMs > 0)
            {
                settings.HelperHeartbeatTimeoutMs = loaded.HelperHeartbeatTimeoutMs;
            }
            if (loaded.SyncHeartbeatTimeoutMs > 0)
            {
                settings.SyncHeartbeatTimeoutMs = loaded.SyncHeartbeatTimeoutMs;
            }
            if (loaded.StatusIntervalMs > 0)
            {
                settings.StatusIntervalMs = loaded.StatusIntervalMs;
            }
            if (!string.IsNullOrWhiteSpace(loaded.SyncMappingName))
            {
                settings.SyncMappingName = loaded.SyncMappingName;
            }
            if (loaded.SyncVersion > 0)
            {
                settings.SyncVersion = loaded.SyncVersion;
            }
            if (loaded.SyncMappingSize > 0)
            {
                settings.SyncMappingSize = loaded.SyncMappingSize;
            }
            if (loaded.InjectorProcessNames is { Length: > 0 })
            {
                settings.InjectorProcessNames = loaded.InjectorProcessNames;
            }
            settings.RequireBothModulesForInjected = loaded.RequireBothModulesForInjected;
        }
        catch
        {
            // 配置读取失败时使用默认值，避免阻塞 GUI 启动
        }

        return settings;
    }
}
