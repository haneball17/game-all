#![allow(clippy::missing_safety_doc, clippy::undocumented_unsafe_blocks)]

use crate::{
    HelperControlApplyPlan, HelperStatusContractDecision, decode_control_apply_plan,
    evaluate_helper_control_contract, evaluate_helper_status_contract,
};
use game_core_protocols::{HelperControlV4, HelperStatusV5};

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperStatusContractDecisionInterop {
    pub contract_ok: u32,
    pub process_alive: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HelperControlApplyPlanInterop {
    pub apply_fullscreen_attack_target: u32,
    pub fullscreen_attack_target: u32,
    pub apply_fullscreen_skill_enabled: u32,
    pub fullscreen_skill_enabled: u32,
    pub apply_auto_transparent_enabled: u32,
    pub auto_transparent_enabled: u32,
    pub apply_hotkey_enabled: u32,
    pub hotkey_enabled: u32,
    pub apply_attract_mode: u32,
    pub attract_mode: i32,
    pub apply_attract_enabled: u32,
    pub attract_enabled: u32,
    pub apply_attract_positive: u32,
    pub attract_positive: u32,
    pub apply_gather_items_enabled: u32,
    pub gather_items_enabled: u32,
    pub apply_damage_multiplier: u32,
    pub damage_multiplier: i32,
    pub apply_damage_enabled: u32,
    pub damage_enabled: u32,
    pub apply_invincible_enabled: u32,
    pub invincible_enabled: u32,
    pub summon_sequence: u32,
    pub action_sequence: u32,
}

impl From<HelperStatusContractDecision> for HelperStatusContractDecisionInterop {
    fn from(value: HelperStatusContractDecision) -> Self {
        Self {
            contract_ok: u32::from(value.contract_ok),
            process_alive: u32::from(value.process_alive),
        }
    }
}

impl From<HelperControlApplyPlan> for HelperControlApplyPlanInterop {
    fn from(value: HelperControlApplyPlan) -> Self {
        Self {
            apply_fullscreen_attack_target: u32::from(value.fullscreen_attack_target.is_some()),
            fullscreen_attack_target: u32::from(value.fullscreen_attack_target.unwrap_or(false)),
            apply_fullscreen_skill_enabled: u32::from(value.fullscreen_skill_enabled.is_some()),
            fullscreen_skill_enabled: u32::from(value.fullscreen_skill_enabled.unwrap_or(false)),
            apply_auto_transparent_enabled: u32::from(value.auto_transparent_enabled.is_some()),
            auto_transparent_enabled: u32::from(value.auto_transparent_enabled.unwrap_or(false)),
            apply_hotkey_enabled: u32::from(value.hotkey_enabled.is_some()),
            hotkey_enabled: u32::from(value.hotkey_enabled.unwrap_or(false)),
            apply_attract_mode: u32::from(value.attract_mode.is_some()),
            attract_mode: value.attract_mode.unwrap_or_default(),
            apply_attract_enabled: u32::from(value.attract_enabled.is_some()),
            attract_enabled: u32::from(value.attract_enabled.unwrap_or(false)),
            apply_attract_positive: u32::from(value.attract_positive.is_some()),
            attract_positive: u32::from(value.attract_positive.unwrap_or(false)),
            apply_gather_items_enabled: u32::from(value.gather_items_enabled.is_some()),
            gather_items_enabled: u32::from(value.gather_items_enabled.unwrap_or(false)),
            apply_damage_multiplier: u32::from(value.damage_multiplier.is_some()),
            damage_multiplier: value.damage_multiplier.unwrap_or_default(),
            apply_damage_enabled: u32::from(value.damage_enabled.is_some()),
            damage_enabled: u32::from(value.damage_enabled.unwrap_or(false)),
            apply_invincible_enabled: u32::from(value.invincible_enabled.is_some()),
            invincible_enabled: u32::from(value.invincible_enabled.unwrap_or(false)),
            summon_sequence: value.summon_sequence,
            action_sequence: value.action_sequence,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_helper_core_evaluate_status_contract(
    snapshot: *const core::ffi::c_void,
    out_decision: *mut HelperStatusContractDecisionInterop,
) -> u32 {
    if snapshot.is_null() || out_decision.is_null() {
        return 0;
    }
    let decision = evaluate_helper_status_contract(unsafe { &*(snapshot as *const HelperStatusV5) });
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_helper_core_evaluate_control_contract(
    snapshot: *const core::ffi::c_void,
) -> u32 {
    if snapshot.is_null() {
        return 0;
    }
    u32::from(evaluate_helper_control_contract(unsafe { &*(snapshot as *const HelperControlV4) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_helper_core_decode_control_apply_plan(
    snapshot: *const core::ffi::c_void,
    out_plan: *mut HelperControlApplyPlanInterop,
) -> u32 {
    if snapshot.is_null() || out_plan.is_null() {
        return 0;
    }
    let plan = decode_control_apply_plan(unsafe { &*(snapshot as *const HelperControlV4) });
    unsafe { out_plan.write(plan.into()) };
    1
}
