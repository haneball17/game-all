using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text;
using GameHelperGUI.Models;

namespace GameHelperGUI.ViewModels;

public sealed class ProcessStatusViewModel : INotifyPropertyChanged
{
    private string _playerName = string.Empty;
    private string _statusText = "未知";
    private string _lastUpdateText = "-";
    private string _injectText = "-";
    private string _syncText = "-";
    private bool _isOnline;
    private bool _isCompatible = true;
    private bool _autoTransparentEnabled;
    private bool _fullscreenAttackTarget;
    private bool _fullscreenAttackPatchOn;
    private int _attractMode;
    private bool _attractPositive;
    private bool _gatherItemsEnabled;
    private bool _damageEnabled;
    private int _damageMultiplier = 1;
    private bool _invincibleEnabled;
    private bool _summonEnabled;
    private bool _fullscreenSkillEnabled;
    private bool _fullscreenSkillActive;
    private uint _fullscreenSkillHotkey;
    private bool _hotkeyEnabled;

    public event PropertyChangedEventHandler? PropertyChanged;

    public uint Pid { get; }

    public string PlayerName
    {
        get => _playerName;
        private set => SetField(ref _playerName, value);
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetField(ref _statusText, value);
    }

    public string LastUpdateText
    {
        get => _lastUpdateText;
        private set => SetField(ref _lastUpdateText, value);
    }

    public string InjectText
    {
        get => _injectText;
        private set => SetField(ref _injectText, value);
    }

    public string SyncText
    {
        get => _syncText;
        private set => SetField(ref _syncText, value);
    }

    public bool IsOnline
    {
        get => _isOnline;
        private set => SetField(ref _isOnline, value);
    }

    public bool IsCompatible
    {
        get => _isCompatible;
        private set => SetField(ref _isCompatible, value);
    }

    public bool AutoTransparentEnabled
    {
        get => _autoTransparentEnabled;
        private set => SetField(ref _autoTransparentEnabled, value);
    }

    public bool FullscreenAttackTarget
    {
        get => _fullscreenAttackTarget;
        private set => SetField(ref _fullscreenAttackTarget, value);
    }

    public bool FullscreenAttackPatchOn
    {
        get => _fullscreenAttackPatchOn;
        private set => SetField(ref _fullscreenAttackPatchOn, value);
    }

    public int AttractMode
    {
        get => _attractMode;
        private set => SetField(ref _attractMode, value);
    }

    public bool AttractPositive
    {
        get => _attractPositive;
        private set => SetField(ref _attractPositive, value);
    }

    public bool GatherItemsEnabled
    {
        get => _gatherItemsEnabled;
        private set => SetField(ref _gatherItemsEnabled, value);
    }

    public bool DamageEnabled
    {
        get => _damageEnabled;
        private set => SetField(ref _damageEnabled, value);
    }

    public int DamageMultiplier
    {
        get => _damageMultiplier;
        private set => SetField(ref _damageMultiplier, value);
    }

    public bool InvincibleEnabled
    {
        get => _invincibleEnabled;
        private set => SetField(ref _invincibleEnabled, value);
    }

    public bool SummonEnabled
    {
        get => _summonEnabled;
        private set => SetField(ref _summonEnabled, value);
    }

    public bool FullscreenSkillEnabled
    {
        get => _fullscreenSkillEnabled;
        private set => SetField(ref _fullscreenSkillEnabled, value);
    }

    public bool FullscreenSkillActive
    {
        get => _fullscreenSkillActive;
        private set => SetField(ref _fullscreenSkillActive, value);
    }

    public uint FullscreenSkillHotkey
    {
        get => _fullscreenSkillHotkey;
        private set => SetField(ref _fullscreenSkillHotkey, value);
    }

    public bool HotkeyEnabled
    {
        get => _hotkeyEnabled;
        private set => SetField(ref _hotkeyEnabled, value);
    }

