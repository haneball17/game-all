namespace GameHelperGUI.Models;

public sealed class HelperControlSnapshot
{
    public ControlOverride FullscreenAttack { get; set; } = ControlOverride.Follow;
    public ControlOverride FullscreenSkill { get; set; } = ControlOverride.Follow;
    public ControlOverride AutoTransparent { get; set; } = ControlOverride.Follow;
    public ControlOverride Attract { get; set; } = ControlOverride.Follow;
    public ControlOverride HotkeyEnabled { get; set; } = ControlOverride.Follow;
    public uint SummonSequence { get; set; }
    public uint ActionSequence { get; set; }
    public uint ActionMask { get; set; }
    public bool DesiredFullscreenAttack { get; set; }
    public bool DesiredFullscreenSkill { get; set; }
    public bool DesiredAutoTransparent { get; set; }
    public bool DesiredAttractEnabled { get; set; }
    public byte DesiredAttractMode { get; set; }
    public bool DesiredAttractPositive { get; set; }
    public bool DesiredHotkeyEnabled { get; set; }
    public bool DesiredGatherItemsEnabled { get; set; }
    public bool DesiredDamageEnabled { get; set; }
    public int DesiredDamageMultiplier { get; set; }
    public bool DesiredInvincibleEnabled { get; set; }
}
