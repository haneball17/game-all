use game_core_protocols::{
    HELPER_CONTROL_V4_SIZE, HELPER_CONTROL_V4_VERSION, HELPER_STATUS_V5_SIZE,
    HELPER_STATUS_V5_VERSION, HelperControlV4, HelperStatusV5,
};

pub mod ffi;

pub const ACTION_MASK_FULLSCREEN_ATTACK: u32 = 1 << 0;
pub const ACTION_MASK_FULLSCREEN_SKILL: u32 = 1 << 1;
pub const ACTION_MASK_AUTO_TRANSPARENT: u32 = 1 << 2;
pub const ACTION_MASK_ATTRACT_ENABLED: u32 = 1 << 3;
pub const ACTION_MASK_ATTRACT_MODE: u32 = 1 << 4;
pub const ACTION_MASK_ATTRACT_POSITIVE: u32 = 1 << 5;
pub const ACTION_MASK_HOTKEY_ENABLED: u32 = 1 << 6;
pub const ACTION_MASK_GATHER_ITEMS: u32 = 1 << 7;
pub const ACTION_MASK_DAMAGE_ENABLED: u32 = 1 << 8;
pub const ACTION_MASK_DAMAGE_MULTIPLIER: u32 = 1 << 9;
pub const ACTION_MASK_INVINCIBLE_ENABLED: u32 = 1 << 10;

