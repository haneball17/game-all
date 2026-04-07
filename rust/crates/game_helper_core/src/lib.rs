use game_core_protocols::{
    HELPER_CONTROL_V4_SIZE, HELPER_CONTROL_V4_VERSION, HELPER_STATUS_V5_SIZE,
    HELPER_STATUS_V5_VERSION, HelperControlV4, HelperStatusV5,
};

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperStatusContractDecision {
    pub contract_ok: bool,
    pub process_alive: bool,
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
}
