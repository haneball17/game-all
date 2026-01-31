namespace DNFSyncBox;

public sealed class KeyStateTracker
{
    private readonly bool[] _down = new bool[SharedMemoryConstants.KeyCount];
    private readonly uint[] _edgeCounter = new uint[SharedMemoryConstants.KeyCount];
    private readonly bool[] _repeatDown = new bool[SharedMemoryConstants.KeyCount];
    private readonly long[] _repeatNextToggle = new long[SharedMemoryConstants.KeyCount];
    private readonly uint[] _repeatEdgeCounter = new uint[SharedMemoryConstants.KeyCount];
    private readonly bool[] _effectiveDown = new bool[SharedMemoryConstants.KeyCount];
    private readonly uint[] _effectiveEdge = new uint[SharedMemoryConstants.KeyCount];

    /// <summary>
    /// 更新按键状态，返回是否发生变化（用于去重与边沿计数）。
    /// </summary>
    public bool SetState(int vKey, bool isDown)
    {
        if (vKey < 0 || vKey >= SharedMemoryConstants.KeyCount)
        {
            return false;
        }

        if (_down[vKey] == isDown)
        {
            return false;
        }

        _down[vKey] = isDown;
        if (isDown)
        {
            _edgeCounter[vKey] = unchecked(_edgeCounter[vKey] + 1);
        }
        else
        {
            _repeatDown[vKey] = false;
            _repeatNextToggle[vKey] = 0;
        }

        return true;
    }

    /// <summary>
    /// 按方案生成键盘快照。
    /// </summary>
    internal void ApplyProfile(KeyboardProfile profile, byte[] toggleState, byte[] keyboardState, uint[] edgeOut, byte[] maskOut, long nowMs)
    {
        BuildEffectiveState(profile, nowMs);
        profile.Apply(_effectiveDown, _effectiveEdge, toggleState, keyboardState, edgeOut, maskOut);
    }

    /// <summary>
    /// 复制边沿计数（用于暂停/清键时保持一致性）。
    /// </summary>
    public void CopyEdgeCounters(uint[] edgeOut)
    {
        for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
        {
            edgeOut[i] = _edgeCounter[i] + _repeatEdgeCounter[i];
        }
    }

    /// <summary>
    /// 清空所有按下状态（用于暂停时的清键逻辑）。
    /// </summary>
    public void Clear()
    {
        Array.Clear(_down, 0, _down.Length);
        Array.Clear(_repeatDown, 0, _repeatDown.Length);
        Array.Clear(_repeatNextToggle, 0, _repeatNextToggle.Length);
    }

    private void BuildEffectiveState(KeyboardProfile profile, long nowMs)
    {
        Array.Copy(_down, _effectiveDown, _down.Length);
        for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
        {
            _effectiveEdge[i] = _edgeCounter[i];
        }

        var repeatIntervalMs = profile.RepeatIntervalMs;
        var repeatMask = profile.RepeatMask;
        if (repeatIntervalMs <= 0 || repeatMask.Length == 0)
        {
            Array.Clear(_repeatDown, 0, _repeatDown.Length);
            Array.Clear(_repeatNextToggle, 0, _repeatNextToggle.Length);
            return;
        }

        var halfInterval = Math.Max(20, repeatIntervalMs / 2);
        for (var i = 0; i < SharedMemoryConstants.KeyCount; i++)
        {
            if (!repeatMask[i])
            {
                _repeatDown[i] = false;
                _repeatNextToggle[i] = 0;
                continue;
            }

            if (!_down[i])
            {
                _repeatDown[i] = false;
                _repeatNextToggle[i] = 0;
                continue;
            }

            if (_repeatNextToggle[i] == 0)
            {
                _repeatDown[i] = true;
                _repeatNextToggle[i] = nowMs + halfInterval;
            }
            else if (nowMs >= _repeatNextToggle[i])
            {
                _repeatDown[i] = !_repeatDown[i];
                _repeatNextToggle[i] = nowMs + halfInterval;
                if (_repeatDown[i])
                {
                    _repeatEdgeCounter[i] = unchecked(_repeatEdgeCounter[i] + 1);
                }
            }

            _effectiveDown[i] = _repeatDown[i];
            _effectiveEdge[i] = _edgeCounter[i] + _repeatEdgeCounter[i];
        }
    }
}
