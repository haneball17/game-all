using System.Runtime.InteropServices;

namespace GameHelperGUI.Models;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct HelperControlV2
{
    public uint Version;
    public uint Size;
    public uint Pid;
    public uint LastUpdateTick;
    public byte FullscreenAttack;
    public byte FullscreenSkill;
    public byte AutoTransparent;
    public byte Attract;
    public byte HotkeyEnabled;
    public byte Reserved0;
    public byte Reserved1;
    public byte Reserved2;
    public uint SummonSequence;
    public uint ActionSequence;
    public uint ActionMask;
    public byte DesiredFullscreenAttack;
    public byte DesiredFullscreenSkill;
    public byte DesiredAutoTransparent;
    public byte DesiredAttractEnabled;
    public byte DesiredAttractMode;
    public byte DesiredAttractPositive;
    public byte DesiredHotkeyEnabled;
    public byte Reserved3;
}
