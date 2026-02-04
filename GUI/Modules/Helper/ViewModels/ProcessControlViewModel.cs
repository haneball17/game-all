using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using GameHelperGUI.Models;
using GameHelperGUI.Services;

namespace GameHelperGUI.ViewModels;

public sealed class ProcessControlViewModel : INotifyPropertyChanged
{
    [Flags]
    private enum ControlActionMask : uint
    {
        FullscreenAttack = 1 << 0,
        FullscreenSkill = 1 << 1,
        AutoTransparent = 1 << 2,
        AttractEnabled = 1 << 3,
        AttractMode = 1 << 4,
        AttractPositive = 1 << 5,
        HotkeyEnabled = 1 << 6,
        GatherItems = 1 << 7,
        DamageEnabled = 1 << 8,
        DamageMultiplier = 1 << 9,
        InvincibleEnabled = 1 << 10
    }

    private readonly SharedMemoryControlWriter _writer = new();
    private readonly Dictionary<uint, HelperControlSnapshot> _states = new();
    private bool _suspendWrite;
    private bool _suspendStatusSync;
    private uint _pid;
    private ControlOverride _fullscreenAttackOverride = ControlOverride.Follow;
    private ControlOverride _fullscreenSkillOverride = ControlOverride.Follow;
    private ControlOverride _autoTransparentOverride = ControlOverride.Follow;
    private ControlOverride _attractOverride = ControlOverride.Follow;
    private ControlOverride _hotkeyEnabledOverride = ControlOverride.Follow;
    private bool _fullscreenAttackEnabled;
    private bool _fullscreenSkillActive;
    private bool _autoTransparentEnabled;
    private bool _attractEnabled;
    private bool _attractPositive = true;
    private int _attractModeIndex;
    private bool _gatherItemsEnabled;
    private bool _damageEnabled;
    private int _damageMultiplier = 10;
    private bool _invincibleEnabled;
    private bool _hotkeyEnabled;
    private uint _summonSequence;
    private string _statusMessage = string.Empty;

    public event PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<ControlOptionItem> OverrideOptions { get; } = new[]
    {
        new ControlOptionItem(ControlOverride.Follow, "跟随配置"),
        new ControlOptionItem(ControlOverride.ForceOn, "强制开启"),
        new ControlOptionItem(ControlOverride.ForceOff, "强制关闭")
    };

    public IReadOnlyList<string> AttractModeOptions { get; } = new[] { "0", "1", "2", "3" };

    public bool HasTarget => _pid != 0;

    public string StatusMessage
    {
        get => _statusMessage;
        private set => SetField(ref _statusMessage, value);
    }

    public ControlOverride FullscreenAttackOverride
    {
        get => _fullscreenAttackOverride;
        set => SetOverride(ref _fullscreenAttackOverride, value);
    }

    public ControlOverride FullscreenSkillOverride
    {
        get => _fullscreenSkillOverride;
        set => SetOverride(ref _fullscreenSkillOverride, value);
    }

    public ControlOverride AutoTransparentOverride
    {
        get => _autoTransparentOverride;
        set => SetOverride(ref _autoTransparentOverride, value);
    }

    public ControlOverride AttractOverride
    {
        get => _attractOverride;
        set => SetOverride(ref _attractOverride, value);
    }

    public ControlOverride HotkeyEnabledOverride
    {
        get => _hotkeyEnabledOverride;
        set => SetOverride(ref _hotkeyEnabledOverride, value);
    }

    public bool FullscreenAttackEnabled
    {
        get => _fullscreenAttackEnabled;
        set => SetToggle(ref _fullscreenAttackEnabled, value, ControlActionMask.FullscreenAttack, snapshot =>
        {
            snapshot.DesiredFullscreenAttack = value;
        });
    }

    public bool FullscreenSkillActive
    {
        get => _fullscreenSkillActive;
        set => SetToggle(ref _fullscreenSkillActive, value, ControlActionMask.FullscreenSkill, snapshot =>
        {
            snapshot.DesiredFullscreenSkill = value;
        });
    }

    public bool AutoTransparentEnabled
    {
        get => _autoTransparentEnabled;
        set => SetToggle(ref _autoTransparentEnabled, value, ControlActionMask.AutoTransparent, snapshot =>
        {
            snapshot.DesiredAutoTransparent = value;
        });
    }

    public bool HotkeyEnabled
    {
        get => _hotkeyEnabled;
        set => SetToggle(ref _hotkeyEnabled, value, ControlActionMask.HotkeyEnabled, snapshot =>
        {
            snapshot.DesiredHotkeyEnabled = value;
        });
    }

