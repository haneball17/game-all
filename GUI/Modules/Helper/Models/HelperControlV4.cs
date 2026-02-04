using System.Runtime.InteropServices;

namespace GameHelperGUI.Models;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct HelperControlV4
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
    public byte DesiredGatherItemsEnabled;
    public uint DesiredDamageMultiplier;
    public byte DesiredDamageEnabled;
    public byte DesiredInvincibleEnabled;
    public byte Reserved3;
    public byte Reserved4;
    public byte Reserved5;
    public byte Reserved6;
    public byte Reserved7;
    public byte Reserved8;
}
