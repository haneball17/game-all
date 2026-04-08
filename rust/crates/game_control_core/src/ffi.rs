#![allow(clippy::missing_safety_doc, clippy::undocumented_unsafe_blocks)]

use crate::{
    ControlKeyStateCore, ForegroundDecision, ForegroundTracker, HeartbeatPlan, PublishHeader,
    PublishHeaderInput, WindowSnapshotInput, apply_profile, build_heartbeat_plan,
    build_profile_masks,
    build_physical_alignment_plan, build_publish_header, evaluate_foreground_state,
    finalize_input_mask, finalize_publish_profile,
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

#[unsafe(no_mangle)]
pub extern "C" fn game_control_core_key_state_create() -> *mut ControlKeyStateCore {
    Box::into_raw(Box::new(ControlKeyStateCore::default()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_key_state_destroy(state: *mut ControlKeyStateCore) {
    if state.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(state));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_key_state_set_state(
    state: *mut ControlKeyStateCore,
    vkey: u32,
    is_down: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    u32::from(unsafe { (&mut *state).set_state(vkey as usize, is_down != 0) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_key_state_clear(state: *mut ControlKeyStateCore) {
    if state.is_null() {
        return;
    }
    unsafe { (&mut *state).clear() };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_key_state_copy_edge_counters(
    state: *const ControlKeyStateCore,
    out_edge_ptr: *mut u32,
    out_len: usize,
) {
    if state.is_null() || out_edge_ptr.is_null() {
        return;
    }
    let out_edge = unsafe { std::slice::from_raw_parts_mut(out_edge_ptr, out_len) };
    unsafe { (&*state).copy_edge_counters(out_edge) };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_key_state_build_effective(
    state: *mut ControlKeyStateCore,
    repeat_mask_ptr: *const u8,
    repeat_mask_len: usize,
    repeat_interval_ms: u32,
    now_ms: u64,
    out_effective_down_ptr: *mut u8,
    out_effective_edge_ptr: *mut u32,
    out_len: usize,
) {
    if state.is_null()
        || repeat_mask_ptr.is_null()
        || out_effective_down_ptr.is_null()
        || out_effective_edge_ptr.is_null()
    {
        return;
    }
    let repeat_mask = unsafe { std::slice::from_raw_parts(repeat_mask_ptr, repeat_mask_len) };
    let out_effective_down = unsafe { std::slice::from_raw_parts_mut(out_effective_down_ptr, out_len) };
    let out_effective_edge = unsafe { std::slice::from_raw_parts_mut(out_effective_edge_ptr, out_len) };
    unsafe {
        (&mut *state).build_effective_state(
            repeat_mask,
            repeat_interval_ms,
            now_ms,
            out_effective_down,
            out_effective_edge,
        )
    };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_apply_profile(
    profile_mode: u32,
    keys_ptr: *const i32,
    key_len: usize,
    mapping_sources_ptr: *const i32,
    mapping_targets_ptr: *const i32,
    mapping_len: usize,
    mapping_behavior_replace: u32,
    down_ptr: *const u8,
    edge_counter_ptr: *const u32,
    toggle_state_ptr: *const u8,
    len: usize,
    keyboard_state_ptr: *mut u8,
    edge_out_ptr: *mut u32,
    target_mask_ptr: *mut u8,
    block_mask_ptr: *mut u8,
    mapping_source_mask_ptr: *mut u8,
) {
    if down_ptr.is_null()
        || edge_counter_ptr.is_null()
        || toggle_state_ptr.is_null()
        || keyboard_state_ptr.is_null()
        || edge_out_ptr.is_null()
        || target_mask_ptr.is_null()
        || block_mask_ptr.is_null()
        || mapping_source_mask_ptr.is_null()
    {
        return;
    }

    let keys = if keys_ptr.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(keys_ptr, key_len) }
    };
    let mapping_sources = if mapping_sources_ptr.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(mapping_sources_ptr, mapping_len) }
    };
    let mapping_targets = if mapping_targets_ptr.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(mapping_targets_ptr, mapping_len) }
    };
    let down = unsafe { std::slice::from_raw_parts(down_ptr, len) };
    let edge_counter = unsafe { std::slice::from_raw_parts(edge_counter_ptr, len) };
    let toggle_state = unsafe { std::slice::from_raw_parts(toggle_state_ptr, len) };
    let keyboard_state = unsafe { std::slice::from_raw_parts_mut(keyboard_state_ptr, len) };
    let edge_out = unsafe { std::slice::from_raw_parts_mut(edge_out_ptr, len) };
    let target_mask = unsafe { std::slice::from_raw_parts_mut(target_mask_ptr, len) };
    let block_mask = unsafe { std::slice::from_raw_parts_mut(block_mask_ptr, len) };
    let mapping_source_mask = unsafe { std::slice::from_raw_parts_mut(mapping_source_mask_ptr, len) };

    apply_profile(
        profile_mode,
        keys,
        mapping_sources,
        mapping_targets,
        mapping_behavior_replace != 0,
        down,
        edge_counter,
        toggle_state,
        keyboard_state,
        edge_out,
        target_mask,
        block_mask,
        mapping_source_mask,
    );
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn game_control_core_build_profile_masks(
    profile_mode: u32,
    keys_ptr: *const i32,
    key_len: usize,
    mapping_sources_ptr: *const i32,
    mapping_targets_ptr: *const i32,
    mapping_len: usize,
    mapping_behavior_replace: u32,
    target_mask_ptr: *mut u8,
    block_mask_ptr: *mut u8,
    mapping_source_mask_ptr: *mut u8,
    input_mask_ptr: *mut u8,
    len: usize,
) {
    if target_mask_ptr.is_null()
        || block_mask_ptr.is_null()
        || mapping_source_mask_ptr.is_null()
        || input_mask_ptr.is_null()
    {
        return;
    }

    let keys = if keys_ptr.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(keys_ptr, key_len) }
    };
    let mapping_sources = if mapping_sources_ptr.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(mapping_sources_ptr, mapping_len) }
    };
    let mapping_targets = if mapping_targets_ptr.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(mapping_targets_ptr, mapping_len) }
    };
    let target_mask = unsafe { std::slice::from_raw_parts_mut(target_mask_ptr, len) };
    let block_mask = unsafe { std::slice::from_raw_parts_mut(block_mask_ptr, len) };
    let mapping_source_mask = unsafe { std::slice::from_raw_parts_mut(mapping_source_mask_ptr, len) };
    let input_mask = unsafe { std::slice::from_raw_parts_mut(input_mask_ptr, len) };

    build_profile_masks(
        profile_mode,
        keys,
        mapping_sources,
        mapping_targets,
        mapping_behavior_replace != 0,
        target_mask,
        block_mask,
        mapping_source_mask,
        input_mask,
    );
}