    public bool AttractEnabled
    {
        get => _attractEnabled;
        set => SetToggle(ref _attractEnabled, value, ControlActionMask.AttractEnabled, snapshot =>
        {
            snapshot.DesiredAttractEnabled = value;
            if (value)
            {
                snapshot.DesiredAttractMode = ToAttractModeValue(_attractModeIndex);
                snapshot.ActionMask |= (uint)ControlActionMask.AttractMode;
            }
        });
    }

    public bool AttractPositive
    {
        get => _attractPositive;
        set => SetToggle(ref _attractPositive, value, ControlActionMask.AttractPositive, snapshot =>
        {
            snapshot.DesiredAttractPositive = value;
        });
    }

    public bool GatherItemsEnabled
    {
        get => _gatherItemsEnabled;
        set => SetToggle(ref _gatherItemsEnabled, value, ControlActionMask.GatherItems, snapshot =>
        {
            snapshot.DesiredGatherItemsEnabled = value;
        });
    }

    public bool DamageEnabled
    {
        get => _damageEnabled;
        set => SetToggle(ref _damageEnabled, value, ControlActionMask.DamageEnabled, snapshot =>
        {
            snapshot.DesiredDamageEnabled = value;
            if (value)
            {
                snapshot.DesiredDamageMultiplier = _damageMultiplier;
                snapshot.ActionMask |= (uint)ControlActionMask.DamageMultiplier;
            }
        });
    }

    public bool InvincibleEnabled
    {
        get => _invincibleEnabled;
        set => SetToggle(ref _invincibleEnabled, value, ControlActionMask.InvincibleEnabled, snapshot =>
        {
            snapshot.DesiredInvincibleEnabled = value;
        });
    }

    public int DamageMultiplier
    {
        get => _damageMultiplier;
        set
        {
            int normalized = ClampDamageMultiplier(value);
            if (SetField(ref _damageMultiplier, normalized))
            {
                OnPropertyChanged(nameof(DamageMultiplierText));
                if (_suspendStatusSync)
                {
                    return;
                }
                SendAction(ControlActionMask.DamageMultiplier, snapshot =>
                {
                    snapshot.DesiredDamageMultiplier = normalized;
                });
            }
        }
    }

    public string DamageMultiplierText
    {
        get => _damageMultiplier.ToString();
        set
        {
            if (!int.TryParse(value, out int parsed))
            {
                return;
            }
            DamageMultiplier = parsed;
        }
    }

    public int AttractModeIndex
    {
        get => _attractModeIndex;
        set
        {
            int normalized = Math.Clamp(value, 0, 3);
            if (SetField(ref _attractModeIndex, normalized))
            {
                OnPropertyChanged(nameof(AttractModeText));
                if (_suspendStatusSync)
                {
                    return;
                }
                SendAction(ControlActionMask.AttractMode | ControlActionMask.AttractEnabled, snapshot =>
                {
                    snapshot.DesiredAttractMode = ToAttractModeValue(normalized);
                    snapshot.DesiredAttractEnabled = true;
                });
            }
        }
    }

    public string FullscreenAttackStateText => _fullscreenAttackEnabled ? "已开启" : "已关闭";
    public string FullscreenSkillStateText => _fullscreenSkillActive ? "已开启" : "已关闭";
    public string AutoTransparentStateText => _autoTransparentEnabled ? "已开启" : "已关闭";
    public string HotkeyEnabledStateText => _hotkeyEnabled ? "已开启" : "已关闭";
    public string AttractEnabledStateText => _attractEnabled ? "已开启" : "已关闭";
    public string AttractDirectionText => _attractPositive ? "正向" : "负向";
    public string AttractModeText => AttractModeOptions[Math.Clamp(_attractModeIndex, 0, 3)];
    public string GatherItemsStateText => _gatherItemsEnabled ? "已开启" : "已关闭";
    public string DamageEnabledStateText => _damageEnabled ? "已开启" : "已关闭";
    public string InvincibleEnabledStateText => _invincibleEnabled ? "已开启" : "已关闭";

    public bool CanControlFullscreenAttack => HasTarget && _fullscreenAttackOverride == ControlOverride.Follow;
    public bool CanControlFullscreenSkill => HasTarget && _fullscreenSkillOverride == ControlOverride.Follow;
    public bool CanControlAutoTransparent => HasTarget && _autoTransparentOverride == ControlOverride.Follow;
    public bool CanControlAttract => HasTarget && _attractOverride == ControlOverride.Follow;
    public bool CanControlGatherItems => HasTarget && _attractOverride == ControlOverride.Follow;
    public bool CanControlDamage => HasTarget;
    public bool CanControlHotkeyEnabled => HasTarget && _hotkeyEnabledOverride == ControlOverride.Follow;

    public ICommand SummonCommand { get; }

