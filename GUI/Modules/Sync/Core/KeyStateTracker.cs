using System;

namespace DNFSyncBox;

public sealed class KeyStateTracker : IDisposable
{
    private readonly IntPtr _nativeState;
    private readonly byte[] _repeatMaskBytes = new byte[SharedMemoryConstants.KeyCount];
    private readonly byte[] _effectiveDownBytes = new byte[SharedMemoryConstants.KeyCount];
    private readonly uint[] _effectiveEdge = new uint[SharedMemoryConstants.KeyCount];
    private readonly bool[] _effectiveDown = new bool[SharedMemoryConstants.KeyCount];
    private bool _disposed;

    public KeyStateTracker()
    {
        _nativeState = NativeMethods.game_control_core_key_state_create();
        if (_nativeState == IntPtr.Zero)
        {
            throw new InvalidOperationException("无法创建 Rust key state core");
        }
    }

    public bool SetState(int vKey, bool isDown)
    {
        if (vKey < 0 || vKey >= SharedMemoryConstants.KeyCount)
        {
            return false;
        }

        return NativeMethods.game_control_core_key_state_set_state(
            _nativeState,
            (uint)vKey,
            isDown ? 1u : 0u) != 0;
    }

    internal void ApplyProfile(
        KeyboardProfile profile,
        byte[] toggleState,
        byte[] keyboardState,
        uint[] edgeOut,
        byte[] maskOut,
        byte[] blockMask,
        byte[] mappingSourceMask,
        long nowMs)
    {
        Array.Clear(_repeatMaskBytes, 0, _repeatMaskBytes.Length);
        var repeatMask = profile.RepeatMask;
        for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
        {
            _repeatMaskBytes[i] = repeatMask[i] ? (byte)1 : (byte)0;
        }

        NativeMethods.game_control_core_key_state_build_effective(
            _nativeState,
            _repeatMaskBytes,
            (nuint)_repeatMaskBytes.Length,
            (uint)Math.Max(profile.RepeatIntervalMs, 0),
            (ulong)Math.Max(nowMs, 0L),
            _effectiveDownBytes,
            _effectiveEdge,
            (nuint)SharedMemoryConstants.KeyCount);

        for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
        {
            _effectiveDown[i] = _effectiveDownBytes[i] != 0;
        }

        NativeMethods.game_control_core_apply_profile(
            (uint)profile.Mode,
            profile.KeysArray,
            (nuint)profile.KeysArray.Length,
            profile.MappingSources,
            profile.MappingTargets,
            (nuint)profile.MappingSources.Length,
            profile.MappingBehavior == KeyboardMappingBehavior.Replace ? 1u : 0u,
            _effectiveDownBytes,
            _effectiveEdge,
            toggleState,
            (nuint)SharedMemoryConstants.KeyCount,
            keyboardState,
            edgeOut,
            maskOut,
            blockMask,
            mappingSourceMask);
    }

    public void CopyEdgeCounters(uint[] edgeOut)
    {
        NativeMethods.game_control_core_key_state_copy_edge_counters(
            _nativeState,
            edgeOut,
            (nuint)edgeOut.Length);
    }

    public void Clear()
    {
        NativeMethods.game_control_core_key_state_clear(_nativeState);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        NativeMethods.game_control_core_key_state_destroy(_nativeState);
        _disposed = true;
        GC.SuppressFinalize(this);
    }

    ~KeyStateTracker()
    {
        if (!_disposed)
        {
            NativeMethods.game_control_core_key_state_destroy(_nativeState);
        }
    }
}