    public string DisplayName => string.IsNullOrWhiteSpace(PlayerName) ? "未知角色" : PlayerName;
    public string PidText => Pid.ToString();
    public string FullscreenAttackSummary => $"{(FullscreenAttackTarget ? "开" : "关")}/{(FullscreenAttackPatchOn ? "开" : "关")}";

    public string DetailSummary
    {
        get
        {
            var builder = new StringBuilder();
            builder.AppendLine($"角色：{DisplayName}");
            builder.AppendLine($"PID：{Pid}");
            builder.AppendLine($"状态：{StatusText}");
            builder.AppendLine($"最近更新：{LastUpdateText}");
            builder.AppendLine($"注入：{InjectText}");
            builder.AppendLine($"同步：{SyncText}");
            builder.AppendLine($"全屏攻击：目标={(FullscreenAttackTarget ? "开" : "关")} / 当前={(FullscreenAttackPatchOn ? "开" : "关")}");
            builder.AppendLine($"自动透明：{(AutoTransparentEnabled ? "开" : "关")}");
            builder.AppendLine($"吸怪模式：{AttractMode}，方向：{(AttractPositive ? "正向" : "负向")}");
            builder.AppendLine($"聚物：{(GatherItemsEnabled ? "开" : "关")}");
            builder.AppendLine($"倍攻：{(DamageEnabled ? "开" : "关")} 倍率：{DamageMultiplier}");
            builder.AppendLine($"怪物零伤：{(InvincibleEnabled ? "开" : "关")}");
            builder.AppendLine($"召唤人偶：{(SummonEnabled ? "启用" : "停用")}");
            builder.AppendLine($"全屏技能：{(FullscreenSkillEnabled ? "启用" : "停用")} / {(FullscreenSkillActive ? "激活" : "关闭")}");
            builder.AppendLine($"技能热键：{HotkeyTextFormatter.Format((int)FullscreenSkillHotkey)}");
            builder.AppendLine($"热键响应：{(HotkeyEnabled ? "启用" : "停用")}");
            return builder.ToString().TrimEnd();
        }
    }

    public ProcessStatusViewModel(uint pid)
    {
        Pid = pid;
    }

    public static ProcessStatusViewModel FromSnapshot(HelperStatusSnapshot snapshot, string statusText, string lastUpdateText, bool isOnline, string injectText, string syncText)
    {
        var vm = new ProcessStatusViewModel(snapshot.Pid);
        vm.UpdateFromSnapshot(snapshot, statusText, lastUpdateText, isOnline, injectText, syncText);
        return vm;
    }

    public static ProcessStatusViewModel CreateFallback(uint pid, string statusText, string lastUpdateText, bool isCompatible, string injectText, string syncText)
    {
        var vm = new ProcessStatusViewModel(pid);
        vm.UpdateFallback(statusText, lastUpdateText, isCompatible, injectText, syncText);
        return vm;
    }