    public ProcessControlViewModel()
    {
        SummonCommand = new RelayCommand(_ => TriggerSummon(), _ => HasTarget);
    }

    public void UpdateTarget(ProcessStatusViewModel? process)
    {
        uint pid = process?.Pid ?? 0;
        _pid = pid;
        _suspendWrite = true;
        if (pid == 0)
        {
            FullscreenAttackOverride = ControlOverride.Follow;
            FullscreenSkillOverride = ControlOverride.Follow;
            AutoTransparentOverride = ControlOverride.Follow;
            AttractOverride = ControlOverride.Follow;
            HotkeyEnabledOverride = ControlOverride.Follow;
            _summonSequence = 0;
            StatusMessage = "未选择实例";
            UpdateStatus(null);
        }
        else
        {
            var state = GetOrCreateState(pid);
            FullscreenAttackOverride = state.FullscreenAttack;
            FullscreenSkillOverride = state.FullscreenSkill;
            AutoTransparentOverride = state.AutoTransparent;
            AttractOverride = state.Attract;
            HotkeyEnabledOverride = state.HotkeyEnabled;
            _summonSequence = state.SummonSequence;
            StatusMessage = string.Empty;
            UpdateStatus(process);
        }
        _suspendWrite = false;
        OnPropertyChanged(nameof(HasTarget));
        NotifyControlAvailability();
        CommandManager.InvalidateRequerySuggested();
    }

    public void UpdateStatus(ProcessStatusViewModel? process)
    {
        _suspendStatusSync = true;
        if (process == null)
        {
            SetField(ref _fullscreenAttackEnabled, false, nameof(FullscreenAttackEnabled));
            SetField(ref _fullscreenSkillActive, false, nameof(FullscreenSkillActive));
            SetField(ref _autoTransparentEnabled, false, nameof(AutoTransparentEnabled));
            SetField(ref _hotkeyEnabled, false, nameof(HotkeyEnabled));
            SetField(ref _attractEnabled, false, nameof(AttractEnabled));
            SetField(ref _attractPositive, true, nameof(AttractPositive));
            SetField(ref _attractModeIndex, 0, nameof(AttractModeIndex));
            SetField(ref _gatherItemsEnabled, false, nameof(GatherItemsEnabled));
            SetField(ref _damageEnabled, false, nameof(DamageEnabled));
            SetField(ref _invincibleEnabled, false, nameof(InvincibleEnabled));
            SetField(ref _damageMultiplier, 10, nameof(DamageMultiplier));
            OnPropertyChanged(nameof(DamageMultiplierText));
        }
        else
        {
            SetField(ref _fullscreenAttackEnabled, process.FullscreenAttackTarget, nameof(FullscreenAttackEnabled));
            SetField(ref _fullscreenSkillActive, process.FullscreenSkillActive, nameof(FullscreenSkillActive));
            SetField(ref _autoTransparentEnabled, process.AutoTransparentEnabled, nameof(AutoTransparentEnabled));
            SetField(ref _hotkeyEnabled, process.HotkeyEnabled, nameof(HotkeyEnabled));
            SetField(ref _attractEnabled, process.AttractMode != 0, nameof(AttractEnabled));
            SetField(ref _attractPositive, process.AttractPositive, nameof(AttractPositive));
            int modeIndex = process.AttractMode > 0 ? Math.Clamp(process.AttractMode - 1, 0, 3) : _attractModeIndex;
            SetField(ref _attractModeIndex, modeIndex, nameof(AttractModeIndex));
            SetField(ref _gatherItemsEnabled, process.GatherItemsEnabled, nameof(GatherItemsEnabled));
            SetField(ref _damageEnabled, process.DamageEnabled, nameof(DamageEnabled));
            SetField(ref _invincibleEnabled, process.InvincibleEnabled, nameof(InvincibleEnabled));
            int multiplier = ClampDamageMultiplier(process.DamageMultiplier);
            SetField(ref _damageMultiplier, multiplier, nameof(DamageMultiplier));
            OnPropertyChanged(nameof(DamageMultiplierText));
        }
        _suspendStatusSync = false;
        NotifyStatusText();
    }

    private void TriggerSummon()
    {
        if (_pid == 0)
        {
            return;
        }
        _summonSequence = unchecked(_summonSequence + 1);
        PersistState();
    }

    private void SetOverride(ref ControlOverride field, ControlOverride value)
    {
        if (SetField(ref field, value))
        {
            PersistState();
            NotifyControlAvailability();
        }
    }

    private HelperControlSnapshot GetOrCreateState(uint pid)
    {
        if (_states.TryGetValue(pid, out var state))
        {
            return state;
        }
        state = new HelperControlSnapshot();
        _states[pid] = state;
        return state;
    }

