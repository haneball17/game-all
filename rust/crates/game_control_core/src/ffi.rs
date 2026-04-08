#![allow(clippy::missing_safety_doc, clippy::undocumented_unsafe_blocks)]

use crate::{
    ForegroundDecision, ForegroundTracker, HeartbeatPlan, PublishHeader, PublishHeaderInput,
    WindowSnapshotInput, build_heartbeat_plan, build_physical_alignment_plan, build_publish_header,
    evaluate_foreground_state, finalize_input_mask, finalize_publish_profile,
};

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ControlWindowSnapshotInterop {
    pub foreground_is_dnf: u32,
    pub foreground_process_id: u32,
    pub master_process_id: u32,
    pub total_count: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ControlForegroundTrackerInterop {
    pub last_foreground_pid: u32,
    pub last_foreground_tick_ms: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ControlForegroundDecisionInterop {
    pub last_foreground_pid: u32,
    pub last_foreground_tick_ms: u64,
    pub effective_foreground_pid: u32,
    pub effective_foreground_is_dnf: u32,
    pub auto_paused: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ControlPublishHeaderInterop {
    pub flags: u32,
    pub active_pid: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ControlHeartbeatPlanInterop {
    pub should_align_physical_input: u32,
    pub should_publish_snapshot: u32,
}

impl From<ControlWindowSnapshotInterop> for WindowSnapshotInput {
    fn from(value: ControlWindowSnapshotInterop) -> Self {
        Self {
            foreground_is_dnf: value.foreground_is_dnf != 0,
            foreground_process_id: value.foreground_process_id,
            master_process_id: value.master_process_id,
            total_count: value.total_count,
        }
    }
}

impl From<ControlForegroundTrackerInterop> for ForegroundTracker {
    fn from(value: ControlForegroundTrackerInterop) -> Self {
        Self {
            last_foreground_pid: value.last_foreground_pid,
            last_foreground_tick_ms: value.last_foreground_tick_ms,
        }
    }
}

impl From<ForegroundDecision> for ControlForegroundDecisionInterop {
    fn from(value: ForegroundDecision) -> Self {
        Self {
            last_foreground_pid: value.tracker.last_foreground_pid,
            last_foreground_tick_ms: value.tracker.last_foreground_tick_ms,
            effective_foreground_pid: value.effective_foreground_pid,
            effective_foreground_is_dnf: u32::from(value.effective_foreground_is_dnf),
            auto_paused: u32::from(value.auto_paused),
        }
    }
}

impl From<PublishHeader> for ControlPublishHeaderInterop {
    fn from(value: PublishHeader) -> Self {
        Self {
            flags: value.flags,
            active_pid: value.active_pid,
            profile_id: value.profile_id,
            profile_mode: value.profile_mode,
            last_tick: value.last_tick,
        }
    }
}

impl From<HeartbeatPlan> for ControlHeartbeatPlanInterop {
    fn from(value: HeartbeatPlan) -> Self {
        Self {
            should_align_physical_input: u32::from(value.should_align_physical_input),
            should_publish_snapshot: u32::from(value.should_publish_snapshot),
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn game_control_core_evaluate_foreground(
    snapshot: ControlWindowSnapshotInterop,
    tracker: ControlForegroundTrackerInterop,
    now_ms: u64,
    foreground_grace_ms: u64,
    disable_auto_pause: u32,
) -> ControlForegroundDecisionInterop {
    evaluate_foreground_state(
        snapshot.into(),
        tracker.into(),
        now_ms,
        foreground_grace_ms,
        disable_auto_pause != 0,
    )
    .into()
}

#[unsafe(no_mangle)]
pub extern "C" fn game_control_core_build_publish_header(
    user_paused: u32,
    auto_paused: u32,
    force_clear: u32,
    effective_foreground_is_dnf: u32,
    effective_foreground_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
) -> ControlPublishHeaderInterop {
    build_publish_header(PublishHeaderInput {
        user_paused: user_paused != 0,
        auto_paused: auto_paused != 0,
        force_clear: force_clear != 0,
        effective_foreground_is_dnf: effective_foreground_is_dnf != 0,
        effective_foreground_pid,
        profile_id,
        profile_mode,
        last_tick,
    })
    .into()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_finalize_publish_profile(
    profile_mode: u32,
    mapping_behavior_replace: u32,
    mapping_source_mask_ptr: *const u8,
    mapping_source_len: usize,
    block_mask_ptr: *mut u8,
    block_mask_len: usize,
) -> u32 {
    if mapping_source_mask_ptr.is_null() || block_mask_ptr.is_null() {
        return profile_mode;
    }
    let mapping_source_mask = unsafe { std::slice::from_raw_parts(mapping_source_mask_ptr, mapping_source_len) };
    let block_mask = unsafe { std::slice::from_raw_parts_mut(block_mask_ptr, block_mask_len) };
    finalize_publish_profile(profile_mode, mapping_behavior_replace != 0, mapping_source_mask, block_mask)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_finalize_input_mask(
    profile_mode: u32,
    mapping_behavior_replace: u32,
    mapping_source_mask_ptr: *const u8,
    mapping_source_len: usize,
    input_mask_ptr: *mut u8,
    input_mask_len: usize,
) {
    if mapping_source_mask_ptr.is_null() || input_mask_ptr.is_null() {
        return;
    }
    let mapping_source_mask = unsafe { std::slice::from_raw_parts(mapping_source_mask_ptr, mapping_source_len) };
    let input_mask = unsafe { std::slice::from_raw_parts_mut(input_mask_ptr, input_mask_len) };
    finalize_input_mask(profile_mode, mapping_behavior_replace != 0, mapping_source_mask, input_mask);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_build_physical_alignment_plan(
    paused: u32,
    effective_foreground_is_dnf: u32,
    input_mask_ptr: *const u8,
    physical_down_ptr: *const u8,
    len: usize,
    out_apply_mask_ptr: *mut u8,
    out_desired_down_ptr: *mut u8,
) {
    if input_mask_ptr.is_null()
        || physical_down_ptr.is_null()
        || out_apply_mask_ptr.is_null()
        || out_desired_down_ptr.is_null()
    {
        return;
    }
    let input_mask = unsafe { std::slice::from_raw_parts(input_mask_ptr, len) };
    let physical_down = unsafe { std::slice::from_raw_parts(physical_down_ptr, len) };
    let out_apply_mask = unsafe { std::slice::from_raw_parts_mut(out_apply_mask_ptr, len) };
    let out_desired_down = unsafe { std::slice::from_raw_parts_mut(out_desired_down_ptr, len) };
    build_physical_alignment_plan(
        paused != 0,
        effective_foreground_is_dnf != 0,
        input_mask,
        physical_down,
        out_apply_mask,
        out_desired_down,
    );
}

#[unsafe(no_mangle)]
pub extern "C" fn game_control_core_build_heartbeat_plan(
    shared_memory_ready: u32,
) -> ControlHeartbeatPlanInterop {
    build_heartbeat_plan(shared_memory_ready != 0).into()
}
