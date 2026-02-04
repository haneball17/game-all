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

    private const uint ExpectedVersionV4 = 4;

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
            return TryWriteV4(mapping, pid, snapshot) ? SharedMemoryWriteStatus.Ok : SharedMemoryWriteStatus.WriteFailed;
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

    private static bool TryWriteV4(MemoryMappedFile mapping, uint pid, HelperControlSnapshot snapshot)
    {
        try
        {
            uint size = (uint)Marshal.SizeOf<HelperControlV4>();
            using var accessor = mapping.CreateViewAccessor(0, size, MemoryMappedFileAccess.Write);
            int multiplier = snapshot.DesiredDamageMultiplier;
            if (multiplier < 1)
            {
                multiplier = 1;
            }
            else if (multiplier > 1000)
            {
                multiplier = 1000;
            }
            var raw = new HelperControlV4
            {
                Version = ExpectedVersionV4,
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
                DesiredHotkeyEnabled = snapshot.DesiredHotkeyEnabled ? (byte)1 : (byte)0,
                DesiredGatherItemsEnabled = snapshot.DesiredGatherItemsEnabled ? (byte)1 : (byte)0,
                DesiredDamageMultiplier = (uint)multiplier,
                DesiredDamageEnabled = snapshot.DesiredDamageEnabled ? (byte)1 : (byte)0,
                DesiredInvincibleEnabled = snapshot.DesiredInvincibleEnabled ? (byte)1 : (byte)0
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
