using System;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace DNFSyncBox;

/// <summary>
/// 共享内存写入端：负责把键盘快照写入全局共享内存。
/// </summary>
internal sealed unsafe class SharedMemoryWriter : IDisposable
{
    private MemoryMappedFile? _mmf;
    private MemoryMappedViewAccessor? _accessor;
    private byte* _basePtr;
    private bool _initialized;

    public bool IsReady => _initialized;

    /// <summary>
    /// 初始化共享内存并写入版本号。
    /// </summary>
    public void Initialize()
    {
        if (_initialized)
        {
            return;
        }

        _mmf = MemoryMappedFile.CreateOrOpen(
            SharedMemoryConstants.MappingName,
            SharedMemoryConstants.SharedMemorySize,
            MemoryMappedFileAccess.ReadWrite);

        _accessor = _mmf.CreateViewAccessor(0, SharedMemoryConstants.SharedMemorySize, MemoryMappedFileAccess.ReadWrite);
        byte* rawPtr = null;
        _accessor.SafeMemoryMappedViewHandle.AcquirePointer(ref rawPtr);
        _basePtr = rawPtr + _accessor.PointerOffset;
        _initialized = true;

        new Span<byte>(_basePtr, SharedMemoryConstants.SharedMemorySize).Clear();
        var shared = (SharedKeyboardStateV3*)_basePtr;
        shared->Version = SharedMemoryConstants.Version;
        shared->Magic = SharedMemoryConstants.Magic;
        shared->WriteHead = 0;
        shared->Capacity = SharedMemoryConstants.EventCapacity;
        shared->Snapshot.Version = SharedMemoryConstants.Version;
    }

    /// <summary>
    /// 发布键盘状态快照（使用 seq 进行无锁一致性保护）。
    /// </summary>
    public void PublishSnapshot(
        uint flags,
        uint activePid,
        uint profileId,
        uint profileMode,
        ulong lastTick,
        byte[] keyboardState,
        uint[] edgeCounter,
        byte[] targetMask,
        byte[] blockMask)
    {
        if (!_initialized)
        {
            return;
        }

        var shared = (SharedKeyboardStateV3*)_basePtr;
        var snapshot = &shared->Snapshot;
        snapshot->Version = SharedMemoryConstants.Version;
        var seq = snapshot->Seq + 1;
        if ((seq & 1) == 0)
        {
            seq++;
        }

        snapshot->Seq = seq;
        snapshot->Flags = flags;
        snapshot->ActivePid = activePid;
        snapshot->ProfileId = profileId;
        snapshot->ProfileMode = profileMode;
        snapshot->LastTick = lastTick;

        fixed (byte* srcState = keyboardState)
        {
            Buffer.MemoryCopy(
                srcState,
                snapshot->KeyboardState,
                SharedMemoryConstants.KeyCount,
                SharedMemoryConstants.KeyCount);
        }

        fixed (uint* srcEdge = edgeCounter)
        {
            Buffer.MemoryCopy(
                srcEdge,
                snapshot->EdgeCounter,
                SharedMemoryConstants.KeyCount * sizeof(uint),
                SharedMemoryConstants.KeyCount * sizeof(uint));
        }

        fixed (byte* srcMask = targetMask)
        {
            Buffer.MemoryCopy(
                srcMask,
                snapshot->TargetMask,
                SharedMemoryConstants.KeyCount,
                SharedMemoryConstants.KeyCount);
        }

        fixed (byte* srcBlock = blockMask)
        {
            Buffer.MemoryCopy(
                srcBlock,
                snapshot->BlockMask,
                SharedMemoryConstants.KeyCount,
                SharedMemoryConstants.KeyCount);
        }

        Thread.MemoryBarrier();
        snapshot->Seq = seq + 1;
    }

    /// <summary>
    /// 推送输入事件到事件流（V3）。
    /// </summary>
    public void PushEvent(int vKey, bool isDown, long timestamp)
    {
        if (!_initialized)
        {
            return;
        }

        if (vKey < 0 || vKey >= SharedMemoryConstants.KeyCount)
        {
            return;
        }

        var shared = (SharedKeyboardStateV3*)_basePtr;
        var capacity = shared->Capacity;
        if (capacity == 0)
        {
            return;
        }

        var currentHead = shared->WriteHead;
        var offset = (int)((uint)currentHead % capacity);
        var bufferOffset = offset * SharedMemoryConstants.InputEventSize;

        byte* bufferPtr = shared->EventBuffer;
        var eventPtr = (InputEventV3*)(bufferPtr + bufferOffset);
        eventPtr->SequenceId = unchecked((uint)currentHead);
        eventPtr->VirtualKey = (byte)vKey;
        eventPtr->IsDown = (byte)(isDown ? 1 : 0);
        eventPtr->Flags = 0;
        eventPtr->Reserved = 0;
        eventPtr->Timestamp = timestamp;

        Thread.MemoryBarrier();
        Interlocked.Increment(ref shared->WriteHead);
    }

    /// <summary>
    /// 释放共享内存资源。
    /// </summary>
    public void Dispose()
    {
        if (_accessor != null)
        {
            _accessor.SafeMemoryMappedViewHandle.ReleasePointer();
            _accessor.Dispose();
            _accessor = null;
        }

        _mmf?.Dispose();
        _mmf = null;
        _initialized = false;
    }

}
