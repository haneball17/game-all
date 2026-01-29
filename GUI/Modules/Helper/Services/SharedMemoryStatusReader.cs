using System;
using System.Collections.Generic;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Threading;
using GameHelperGUI.Models;

namespace GameHelperGUI.Services;

public enum SharedMemoryReadStatus
{
    Ok,
    NotFound,
    VersionMismatch,
    ReadFailed
}

public sealed class SharedMemoryStatusReader
{
    private static readonly string[] MappingPrefixes =
    {
        "Local\\GameHelperStatus_",
        "Global\\GameHelperStatus_"
    };

    private const uint ExpectedVersion = 3;
    private static int _protocolLogged;

    public SharedMemoryReadStatus TryRead(uint pid, out HelperStatusSnapshot snapshot)
    {
        snapshot = new HelperStatusSnapshot();
        foreach (string prefix in MappingPrefixes)
        {
            string mappingName = prefix + pid;
            var status = TryReadInternal(mappingName, out snapshot);
            if (status == SharedMemoryReadStatus.NotFound)
            {
                continue;
            }
            return status;
        }
        return SharedMemoryReadStatus.NotFound;
    }

    private unsafe SharedMemoryReadStatus TryReadInternal(string mappingName, out HelperStatusSnapshot snapshot)
    {
        snapshot = new HelperStatusSnapshot();
        try
        {
            using var mapping = MemoryMappedFile.OpenExisting(mappingName, MemoryMappedFileRights.Read);
            using var accessor = mapping.CreateViewAccessor(0, Marshal.SizeOf<HelperStatusV2>(), MemoryMappedFileAccess.Read);
            accessor.Read(0, out HelperStatusV2 raw);
            uint expectedSize = (uint)Marshal.SizeOf<HelperStatusV2>();
            if (raw.Version != ExpectedVersion || raw.Size != expectedSize)
            {
                if (Interlocked.Exchange(ref _protocolLogged, 1) == 0)
                {
                    GuiLogger.Warn("protocol_mismatch", "helper_status_mismatch", new Dictionary<string, object?>
                    {
                        ["expected_version"] = ExpectedVersion,
                        ["expected_size"] = expectedSize,
                        ["actual_version"] = raw.Version,
                        ["actual_size"] = raw.Size,
                        ["mapping"] = mappingName
                    });
                }
                return SharedMemoryReadStatus.VersionMismatch;
            }
            HelperStatusV2* ptr = &raw;
            string name = new string(ptr->PlayerName).TrimEnd('\0');
            snapshot = new HelperStatusSnapshot
            {
                LastTickMs = raw.LastTickMs,
                Pid = raw.Pid,
                ProcessAlive = raw.ProcessAlive != 0,
                AutoTransparentEnabled = raw.AutoTransparentEnabled != 0,
                FullscreenAttackTarget = raw.FullscreenAttackTarget != 0,
                FullscreenAttackPatchOn = raw.FullscreenAttackPatchOn != 0,
                AttractMode = raw.AttractMode,
                AttractPositive = raw.AttractPositive != 0,
                SummonEnabled = raw.SummonEnabled != 0,
                SummonLastTick = raw.SummonLastTick,
                FullscreenSkillEnabled = raw.FullscreenSkillEnabled != 0,
                FullscreenSkillActive = raw.FullscreenSkillActive != 0,
                FullscreenSkillHotkey = raw.FullscreenSkillHotkey,
                HotkeyEnabled = raw.HotkeyEnabled != 0,
                PlayerName = name
            };
            return SharedMemoryReadStatus.Ok;
        }
        catch (FileNotFoundException)
        {
            return SharedMemoryReadStatus.NotFound;
        }
        catch (IOException)
        {
            return SharedMemoryReadStatus.ReadFailed;
        }
        catch (UnauthorizedAccessException)
        {
            return SharedMemoryReadStatus.ReadFailed;
        }
    }
}