    private void PersistState()
    {
        if (_suspendWrite || _pid == 0)
        {
            return;
        }
        var state = GetOrCreateState(_pid);
        var snapshot = new HelperControlSnapshot
        {
            FullscreenAttack = _fullscreenAttackOverride,
            FullscreenSkill = _fullscreenSkillOverride,
            AutoTransparent = _autoTransparentOverride,
            Attract = _attractOverride,
            HotkeyEnabled = _hotkeyEnabledOverride,
            SummonSequence = _summonSequence,
            ActionSequence = state.ActionSequence
        };
        _states[_pid] = snapshot;
        var result = _writer.TryWrite(_pid, snapshot);
        switch (result)
        {
            case SharedMemoryWriteStatus.Ok:
                StatusMessage = "已发送";
                GuiLogger.Info("control", "write_ok", new Dictionary<string, object?>
                {
                    ["pid"] = _pid
                });
                break;
            case SharedMemoryWriteStatus.NotFound:
                StatusMessage = "未连接";
                GuiLogger.Info("control", "write_not_found", new Dictionary<string, object?>
                {
                    ["pid"] = _pid
                });
                break;
            default:
                StatusMessage = "写入失败";
                GuiLogger.Info("control", "write_failed", new Dictionary<string, object?>
                {
                    ["pid"] = _pid
                });
                break;
        }
    }

    private void SetToggle(ref bool field, bool value, ControlActionMask mask, Action<HelperControlSnapshot> apply)
    {
        if (!SetField(ref field, value))
        {
            return;
        }
        NotifyStatusText();
        if (_suspendStatusSync)
        {
            return;
        }
        SendAction(mask, apply);
    }

    private void SendAction(ControlActionMask mask, Action<HelperControlSnapshot> apply)
    {
        if (_suspendWrite || _pid == 0)
        {
            return;
        }
        var state = GetOrCreateState(_pid);
        uint nextSequence = unchecked(state.ActionSequence + 1);
        var snapshot = new HelperControlSnapshot
        {
            FullscreenAttack = _fullscreenAttackOverride,
            FullscreenSkill = _fullscreenSkillOverride,
            AutoTransparent = _autoTransparentOverride,
            Attract = _attractOverride,
            HotkeyEnabled = _hotkeyEnabledOverride,
            SummonSequence = _summonSequence,
            ActionSequence = nextSequence,
            ActionMask = (uint)mask
        };
        apply(snapshot);
        _states[_pid] = snapshot;
        var result = _writer.TryWrite(_pid, snapshot);
        switch (result)
        {
            case SharedMemoryWriteStatus.Ok:
                StatusMessage = "已发送";
                break;
            case SharedMemoryWriteStatus.NotFound:
                StatusMessage = "未连接";
                break;
            default:
                StatusMessage = "写入失败";
                break;
        }
    }

    private static byte ToAttractModeValue(int index)
    {
        int normalized = Math.Clamp(index, 0, 3);
        return (byte)(normalized + 1);
    }

    private void NotifyStatusText()
    {
        OnPropertyChanged(nameof(FullscreenAttackStateText));
        OnPropertyChanged(nameof(FullscreenSkillStateText));
        OnPropertyChanged(nameof(AutoTransparentStateText));
        OnPropertyChanged(nameof(HotkeyEnabledStateText));
        OnPropertyChanged(nameof(AttractEnabledStateText));
        OnPropertyChanged(nameof(AttractDirectionText));
        OnPropertyChanged(nameof(AttractModeText));
        OnPropertyChanged(nameof(GatherItemsStateText));
        OnPropertyChanged(nameof(DamageEnabledStateText));
        OnPropertyChanged(nameof(InvincibleEnabledStateText));
    }

    private void NotifyControlAvailability()
    {
        OnPropertyChanged(nameof(CanControlFullscreenAttack));
        OnPropertyChanged(nameof(CanControlFullscreenSkill));
        OnPropertyChanged(nameof(CanControlAutoTransparent));
        OnPropertyChanged(nameof(CanControlAttract));
        OnPropertyChanged(nameof(CanControlGatherItems));
        OnPropertyChanged(nameof(CanControlDamage));
        OnPropertyChanged(nameof(CanControlHotkeyEnabled));
    }

    private static int ClampDamageMultiplier(int value)
    {
        if (value < 1)
        {
            return 1;
        }
        if (value > 1000)
        {
            return 1000;
        }
        return value;
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

    private void OnPropertyChanged([CallerMemberName] string? name = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }

    public sealed class ControlOptionItem
    {
        public ControlOverride Value { get; }
        public string Label { get; }

        public ControlOptionItem(ControlOverride value, string label)
        {
            Value = value;
            Label = label;
        }
    }
}
