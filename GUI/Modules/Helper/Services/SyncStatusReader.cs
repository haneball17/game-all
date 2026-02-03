using System;
using System.IO;
using System.IO.MemoryMappedFiles;

namespace GameHelperGUI.Services;

public enum SyncReadStatus
{
    Ok,
    NotFound,
    VersionMismatch,
    SizeMismatch,
    ReadFailed
}

public readonly struct SyncStatusSnapshot
{
    public SyncStatusSnapshot(uint version, uint flags, uint activePid, ulong lastTick)
    {
        Version = version;
        Flags = flags;
        ActivePid = activePid;
        LastTick = lastTick;
    }

    public uint Version { get; }
    public uint Flags { get; }
    public uint ActivePid { get; }
    public ulong LastTick { get; }
}

/// <summary>
/// Sync 共享内存读取器（仅用于展示状态）。
/// </summary>
public sealed class SyncStatusReader
{
    private const string MappingName = "Local\\DNFSyncBox.KeyboardState.V3";
    private const uint ExpectedVersion = 3;
    private const uint ExpectedMagic = 0x33564E44; // "DNV3"
    // 只读取 V3 头部 + Snapshot 头部（不需要映射完整事件缓冲区）
    private const int MappingSize = 48;

    public SyncReadStatus TryRead(out SyncStatusSnapshot snapshot)
    {
        snapshot = default;
        try
        {
            using var mapping = MemoryMappedFile.OpenExisting(MappingName, MemoryMappedFileRights.Read);
            using var accessor = mapping.CreateViewAccessor(0, MappingSize, MemoryMappedFileAccess.Read);
            if (accessor.Capacity < MappingSize)
            {
                return SyncReadStatus.SizeMismatch;
            }

            accessor.Read(0, out uint version);
            if (version != ExpectedVersion)
            {
                return SyncReadStatus.VersionMismatch;
            }

            accessor.Read(4, out uint magic);
            if (magic != ExpectedMagic)
            {
                return SyncReadStatus.VersionMismatch;
            }

            accessor.Read(24, out uint flags);
            accessor.Read(28, out uint activePid);
            accessor.Read(40, out ulong lastTick);
            snapshot = new SyncStatusSnapshot(version, flags, activePid, lastTick);
            return SyncReadStatus.Ok;
        }
        catch (FileNotFoundException)
        {
            return SyncReadStatus.NotFound;
        }
        catch (UnauthorizedAccessException)
        {
            return SyncReadStatus.ReadFailed;
        }
        catch (IOException)
        {
            return SyncReadStatus.ReadFailed;
        }
    }
}
