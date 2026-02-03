namespace DNFSyncBox;

/// <summary>
/// 共享内存协议常量。
/// </summary>
internal static unsafe class SharedMemoryConstants
{
    public const string MappingName = "Local\\DNFSyncBox.KeyboardState.V3";
    public const uint Version = 3;
    public const uint Magic = 0x33564E44; // "DNV3"
    public const int KeyCount = 256;

    public const uint FlagPaused = 0x1;
    public const uint FlagClear = 0x2;

    public const int HeartbeatIntervalMs = 50;

    public const int EventCapacity = 4096;
    // InputEvent 结构固定 16 字节（Pack=1），用于固定缓冲区长度。
    public const int InputEventSize = 16;
    public const int EventBufferSize = EventCapacity * InputEventSize;

    public static int SharedMemorySize => sizeof(SharedKeyboardStateV3);
}