    public void UpdateFromSnapshot(HelperStatusSnapshot snapshot, string statusText, string lastUpdateText, bool isOnline, string injectText, string syncText)
    {
        bool changed = false;
        changed |= SetField(ref _playerName, snapshot.PlayerName, nameof(PlayerName));
        changed |= SetField(ref _statusText, statusText, nameof(StatusText));
        changed |= SetField(ref _lastUpdateText, lastUpdateText, nameof(LastUpdateText));
        changed |= SetField(ref _injectText, injectText, nameof(InjectText));
        changed |= SetField(ref _syncText, syncText, nameof(SyncText));
        changed |= SetField(ref _isOnline, isOnline, nameof(IsOnline));
        changed |= SetField(ref _isCompatible, true, nameof(IsCompatible));
        changed |= SetField(ref _autoTransparentEnabled, snapshot.AutoTransparentEnabled, nameof(AutoTransparentEnabled));
        changed |= SetField(ref _fullscreenAttackTarget, snapshot.FullscreenAttackTarget, nameof(FullscreenAttackTarget));
        changed |= SetField(ref _fullscreenAttackPatchOn, snapshot.FullscreenAttackPatchOn, nameof(FullscreenAttackPatchOn));
        changed |= SetField(ref _attractMode, snapshot.AttractMode, nameof(AttractMode));
        changed |= SetField(ref _attractPositive, snapshot.AttractPositive, nameof(AttractPositive));
        changed |= SetField(ref _gatherItemsEnabled, snapshot.GatherItemsEnabled, nameof(GatherItemsEnabled));
        changed |= SetField(ref _damageEnabled, snapshot.DamageEnabled, nameof(DamageEnabled));
        changed |= SetField(ref _damageMultiplier, snapshot.DamageMultiplier, nameof(DamageMultiplier));
        changed |= SetField(ref _invincibleEnabled, snapshot.InvincibleEnabled, nameof(InvincibleEnabled));
        changed |= SetField(ref _summonEnabled, snapshot.SummonEnabled, nameof(SummonEnabled));
        changed |= SetField(ref _fullscreenSkillEnabled, snapshot.FullscreenSkillEnabled, nameof(FullscreenSkillEnabled));
        changed |= SetField(ref _fullscreenSkillActive, snapshot.FullscreenSkillActive, nameof(FullscreenSkillActive));
        changed |= SetField(ref _fullscreenSkillHotkey, snapshot.FullscreenSkillHotkey, nameof(FullscreenSkillHotkey));
        changed |= SetField(ref _hotkeyEnabled, snapshot.HotkeyEnabled, nameof(HotkeyEnabled));
        if (changed)
        {
            NotifyDerived();
        }
    }

    public void UpdateFallback(string statusText, string lastUpdateText, bool isCompatible, string injectText, string syncText)
    {
        bool changed = false;
        changed |= SetField(ref _playerName, string.Empty, nameof(PlayerName));
        changed |= SetField(ref _statusText, statusText, nameof(StatusText));
        changed |= SetField(ref _lastUpdateText, lastUpdateText, nameof(LastUpdateText));
        changed |= SetField(ref _injectText, injectText, nameof(InjectText));
        changed |= SetField(ref _syncText, syncText, nameof(SyncText));
        changed |= SetField(ref _isOnline, false, nameof(IsOnline));
        changed |= SetField(ref _isCompatible, isCompatible, nameof(IsCompatible));
        changed |= SetField(ref _autoTransparentEnabled, false, nameof(AutoTransparentEnabled));
        changed |= SetField(ref _fullscreenAttackTarget, false, nameof(FullscreenAttackTarget));
        changed |= SetField(ref _fullscreenAttackPatchOn, false, nameof(FullscreenAttackPatchOn));
        changed |= SetField(ref _attractMode, 0, nameof(AttractMode));
        changed |= SetField(ref _attractPositive, false, nameof(AttractPositive));
        changed |= SetField(ref _gatherItemsEnabled, false, nameof(GatherItemsEnabled));
        changed |= SetField(ref _damageEnabled, false, nameof(DamageEnabled));
        changed |= SetField(ref _damageMultiplier, 1, nameof(DamageMultiplier));
        changed |= SetField(ref _invincibleEnabled, false, nameof(InvincibleEnabled));
        changed |= SetField(ref _summonEnabled, false, nameof(SummonEnabled));
        changed |= SetField(ref _fullscreenSkillEnabled, false, nameof(FullscreenSkillEnabled));
        changed |= SetField(ref _fullscreenSkillActive, false, nameof(FullscreenSkillActive));
        changed |= SetField(ref _fullscreenSkillHotkey, 0u, nameof(FullscreenSkillHotkey));
        changed |= SetField(ref _hotkeyEnabled, false, nameof(HotkeyEnabled));
        if (changed)
        {
            NotifyDerived();
        }
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }
        field = value;
        OnPropertyChanged(name);
        return true;
    }

    private void NotifyDerived()
    {
        OnPropertyChanged(nameof(DisplayName));
        OnPropertyChanged(nameof(FullscreenAttackSummary));
        OnPropertyChanged(nameof(DetailSummary));
    }

    private void OnPropertyChanged(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
