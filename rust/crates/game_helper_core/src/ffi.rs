#![allow(clippy::missing_safety_doc, clippy::undocumented_unsafe_blocks)]

use crate::{
    HelperControlApplyPlan, HelperStatusContractDecision, HelperStatusSnapshotInput,
    build_helper_status_snapshot, decode_control_apply_plan, evaluate_helper_control_contract,
    evaluate_helper_status_contract,
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
pub struct HelperStatusSnapshotInputInterop {
    pub last_tick_ms: u64,
    pub pid: u32,
    pub process_alive: u32,
    pub auto_transparent_enabled: u32,
    pub fullscreen_attack_target: u32,
    pub fullscreen_attack_patch_on: u32,
    pub attract_mode: i32,
    pub attract_positive: u32,
    pub gather_items_enabled: u32,
    pub damage_enabled: u32,
    pub damage_multiplier: i32,
    pub invincible_enabled: u32,
    pub summon_enabled: u32,
    pub summon_last_tick: u64,
    pub fullscreen_skill_enabled: u32,
    pub fullscreen_skill_active: u32,
    pub fullscreen_skill_hotkey: u32,
    pub hotkey_enabled: u32,
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

impl From<HelperStatusSnapshotInputInterop> for HelperStatusSnapshotInput {
    fn from(value: HelperStatusSnapshotInputInterop) -> Self {
        Self {
            last_tick_ms: value.last_tick_ms,
            pid: value.pid,
            process_alive: value.process_alive != 0,
            auto_transparent_enabled: value.auto_transparent_enabled != 0,
            fullscreen_attack_target: value.fullscreen_attack_target != 0,
            fullscreen_attack_patch_on: value.fullscreen_attack_patch_on != 0,
            attract_mode: value.attract_mode,
            attract_positive: value.attract_positive != 0,
            gather_items_enabled: value.gather_items_enabled != 0,
            damage_enabled: value.damage_enabled != 0,
            damage_multiplier: value.damage_multiplier,
            invincible_enabled: value.invincible_enabled != 0,
            summon_enabled: value.summon_enabled != 0,
            summon_last_tick: value.summon_last_tick,
            fullscreen_skill_enabled: value.fullscreen_skill_enabled != 0,
            fullscreen_skill_active: value.fullscreen_skill_active != 0,
            fullscreen_skill_hotkey: value.fullscreen_skill_hotkey,
            hotkey_enabled: value.hotkey_enabled != 0,
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

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_helper_core_build_status_snapshot(
    input: *const HelperStatusSnapshotInputInterop,
    out_snapshot: *mut core::ffi::c_void,
) -> u32 {
    if input.is_null() || out_snapshot.is_null() {
        return 0;
    }
    let snapshot = build_helper_status_snapshot(unsafe { (*input).into() });
    unsafe { (out_snapshot as *mut HelperStatusV5).write(snapshot) };
    1
}
