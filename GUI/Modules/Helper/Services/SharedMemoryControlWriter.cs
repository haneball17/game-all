using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using GameHelperGUI.Models;

namespace GameHelperGUI.Services;

public enum SharedMemoryWriteStatus
{
    Ok,
    NotFound,
    WriteFailed
}

public sealed class SharedMemoryControlWriter
{
    private static readonly string[] MappingPrefixes =
    {
        "Local\\GameHelperControl_",
        "Global\\GameHelperControl_"
    };

    private const uint ExpectedVersionV1 = 1;
    private const uint ExpectedVersionV2 = 2;

    public SharedMemoryWriteStatus TryWrite(uint pid, HelperControlSnapshot snapshot)
    {
        foreach (string prefix in MappingPrefixes)
        {
            string mappingName = prefix + pid;
            var status = TryWriteInternal(mappingName, pid, snapshot);
            if (status == SharedMemoryWriteStatus.NotFound)
            {
                continue;
            }
            return status;
        }
        return SharedMemoryWriteStatus.NotFound;
    }

    private SharedMemoryWriteStatus TryWriteInternal(string mappingName, uint pid, HelperControlSnapshot snapshot)
    {
        try
        {
            using var mapping = MemoryMappedFile.OpenExisting(mappingName, MemoryMappedFileRights.ReadWrite);
            if (TryWriteV2(mapping, pid, snapshot))
            {
                return SharedMemoryWriteStatus.Ok;
            }
            if (TryWriteV1(mapping, pid, snapshot))
            {
                return SharedMemoryWriteStatus.Ok;
            }
            return SharedMemoryWriteStatus.WriteFailed;
        }
        catch (FileNotFoundException)
        {
            return SharedMemoryWriteStatus.NotFound;
        }
        catch (UnauthorizedAccessException)
        {
            return SharedMemoryWriteStatus.NotFound;
        }
        catch (IOException)
        {
            return SharedMemoryWriteStatus.WriteFailed;
        }
    }

    private static bool TryWriteV2(MemoryMappedFile mapping, uint pid, HelperControlSnapshot snapshot)
    {
        try
        {
            uint size = (uint)Marshal.SizeOf<HelperControlV2>();
            using var accessor = mapping.CreateViewAccessor(0, size, MemoryMappedFileAccess.Write);
            var raw = new HelperControlV2
            {
                Version = ExpectedVersionV2,
                Size = size,
                Pid = pid,
                LastUpdateTick = unchecked((uint)Environment.TickCount),
                FullscreenAttack = (byte)snapshot.FullscreenAttack,
                FullscreenSkill = (byte)snapshot.FullscreenSkill,
                AutoTransparent = (byte)snapshot.AutoTransparent,
                Attract = (byte)snapshot.Attract,
                HotkeyEnabled = (byte)snapshot.HotkeyEnabled,
                SummonSequence = snapshot.SummonSequence,
                ActionSequence = snapshot.ActionSequence,
                ActionMask = snapshot.ActionMask,
                DesiredFullscreenAttack = snapshot.DesiredFullscreenAttack ? (byte)1 : (byte)0,
                DesiredFullscreenSkill = snapshot.DesiredFullscreenSkill ? (byte)1 : (byte)0,
                DesiredAutoTransparent = snapshot.DesiredAutoTransparent ? (byte)1 : (byte)0,
                DesiredAttractEnabled = snapshot.DesiredAttractEnabled ? (byte)1 : (byte)0,
                DesiredAttractMode = snapshot.DesiredAttractMode,
                DesiredAttractPositive = snapshot.DesiredAttractPositive ? (byte)1 : (byte)0,
                DesiredHotkeyEnabled = snapshot.DesiredHotkeyEnabled ? (byte)1 : (byte)0
            };
            accessor.Write(0, ref raw);
            return true;
        }
        catch (ArgumentException)
        {
            return false;
        }
        catch (IOException)
        {
            return false;
        }
    }

    private static bool TryWriteV1(MemoryMappedFile mapping, uint pid, HelperControlSnapshot snapshot)
    {
        try
        {
            uint size = (uint)Marshal.SizeOf<HelperControlV1>();
            using var accessor = mapping.CreateViewAccessor(0, size, MemoryMappedFileAccess.Write);
            var raw = new HelperControlV1
            {
                Version = ExpectedVersionV1,
                Size = size,
                Pid = pid,
                LastUpdateTick = unchecked((uint)Environment.TickCount),
                FullscreenAttack = (byte)snapshot.FullscreenAttack,
                FullscreenSkill = (byte)snapshot.FullscreenSkill,
                AutoTransparent = (byte)snapshot.AutoTransparent,
                Attract = (byte)snapshot.Attract,
                HotkeyEnabled = (byte)snapshot.HotkeyEnabled,
                SummonSequence = snapshot.SummonSequence
            };
            accessor.Write(0, ref raw);
            return true;
        }
        catch (ArgumentException)
        {
            return false;
        }
        catch (IOException)
        {
            return false;
        }
    }
}