pub const CONTROL_FOLLOW: u8 = 0;
pub const CONTROL_FORCE_OFF: u8 = 1;
pub const CONTROL_FORCE_ON: u8 = 2;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperStatusContractDecision {
    pub contract_ok: bool,
    pub process_alive: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperStatusSnapshotInput {
    pub last_tick_ms: u64,
    pub pid: u32,
    pub process_alive: bool,
    pub auto_transparent_enabled: bool,
    pub fullscreen_attack_target: bool,
    pub fullscreen_attack_patch_on: bool,
    pub attract_mode: i32,
    pub attract_positive: bool,
    pub gather_items_enabled: bool,
    pub damage_enabled: bool,
    pub damage_multiplier: i32,
    pub invincible_enabled: bool,
    pub summon_enabled: bool,
    pub summon_last_tick: u64,
    pub fullscreen_skill_enabled: bool,
    pub fullscreen_skill_active: bool,
    pub fullscreen_skill_hotkey: u32,
    pub hotkey_enabled: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct HelperControlApplyPlan {
    pub fullscreen_attack_target: Option<bool>,
    pub fullscreen_skill_enabled: Option<bool>,
    pub auto_transparent_enabled: Option<bool>,
    pub hotkey_enabled: Option<bool>,
    pub attract_mode: Option<i32>,
    pub attract_enabled: Option<bool>,
    pub attract_positive: Option<bool>,
    pub gather_items_enabled: Option<bool>,
    pub damage_multiplier: Option<i32>,
    pub damage_enabled: Option<bool>,
    pub invincible_enabled: Option<bool>,
    pub summon_sequence: u32,
    pub action_sequence: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct HelperControlRuntimeState {
    pub last_summon_sequence: u32,
    pub last_action_sequence: u32,
    pub fullscreen_attack: u8,
    pub fullscreen_skill: u8,
    pub auto_transparent: u8,
    pub attract: u8,
    pub hotkey_enabled: u8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperControlTickDecision {
    pub next_state: HelperControlRuntimeState,
    pub control_changed: bool,
    pub should_apply_overrides: bool,
    pub summon_sequence_changed: bool,
    pub action_sequence_changed: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct HelperOverridePlan {
    pub hotkey_enabled: bool,
    pub apply_fullscreen_attack_target: Option<bool>,
    pub apply_auto_transparent: Option<bool>,
    pub apply_attract_enabled: Option<bool>,
    pub apply_gather_items_enabled: Option<bool>,
    pub fullscreen_skill_enabled: bool,
    pub fullscreen_skill_active: bool,
}

pub fn normalize_control_value(value: u8) -> u8 {
    match value {
        CONTROL_FORCE_OFF | CONTROL_FORCE_ON => value,
        _ => CONTROL_FOLLOW,
    }
}

pub fn evaluate_helper_status_contract(snapshot: &HelperStatusV5) -> HelperStatusContractDecision {
    HelperStatusContractDecision {
        contract_ok: snapshot.Version == HELPER_STATUS_V5_VERSION
            && snapshot.Size == HELPER_STATUS_V5_SIZE,
        process_alive: snapshot.ProcessAlive != 0,
    }
}

pub fn evaluate_helper_control_contract(snapshot: &HelperControlV4) -> bool {
    snapshot.Version == HELPER_CONTROL_V4_VERSION && snapshot.Size == HELPER_CONTROL_V4_SIZE
}

pub fn build_helper_status_snapshot(input: HelperStatusSnapshotInput) -> HelperStatusV5 {
    HelperStatusV5 {
        LastTickMs: input.last_tick_ms,
        Pid: input.pid,
        ProcessAlive: i32::from(input.process_alive),
        AutoTransparentEnabled: i32::from(input.auto_transparent_enabled),
        FullscreenAttackTarget: i32::from(input.fullscreen_attack_target),
        FullscreenAttackPatchOn: i32::from(input.fullscreen_attack_patch_on),
        AttractMode: input.attract_mode,
        AttractPositive: i32::from(input.attract_positive),
        GatherItemsEnabled: i32::from(input.gather_items_enabled),
        DamageEnabled: i32::from(input.damage_enabled),
        DamageMultiplier: input.damage_multiplier,
        InvincibleEnabled: i32::from(input.invincible_enabled),
        SummonEnabled: i32::from(input.summon_enabled),
        SummonLastTick: input.summon_last_tick,
        FullscreenSkillEnabled: i32::from(input.fullscreen_skill_enabled),
        FullscreenSkillActive: i32::from(input.fullscreen_skill_active),
        FullscreenSkillHotkey: input.fullscreen_skill_hotkey,
        HotkeyEnabled: i32::from(input.hotkey_enabled),
        ..HelperStatusV5::default()
    }
}

pub fn decode_control_apply_plan(snapshot: &HelperControlV4) -> HelperControlApplyPlan {
    HelperControlApplyPlan {
        fullscreen_attack_target: (snapshot.ActionMask & ACTION_MASK_FULLSCREEN_ATTACK != 0)
            .then_some(snapshot.DesiredFullscreenAttack != 0),
        fullscreen_skill_enabled: (snapshot.ActionMask & ACTION_MASK_FULLSCREEN_SKILL != 0)
            .then_some(snapshot.DesiredFullscreenSkill != 0),
        auto_transparent_enabled: (snapshot.ActionMask & ACTION_MASK_AUTO_TRANSPARENT != 0)
            .then_some(snapshot.DesiredAutoTransparent != 0),
        hotkey_enabled: (snapshot.ActionMask & ACTION_MASK_HOTKEY_ENABLED != 0)
            .then_some(snapshot.DesiredHotkeyEnabled != 0),
        attract_mode: (snapshot.ActionMask & ACTION_MASK_ATTRACT_MODE != 0)
            .then_some(snapshot.DesiredAttractMode as i32),
        attract_enabled: (snapshot.ActionMask & ACTION_MASK_ATTRACT_ENABLED != 0)
            .then_some(snapshot.DesiredAttractEnabled != 0),
        attract_positive: (snapshot.ActionMask & ACTION_MASK_ATTRACT_POSITIVE != 0)
            .then_some(snapshot.DesiredAttractPositive != 0),
        gather_items_enabled: (snapshot.ActionMask & ACTION_MASK_GATHER_ITEMS != 0)
            .then_some(snapshot.DesiredGatherItemsEnabled != 0),
        damage_multiplier: (snapshot.ActionMask & ACTION_MASK_DAMAGE_MULTIPLIER != 0)
            .then_some(snapshot.DesiredDamageMultiplier as i32),
        damage_enabled: (snapshot.ActionMask & ACTION_MASK_DAMAGE_ENABLED != 0)
            .then_some(snapshot.DesiredDamageEnabled != 0),
        invincible_enabled: (snapshot.ActionMask & ACTION_MASK_INVINCIBLE_ENABLED != 0)
            .then_some(snapshot.DesiredInvincibleEnabled != 0),
        summon_sequence: snapshot.SummonSequence,
        action_sequence: snapshot.ActionSequence,
    }
}

pub fn evaluate_control_tick(
    state: HelperControlRuntimeState,
    snapshot: &HelperControlV4,
) -> HelperControlTickDecision {
    let normalized_fullscreen_attack = normalize_control_value(snapshot.FullscreenAttack);
    let normalized_fullscreen_skill = normalize_control_value(snapshot.FullscreenSkill);
    let normalized_auto_transparent = normalize_control_value(snapshot.AutoTransparent);
    let normalized_attract = normalize_control_value(snapshot.Attract);
    let normalized_hotkey_enabled = normalize_control_value(snapshot.HotkeyEnabled);

    let control_changed = normalized_fullscreen_attack != state.fullscreen_attack
        || normalized_fullscreen_skill != state.fullscreen_skill
        || normalized_auto_transparent != state.auto_transparent
        || normalized_attract != state.attract
        || normalized_hotkey_enabled != state.hotkey_enabled;

    let summon_sequence_changed = snapshot.SummonSequence != state.last_summon_sequence;
    let action_sequence_changed =
        snapshot.ActionSequence != 0 && snapshot.ActionSequence != state.last_action_sequence;

    HelperControlTickDecision {
        next_state: HelperControlRuntimeState {
            last_summon_sequence: snapshot.SummonSequence,
            last_action_sequence: if action_sequence_changed {
                snapshot.ActionSequence
            } else {
                state.last_action_sequence
            },
            fullscreen_attack: normalized_fullscreen_attack,
            fullscreen_skill: normalized_fullscreen_skill,
            auto_transparent: normalized_auto_transparent,
            attract: normalized_attract,
            hotkey_enabled: normalized_hotkey_enabled,
        },
        control_changed,
        should_apply_overrides: control_changed,
        summon_sequence_changed,
        action_sequence_changed,
    }
}

pub fn build_control_override_plan(
    fullscreen_attack: u8,
    fullscreen_skill: u8,
    auto_transparent: u8,
    attract: u8,
    hotkey_enabled: u8,
    config_fullscreen_skill_enabled: bool,
    default_hotkey_enabled: bool,
) -> HelperOverridePlan {
    let hotkey_enabled = match hotkey_enabled {
        CONTROL_FORCE_OFF => false,
        CONTROL_FORCE_ON => true,
        _ => default_hotkey_enabled,
    };

    let apply_fullscreen_attack_target = match fullscreen_attack {
        CONTROL_FORCE_OFF => Some(false),
        CONTROL_FORCE_ON => Some(true),
        _ => None,
    };

    let apply_auto_transparent = match auto_transparent {
        CONTROL_FORCE_OFF => Some(false),
        CONTROL_FORCE_ON => Some(true),
        _ => None,
    };

    let (apply_attract_enabled, apply_gather_items_enabled) = match attract {
        CONTROL_FORCE_OFF => (Some(false), Some(false)),
        CONTROL_FORCE_ON => (Some(true), Some(true)),
        _ => (None, None),
    };

    let (fullscreen_skill_enabled, fullscreen_skill_active) = match fullscreen_skill {
        CONTROL_FORCE_ON => (true, true),
        CONTROL_FORCE_OFF => (config_fullscreen_skill_enabled, false),
        _ => (config_fullscreen_skill_enabled, config_fullscreen_skill_enabled),
    };

    HelperOverridePlan {
        hotkey_enabled,
        apply_fullscreen_attack_target,
        apply_auto_transparent,
        apply_attract_enabled,
        apply_gather_items_enabled,
        fullscreen_skill_enabled,
        fullscreen_skill_active,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn helper_status_contract_matches_protocol_constants() {
        let snapshot = HelperStatusV5::default();
        let decision = evaluate_helper_status_contract(&snapshot);
        assert!(decision.contract_ok);
        assert!(!decision.process_alive);
    }

    #[test]
    fn helper_control_contract_requires_matching_version_and_size() {
        let snapshot = HelperControlV4::default();
        assert!(evaluate_helper_control_contract(&snapshot));
    }

    #[test]
    fn control_apply_plan_decodes_action_mask() {
        let snapshot = HelperControlV4 {
            ActionMask: ACTION_MASK_FULLSCREEN_ATTACK
                | ACTION_MASK_DAMAGE_MULTIPLIER
                | ACTION_MASK_INVINCIBLE_ENABLED,
            DesiredFullscreenAttack: 1,
            DesiredDamageMultiplier: 20,
            DesiredInvincibleEnabled: 1,
            ActionSequence: 7,
            ..HelperControlV4::default()
        };

        let plan = decode_control_apply_plan(&snapshot);
        assert_eq!(plan.fullscreen_attack_target, Some(true));
        assert_eq!(plan.damage_multiplier, Some(20));
        assert_eq!(plan.invincible_enabled, Some(true));
        assert_eq!(plan.action_sequence, 7);
        assert_eq!(plan.hotkey_enabled, None);
    }

    #[test]
    fn helper_status_snapshot_builder_preserves_protocol_defaults() {
        let snapshot = build_helper_status_snapshot(HelperStatusSnapshotInput {
            last_tick_ms: 100,
            pid: 42,
            process_alive: true,
            auto_transparent_enabled: true,
            fullscreen_attack_target: false,
            fullscreen_attack_patch_on: true,
            attract_mode: 2,
            attract_positive: true,
            gather_items_enabled: true,
            damage_enabled: true,
            damage_multiplier: 20,
            invincible_enabled: false,
            summon_enabled: true,
            summon_last_tick: 99,
            fullscreen_skill_enabled: true,
            fullscreen_skill_active: false,
            fullscreen_skill_hotkey: 0x31,
            hotkey_enabled: true,
        });
        let version = snapshot.Version;
        let size = snapshot.Size;
        let pid = snapshot.Pid;
        let process_alive = snapshot.ProcessAlive;
        let damage_multiplier = snapshot.DamageMultiplier;
        assert_eq!(version, HELPER_STATUS_V5_VERSION);
        assert_eq!(size, HELPER_STATUS_V5_SIZE);
        assert_eq!(pid, 42);
        assert_eq!(process_alive, 1);
        assert_eq!(damage_multiplier, 20);
    }

    #[test]
    fn control_tick_detects_changes_and_sequence_advances() {
        let snapshot = HelperControlV4 {
            FullscreenAttack: CONTROL_FORCE_ON,
            SummonSequence: 2,
            ActionSequence: 3,
            ..HelperControlV4::default()
        };
        let decision = evaluate_control_tick(HelperControlRuntimeState::default(), &snapshot);
        assert!(decision.control_changed);
        assert!(decision.should_apply_overrides);
        assert!(decision.summon_sequence_changed);
        assert!(decision.action_sequence_changed);
        assert_eq!(decision.next_state.fullscreen_attack, CONTROL_FORCE_ON);
        assert_eq!(decision.next_state.last_action_sequence, 3);
    }

    #[test]
    fn override_plan_respects_force_and_follow_modes() {
        let force_on = build_control_override_plan(
            CONTROL_FORCE_ON,
            CONTROL_FORCE_ON,
            CONTROL_FORCE_OFF,
            CONTROL_FORCE_ON,
            CONTROL_FORCE_OFF,
            false,
            true,
        );
        assert!(!force_on.hotkey_enabled);
        assert_eq!(force_on.apply_fullscreen_attack_target, Some(true));
        assert_eq!(force_on.apply_auto_transparent, Some(false));
        assert_eq!(force_on.apply_attract_enabled, Some(true));
        assert_eq!(force_on.apply_gather_items_enabled, Some(true));
        assert!(force_on.fullscreen_skill_enabled);
        assert!(force_on.fullscreen_skill_active);

        let follow = build_control_override_plan(
            CONTROL_FOLLOW,
            CONTROL_FOLLOW,
            CONTROL_FOLLOW,
            CONTROL_FOLLOW,
            CONTROL_FOLLOW,
            false,
            true,
        );
        assert!(follow.hotkey_enabled);
        assert_eq!(follow.apply_fullscreen_attack_target, None);
        assert!(!follow.fullscreen_skill_enabled);
        assert!(!follow.fullscreen_skill_active);
    }
}
