#![allow(clippy::missing_safety_doc, clippy::undocumented_unsafe_blocks)]

use crate::{
    diagnostics::{
        AdapterDiagnosticsBuffer, AdapterDiagnosticsEvent, AdapterDiagnosticsEventKind,
        SyncObservationSnapshot, build_sync_observation_snapshot,
    },
    runtime::{
        AdapterDriftSummary, AdapterProjectedState, ChannelEmitDecision, EmitAction,
        InputPathObservation, KeyDecision, LogicalKeyDecision, PathDecision, RuntimeDecision,
        decide_channel_emit, decide_channel_emit_with_store, evaluate_adapter_projected_state, evaluate_key_state_header,
        evaluate_logical_key_header, evaluate_logical_key_with_store, evaluate_path_decision_header, evaluate_runtime_header,
        evaluate_runtime_state, observe_input_path, summarize_adapter_drift,
    },
    sync::{
        ClearResetDecision, DirectionConvergenceState, DirectionReleasePolicy,
        DirectionTransitionDecision, MappingTransitionDecision, PauseReleaseDecision,
        PauseReleaseReason, ProjectedChannelKind, ProjectedStateUpdate, SnapshotCachePolicy,
        SyncStateStore,
    },
};
use game_core_protocols::{SHARED_KEYBOARD_KEY_COUNT, SharedKeyboardStateV2};

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadRuntimeDecisionInterop {
    pub is_valid: u32,
    pub is_alive: u32,
    pub is_paused: u32,
    pub should_clear: u32,
    pub is_bypass_process: u32,
    pub active_pid: u32,
    pub flags: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadKeyDecisionInterop {
    pub is_valid: u32,
    pub is_alive: u32,
    pub is_paused: u32,
    pub should_clear: u32,
    pub is_bypass_process: u32,
    pub target_marked: u32,
    pub block_marked: u32,
    pub should_block: u32,
    pub desired_down: u32,
    pub active_pid: u32,
    pub flags: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadLogicalKeyDecisionInterop {
    pub is_valid: u32,
    pub is_alive: u32,
    pub is_paused: u32,
    pub should_clear: u32,
    pub is_bypass_process: u32,
    pub target_marked: u32,
    pub block_marked: u32,
    pub should_block: u32,
    pub desired_down: u32,
    pub pressed_edge: u32,
    pub released_edge: u32,
    pub is_direction: u32,
    pub pair_conflict: u32,
    pub repeat_allowed: u32,
    pub active_pid: u32,
    pub flags: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadChannelEmitDecisionInterop {
    pub emit_action: u32,
    pub desired_down: u32,
    pub projected_down_before: u32,
    pub projected_down_after: u32,
    pub should_block: u32,
    pub suppress_repeat: u32,
    pub transition_reason: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadPathDecisionInterop {
    pub is_valid: u32,
    pub is_alive: u32,
    pub is_paused: u32,
    pub should_clear: u32,
    pub is_bypass_process: u32,
    pub should_spoof_focus: u32,
    pub can_process_keys: u32,
    pub should_use_mapping: u32,
    pub active_pid: u32,
    pub flags: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadInputPathObservationInterop {
    pub channel_kind: u32,
    pub raw_promoted: u32,
    pub raw_active: u32,
    pub direct_input_active: u32,
    pub win32_active: u32,
    pub mixed_inputs: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadAdapterProjectedStateInterop {
    pub desired_down: u32,
    pub raw_projected: u32,
    pub win32_projected: u32,
    pub direct_input_projected: u32,
    pub raw_drift: u32,
    pub win32_drift: u32,
    pub direct_input_drift: u32,
    pub any_drift: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadAdapterDriftSummaryInterop {
    pub raw_drift_count: u32,
    pub win32_drift_count: u32,
    pub direct_input_drift_count: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadPauseReleaseDecisionInterop {
    pub should_emit: u32,
    pub vkey: u32,
    pub is_down: u32,
    pub had_projected: u32,
    pub reason: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadClearResetDecisionInterop {
    pub should_clear_logical: u32,
    pub should_clear_projected: u32,
    pub raw_projected_cleared: u32,
    pub win32_projected_cleared: u32,
    pub direct_input_projected_cleared: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadProjectedStateUpdateInterop {
    pub changed: u32,
    pub projected_before: u32,
    pub projected_after: u32,
    pub transition_reason: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadMappingTransitionDecisionInterop {
    pub should_emit: u32,
    pub vkey: u32,
    pub is_down: u32,
    pub next_scan_cursor: u32,
    pub reason: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadDirectionTransitionDecisionInterop {
    pub should_emit: u32,
    pub vkey: u32,
    pub is_down: u32,
    pub desired_down: u32,
    pub projected_before: u32,
    pub projected_after: u32,
    pub reason: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadSyncObservationSnapshotInterop {
    pub channel_kind: u32,
    pub active_pid: u32,
    pub is_alive: u32,
    pub is_paused: u32,
    pub raw_promoted: u32,
    pub raw_active: u32,
    pub direct_input_active: u32,
    pub win32_active: u32,
    pub mixed_inputs: u32,
    pub raw_drift_count: u32,
    pub win32_drift_count: u32,
    pub direct_input_drift_count: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadAdapterDiagnosticsEventInterop {
    pub tick_ms: u64,
    pub event_kind: u32,
    pub channel_kind: u32,
    pub vkey: u32,
    pub desired_down: u32,
    pub projected_before: u32,
    pub projected_after: u32,
    pub forced_release: u32,
    pub reason_code: u32,
}

impl From<RuntimeDecision> for PayloadRuntimeDecisionInterop {
    fn from(value: RuntimeDecision) -> Self {
        Self {
            is_valid: u32::from(value.is_valid),
            is_alive: u32::from(value.is_alive),
            is_paused: u32::from(value.is_paused),
            should_clear: u32::from(value.should_clear),
            is_bypass_process: u32::from(value.is_bypass_process),
            active_pid: value.active_pid,
            flags: value.flags,
            profile_id: value.profile_id,
            profile_mode: value.profile_mode,
            last_tick: value.last_tick,
        }
    }
}

impl From<KeyDecision> for PayloadKeyDecisionInterop {
    fn from(value: KeyDecision) -> Self {
        Self {
            is_valid: u32::from(value.runtime.is_valid),
            is_alive: u32::from(value.runtime.is_alive),
            is_paused: u32::from(value.runtime.is_paused),
            should_clear: u32::from(value.runtime.should_clear),
            is_bypass_process: u32::from(value.runtime.is_bypass_process),
            target_marked: u32::from(value.target_marked),
            block_marked: u32::from(value.block_marked),
            should_block: u32::from(value.should_block),
            desired_down: u32::from(value.desired_down),
            active_pid: value.runtime.active_pid,
            flags: value.runtime.flags,
            profile_id: value.runtime.profile_id,
            profile_mode: value.runtime.profile_mode,
            last_tick: value.runtime.last_tick,
        }
    }
}

impl From<LogicalKeyDecision> for PayloadLogicalKeyDecisionInterop {
    fn from(value: LogicalKeyDecision) -> Self {
        Self {
            is_valid: u32::from(value.key.runtime.is_valid),
            is_alive: u32::from(value.key.runtime.is_alive),
            is_paused: u32::from(value.key.runtime.is_paused),
            should_clear: u32::from(value.key.runtime.should_clear),
            is_bypass_process: u32::from(value.key.runtime.is_bypass_process),
            target_marked: u32::from(value.key.target_marked),
            block_marked: u32::from(value.key.block_marked),
            should_block: u32::from(value.key.should_block),
            desired_down: u32::from(value.key.desired_down),
            pressed_edge: u32::from(value.pressed_edge),
            released_edge: u32::from(value.released_edge),
            is_direction: u32::from(value.is_direction),
            pair_conflict: u32::from(value.pair_conflict),
            repeat_allowed: u32::from(value.repeat_allowed),
            active_pid: value.key.runtime.active_pid,
            flags: value.key.runtime.flags,
            profile_id: value.key.runtime.profile_id,
            profile_mode: value.key.runtime.profile_mode,
            last_tick: value.key.runtime.last_tick,
        }
    }
}

impl From<ChannelEmitDecision> for PayloadChannelEmitDecisionInterop {
    fn from(value: ChannelEmitDecision) -> Self {
        Self {
            emit_action: match value.emit_action {
                EmitAction::None => 0,
                EmitAction::Press => 1,
                EmitAction::Release => 2,
            },
            desired_down: u32::from(value.desired_down),
            projected_down_before: u32::from(value.projected_down_before),
            projected_down_after: u32::from(value.projected_down_after),
            should_block: u32::from(value.should_block),
            suppress_repeat: u32::from(value.suppress_repeat),
            transition_reason: value.transition_reason as u32,
        }
    }
}

impl From<PathDecision> for PayloadPathDecisionInterop {
    fn from(value: PathDecision) -> Self {
        Self {
            is_valid: u32::from(value.runtime.is_valid),
            is_alive: u32::from(value.runtime.is_alive),
            is_paused: u32::from(value.runtime.is_paused),
            should_clear: u32::from(value.runtime.should_clear),
            is_bypass_process: u32::from(value.runtime.is_bypass_process),
            should_spoof_focus: u32::from(value.should_spoof_focus),
            can_process_keys: u32::from(value.can_process_keys),
            should_use_mapping: u32::from(value.should_use_mapping),
            active_pid: value.runtime.active_pid,
            flags: value.runtime.flags,
            profile_id: value.runtime.profile_id,
            profile_mode: value.runtime.profile_mode,
            last_tick: value.runtime.last_tick,
        }
    }
}

impl From<InputPathObservation> for PayloadInputPathObservationInterop {
    fn from(value: InputPathObservation) -> Self {
        Self {
            channel_kind: value.channel as u32,
            raw_promoted: u32::from(value.raw_promoted),
            raw_active: u32::from(value.raw_active),
            direct_input_active: u32::from(value.direct_input_active),
            win32_active: u32::from(value.win32_active),
            mixed_inputs: u32::from(value.mixed_inputs),
            profile_id: value.profile_id,
            profile_mode: value.profile_mode,
        }
    }
}

impl From<AdapterProjectedState> for PayloadAdapterProjectedStateInterop {
    fn from(value: AdapterProjectedState) -> Self {
        Self {
            desired_down: u32::from(value.desired_down),
            raw_projected: u32::from(value.raw_projected),
            win32_projected: u32::from(value.win32_projected),
            direct_input_projected: u32::from(value.direct_input_projected),
            raw_drift: u32::from(value.raw_drift),
            win32_drift: u32::from(value.win32_drift),
            direct_input_drift: u32::from(value.direct_input_drift),
            any_drift: u32::from(value.any_drift),
        }
    }
}

impl From<AdapterDriftSummary> for PayloadAdapterDriftSummaryInterop {
    fn from(value: AdapterDriftSummary) -> Self {
        Self {
            raw_drift_count: value.raw_drift_count,
            win32_drift_count: value.win32_drift_count,
            direct_input_drift_count: value.direct_input_drift_count,
        }
    }
}

impl From<PauseReleaseDecision> for PayloadPauseReleaseDecisionInterop {
    fn from(value: PauseReleaseDecision) -> Self {
        Self {
            should_emit: u32::from(value.should_emit),
            vkey: value.vkey,
            is_down: u32::from(value.is_down),
            had_projected: u32::from(value.had_projected),
            reason: match value.reason {
                PauseReleaseReason::None => 0,
                PauseReleaseReason::PreferredRelease => 1,
                PauseReleaseReason::PairRelease => 2,
                PauseReleaseReason::DirectionRelease => 3,
                PauseReleaseReason::StaleRelease => 4,
                PauseReleaseReason::Neutralize => 5,
            },
        }
    }
}

impl From<ClearResetDecision> for PayloadClearResetDecisionInterop {
    fn from(value: ClearResetDecision) -> Self {
        Self {
            should_clear_logical: u32::from(value.should_clear_logical),
            should_clear_projected: u32::from(value.should_clear_projected),
            raw_projected_cleared: u32::from(value.raw_projected_cleared),
            win32_projected_cleared: u32::from(value.win32_projected_cleared),
            direct_input_projected_cleared: u32::from(value.direct_input_projected_cleared),
        }
    }
}

impl From<SyncObservationSnapshot> for PayloadSyncObservationSnapshotInterop {
    fn from(value: SyncObservationSnapshot) -> Self {
        Self {
            channel_kind: value.channel as u32,
            active_pid: value.active_pid,
            is_alive: u32::from(value.is_alive),
            is_paused: u32::from(value.is_paused),
            raw_promoted: u32::from(value.raw_promoted),
            raw_active: u32::from(value.raw_active),
            direct_input_active: u32::from(value.direct_input_active),
            win32_active: u32::from(value.win32_active),
            mixed_inputs: u32::from(value.mixed_inputs),
            raw_drift_count: value.raw_drift_count,
            win32_drift_count: value.win32_drift_count,
            direct_input_drift_count: value.direct_input_drift_count,
            profile_id: value.profile_id,
            profile_mode: value.profile_mode,
        }
    }
}

impl From<ProjectedStateUpdate> for PayloadProjectedStateUpdateInterop {
    fn from(value: ProjectedStateUpdate) -> Self {
        Self {
            changed: u32::from(value.changed),
            projected_before: u32::from(value.projected_before),
            projected_after: u32::from(value.projected_after),
            transition_reason: value.transition_reason as u32,
        }
    }
}

impl From<MappingTransitionDecision> for PayloadMappingTransitionDecisionInterop {
    fn from(value: MappingTransitionDecision) -> Self {
        Self {
            should_emit: u32::from(value.should_emit),
            vkey: value.vkey,
            is_down: u32::from(value.is_down),
            next_scan_cursor: value.next_scan_cursor,
            reason: value.reason as u32,
        }
    }
}

impl From<DirectionTransitionDecision> for PayloadDirectionTransitionDecisionInterop {
    fn from(value: DirectionTransitionDecision) -> Self {
        Self {
            should_emit: u32::from(value.should_emit),
            vkey: value.vkey,
            is_down: u32::from(value.is_down),
            desired_down: u32::from(value.desired_down),
            projected_before: u32::from(value.projected_before),
            projected_after: u32::from(value.projected_after),
            reason: value.reason as u32,
        }
    }
}

impl From<AdapterDiagnosticsEvent> for PayloadAdapterDiagnosticsEventInterop {
    fn from(value: AdapterDiagnosticsEvent) -> Self {
        Self {
            tick_ms: value.tick_ms,
            event_kind: value.event_kind as u32,
            channel_kind: value.channel_kind as u32,
            vkey: value.vkey,
            desired_down: u32::from(value.desired_down),
            projected_before: u32::from(value.projected_before),
            projected_after: u32::from(value.projected_after),
            forced_release: u32::from(value.forced_release),
            reason_code: value.reason_code,
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_evaluate_runtime_state(
    snapshot_ptr: *const SharedKeyboardStateV2,
    mapping_size: usize,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    out_decision: *mut PayloadRuntimeDecisionInterop,
) -> u32 {
    if snapshot_ptr.is_null() || out_decision.is_null() {
        return 0;
    }
    let snapshot = unsafe { &*snapshot_ptr };
    let decision = evaluate_runtime_state(
        snapshot,
        mapping_size,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
    );
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_evaluate_runtime_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    out_decision: *mut PayloadRuntimeDecisionInterop,
) -> u32 {
    if out_decision.is_null() {
        return 0;
    }
    let decision = evaluate_runtime_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
    );
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_evaluate_key_state_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    target_marked: u32,
    block_marked: u32,
    keyboard_down: u32,
    force_release: u32,
    out_decision: *mut PayloadKeyDecisionInterop,
) -> u32 {
    if out_decision.is_null() {
        return 0;
    }
    let decision = evaluate_key_state_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
        target_marked != 0,
        block_marked != 0,
        keyboard_down != 0,
        force_release != 0,
    );
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_evaluate_logical_key_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    vkey: u32,
    target_marked: u32,
    block_marked: u32,
    keyboard_down: u32,
    edge_counter: u32,
    pair_vkey: u32,
    pair_target_marked: u32,
    pair_keyboard_down: u32,
    pair_edge_counter: u32,
    force_release: u32,
    previous_desired_down: u32,
    repeat_allowed: u32,
    out_decision: *mut PayloadLogicalKeyDecisionInterop,
) -> u32 {
    if out_decision.is_null() {
        return 0;
    }
    let decision = evaluate_logical_key_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
        vkey,
        target_marked != 0,
        block_marked != 0,
        keyboard_down != 0,
        edge_counter,
        pair_vkey,
        pair_target_marked != 0,
        pair_keyboard_down != 0,
        pair_edge_counter,
        force_release != 0,
        previous_desired_down != 0,
        repeat_allowed != 0,
    );
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_decide_channel_emit(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    vkey: u32,
    target_marked: u32,
    block_marked: u32,
    keyboard_down: u32,
    edge_counter: u32,
    pair_vkey: u32,
    pair_target_marked: u32,
    pair_keyboard_down: u32,
    pair_edge_counter: u32,
    force_release: u32,
    previous_desired_down: u32,
    repeat_allowed: u32,
    projected_down_before: u32,
    observed_down: u32,
    out_logical: *mut PayloadLogicalKeyDecisionInterop,
    out_emit: *mut PayloadChannelEmitDecisionInterop,
) -> u32 {
    if out_emit.is_null() {
        return 0;
    }
    let logical = evaluate_logical_key_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
        vkey,
        target_marked != 0,
        block_marked != 0,
        keyboard_down != 0,
        edge_counter,
        pair_vkey,
        pair_target_marked != 0,
        pair_keyboard_down != 0,
        pair_edge_counter,
        force_release != 0,
        previous_desired_down != 0,
        repeat_allowed != 0,
    );
    let emit = decide_channel_emit(logical, projected_down_before != 0, observed_down != 0);
    if !out_logical.is_null() {
        unsafe { out_logical.write(logical.into()) };
    }
    unsafe { out_emit.write(emit.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_evaluate_path_decision_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    mapping_mode_value: u32,
    out_decision: *mut PayloadPathDecisionInterop,
) -> u32 {
    if out_decision.is_null() {
        return 0;
    }
    let decision = evaluate_path_decision_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
        mapping_mode_value,
    );
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_observe_input_path(
    raw_promoted: u32,
    raw_data_count: u32,
    raw_buffer_count: u32,
    di_state_count: u32,
    di_data_count: u32,
    win32_async_count: u32,
    win32_keyboard_count: u32,
    profile_id: u32,
    profile_mode: u32,
    out_observation: *mut PayloadInputPathObservationInterop,
) -> u32 {
    if out_observation.is_null() {
        return 0;
    }
    let observation = observe_input_path(
        raw_promoted != 0,
        raw_data_count,
        raw_buffer_count,
        di_state_count,
        di_data_count,
        win32_async_count,
        win32_keyboard_count,
        profile_id,
        profile_mode,
    );
    unsafe { out_observation.write(observation.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_evaluate_adapter_projected_state(
    desired_down: u32,
    raw_projected: u32,
    win32_projected: u32,
    direct_input_projected: u32,
    out_state: *mut PayloadAdapterProjectedStateInterop,
) -> u32 {
    if out_state.is_null() {
        return 0;
    }
    let state = evaluate_adapter_projected_state(
        desired_down != 0,
        raw_projected != 0,
        win32_projected != 0,
        direct_input_projected != 0,
    );
    unsafe { out_state.write(state.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_summarize_adapter_drift(
    logical_desired_ptr: *const u8,
    raw_projected_ptr: *const u8,
    win32_projected_ptr: *const u8,
    direct_input_projected_ptr: *const u8,
    len: usize,
    out_summary: *mut PayloadAdapterDriftSummaryInterop,
) -> u32 {
    if logical_desired_ptr.is_null()
        || raw_projected_ptr.is_null()
        || win32_projected_ptr.is_null()
        || direct_input_projected_ptr.is_null()
        || out_summary.is_null()
    {
        return 0;
    }
    let logical_desired = unsafe { std::slice::from_raw_parts(logical_desired_ptr, len) };
    let raw_projected = unsafe { std::slice::from_raw_parts(raw_projected_ptr, len) };
    let win32_projected = unsafe { std::slice::from_raw_parts(win32_projected_ptr, len) };
    let direct_input_projected = unsafe { std::slice::from_raw_parts(direct_input_projected_ptr, len) };
    let summary = summarize_adapter_drift(
        logical_desired,
        raw_projected,
        win32_projected,
        direct_input_projected,
    );
    unsafe { out_summary.write(summary.into()) };
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn payload_core_convergence_create(
    extra_release_pulses: u8,
) -> *mut DirectionConvergenceState {
    Box::into_raw(Box::new(DirectionConvergenceState::new(
        DirectionReleasePolicy {
            extra_release_pulses,
        },
    )))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_convergence_destroy(state: *mut DirectionConvergenceState) {
    if state.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(state));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_convergence_on_key_event(
    state: *mut DirectionConvergenceState,
    vkey: u32,
    is_down: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    let state = unsafe { &mut *state };
    state.on_key_event(vkey as usize, is_down != 0);
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_convergence_take_force_release_mask(
    state: *mut DirectionConvergenceState,
    out_mask: *mut u8,
    out_len: usize,
) -> u32 {
    if state.is_null() || out_mask.is_null() || out_len < SHARED_KEYBOARD_KEY_COUNT {
        return 0;
    }
    let state = unsafe { &mut *state };
    let mask = state.take_force_release_mask();
    let out = unsafe { std::slice::from_raw_parts_mut(out_mask, out_len) };
    out[..SHARED_KEYBOARD_KEY_COUNT].copy_from_slice(&mask);
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_convergence_should_refresh(
    state: *const DirectionConvergenceState,
    age_ms: u64,
    max_age_ms: u64,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    let state = unsafe { &*state };
    let policy = SnapshotCachePolicy { max_age_ms };
    u32::from(matches!(
        policy.decide(age_ms, state),
        crate::sync::CacheDecision::Refresh
    ))
}

#[unsafe(no_mangle)]
pub extern "C" fn payload_core_state_store_create() -> *mut SyncStateStore {
    Box::into_raw(Box::new(SyncStateStore::default()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_destroy(state: *mut SyncStateStore) {
    if state.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(state));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_set_logical_desired(
    state: *mut SyncStateStore,
    vkey: u32,
    down: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    unsafe { (&mut *state).set_logical_desired(vkey as usize, down != 0) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_get_logical_desired(
    state: *const SyncStateStore,
    vkey: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    u32::from(unsafe { (&*state).logical_desired(vkey as usize) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_set_projected(
    state: *mut SyncStateStore,
    channel_kind: u32,
    vkey: u32,
    down: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    let channel = match channel_kind {
        1 => ProjectedChannelKind::Raw,
        2 => ProjectedChannelKind::Win32,
        3 => ProjectedChannelKind::DirectInput,
        _ => return 0,
    };
    unsafe { (&mut *state).set_projected(channel, vkey as usize, down != 0) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_get_projected(
    state: *const SyncStateStore,
    channel_kind: u32,
    vkey: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    let channel = match channel_kind {
        1 => ProjectedChannelKind::Raw,
        2 => ProjectedChannelKind::Win32,
        3 => ProjectedChannelKind::DirectInput,
        _ => return 0,
    };
    u32::from(unsafe { (&*state).projected(channel, vkey as usize) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_evaluate_logical_key(
    state: *mut SyncStateStore,
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    vkey: u32,
    target_marked: u32,
    block_marked: u32,
    keyboard_down: u32,
    edge_counter: u32,
    pair_vkey: u32,
    pair_target_marked: u32,
    pair_keyboard_down: u32,
    pair_edge_counter: u32,
    force_release: u32,
    repeat_allowed: u32,
    out_logical: *mut PayloadLogicalKeyDecisionInterop,
) -> u32 {
    if state.is_null() || out_logical.is_null() {
        return 0;
    }
    let logical = unsafe {
        evaluate_logical_key_with_store(
            &mut *state,
            flags,
            active_pid,
            profile_id,
            profile_mode,
            last_tick,
            current_pid,
            now_tick,
            heartbeat_timeout_ms,
            vkey,
            target_marked != 0,
            block_marked != 0,
            keyboard_down != 0,
            edge_counter,
            pair_vkey,
            pair_target_marked != 0,
            pair_keyboard_down != 0,
            pair_edge_counter,
            force_release != 0,
            repeat_allowed != 0,
        )
    };
    unsafe { out_logical.write(logical.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_decide_channel_emit(
    state: *mut SyncStateStore,
    channel_kind: u32,
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    vkey: u32,
    target_marked: u32,
    block_marked: u32,
    keyboard_down: u32,
    edge_counter: u32,
    pair_vkey: u32,
    pair_target_marked: u32,
    pair_keyboard_down: u32,
    pair_edge_counter: u32,
    force_release: u32,
    repeat_allowed: u32,
    observed_down: u32,
    out_logical: *mut PayloadLogicalKeyDecisionInterop,
    out_emit: *mut PayloadChannelEmitDecisionInterop,
) -> u32 {
    if state.is_null() || out_emit.is_null() {
        return 0;
    }
    let channel = match channel_kind {
        1 => ProjectedChannelKind::Raw,
        2 => ProjectedChannelKind::Win32,
        3 => ProjectedChannelKind::DirectInput,
        _ => return 0,
    };
    let (logical, emit) = unsafe {
        decide_channel_emit_with_store(
            &mut *state,
            channel,
            flags,
            active_pid,
            profile_id,
            profile_mode,
            last_tick,
            current_pid,
            now_tick,
            heartbeat_timeout_ms,
            vkey,
            target_marked != 0,
            block_marked != 0,
            keyboard_down != 0,
            edge_counter,
            pair_vkey,
            pair_target_marked != 0,
            pair_keyboard_down != 0,
            pair_edge_counter,
            force_release != 0,
            repeat_allowed != 0,
            observed_down != 0,
        )
    };
    if !out_logical.is_null() {
        unsafe { out_logical.write(logical.into()) };
    }
    unsafe { out_emit.write(emit.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_clear_logical_desired(
    state: *mut SyncStateStore,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    unsafe { (&mut *state).clear_logical_desired() };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_clear_all_projected(
    state: *mut SyncStateStore,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    unsafe { (&mut *state).clear_all_projected() };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_clear_projected_channel(
    state: *mut SyncStateStore,
    channel_kind: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    let channel = match channel_kind {
        1 => ProjectedChannelKind::Raw,
        2 => ProjectedChannelKind::Win32,
        3 => ProjectedChannelKind::DirectInput,
        _ => return 0,
    };
    unsafe { (&mut *state).clear_projected_channel(channel) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_update_projected(
    state: *mut SyncStateStore,
    channel_kind: u32,
    vkey: u32,
    down: u32,
    out_update: *mut PayloadProjectedStateUpdateInterop,
) -> u32 {
    if state.is_null() || out_update.is_null() {
        return 0;
    }
    let channel = match channel_kind {
        1 => ProjectedChannelKind::Raw,
        2 => ProjectedChannelKind::Win32,
        3 => ProjectedChannelKind::DirectInput,
        _ => return 0,
    };
    let update = unsafe { (&mut *state).update_projected(channel, vkey as usize, down != 0) };
    unsafe { out_update.write(update.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_select_mapping_transition(
    state: *mut SyncStateStore,
    target_mask_ptr: *const u8,
    keyboard_state_ptr: *const u8,
    len: usize,
    allow_down: u32,
    start: usize,
    out_decision: *mut PayloadMappingTransitionDecisionInterop,
) -> u32 {
    if state.is_null() || target_mask_ptr.is_null() || keyboard_state_ptr.is_null() || out_decision.is_null() {
        return 0;
    }
    let target_mask = unsafe { std::slice::from_raw_parts(target_mask_ptr, len) };
    let keyboard_state = unsafe { std::slice::from_raw_parts(keyboard_state_ptr, len) };
    let decision = unsafe { (&mut *state).select_mapping_transition(target_mask, keyboard_state, allow_down != 0, start) };
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_select_direction_transition(
    state: *mut SyncStateStore,
    target_mask_ptr: *const u8,
    keyboard_state_ptr: *const u8,
    edge_counter_ptr: *const u32,
    force_release_mask_ptr: *const u8,
    len: usize,
    can_process_keys: u32,
    preferred_vkey: i32,
    out_decision: *mut PayloadDirectionTransitionDecisionInterop,
) -> u32 {
    if state.is_null()
        || target_mask_ptr.is_null()
        || keyboard_state_ptr.is_null()
        || edge_counter_ptr.is_null()
        || force_release_mask_ptr.is_null()
        || out_decision.is_null()
    {
        return 0;
    }
    let target_mask = unsafe { std::slice::from_raw_parts(target_mask_ptr, len) };
    let keyboard_state = unsafe { std::slice::from_raw_parts(keyboard_state_ptr, len) };
    let edge_counter = unsafe { std::slice::from_raw_parts(edge_counter_ptr, len) };
    let force_release_mask = unsafe { std::slice::from_raw_parts(force_release_mask_ptr, len) };
    let decision = unsafe {
        (&mut *state).select_direction_transition(
            target_mask,
            keyboard_state,
            edge_counter,
            can_process_keys != 0,
            preferred_vkey,
            force_release_mask,
        )
    };
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_pick_pause_release(
    state: *mut SyncStateStore,
    preferred_vkey: i32,
    out_decision: *mut PayloadPauseReleaseDecisionInterop,
) -> u32 {
    if state.is_null() || out_decision.is_null() {
        return 0;
    }
    let decision = unsafe { (&mut *state).pick_pause_release(preferred_vkey) };
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_apply_clear_reset(
    state: *mut SyncStateStore,
    clear_logical: u32,
    clear_projected: u32,
    out_decision: *mut PayloadClearResetDecisionInterop,
) -> u32 {
    if state.is_null() || out_decision.is_null() {
        return 0;
    }
    let decision = unsafe { (&mut *state).apply_clear_reset(clear_logical != 0, clear_projected != 0) };
    unsafe { out_decision.write(decision.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_state_store_summarize_drift(
    state: *const SyncStateStore,
    out_summary: *mut PayloadAdapterDriftSummaryInterop,
) -> u32 {
    if state.is_null() || out_summary.is_null() {
        return 0;
    }
    let summary = unsafe { (&*state).summarize_drift() };
    unsafe { out_summary.write(summary.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_build_sync_observation_snapshot(
    active_pid: u32,
    is_alive: u32,
    is_paused: u32,
    observation: *const PayloadInputPathObservationInterop,
    drift: *const PayloadAdapterDriftSummaryInterop,
    out_snapshot: *mut PayloadSyncObservationSnapshotInterop,
) -> u32 {
    if observation.is_null() || drift.is_null() || out_snapshot.is_null() {
        return 0;
    }
    let observation = unsafe { &*observation };
    let drift = unsafe { &*drift };
    let channel = match observation.channel_kind {
        1 => crate::runtime::InputChannelKind::Win32,
        2 => crate::runtime::InputChannelKind::RawInput,
        3 => crate::runtime::InputChannelKind::DirectInput,
        _ => crate::runtime::InputChannelKind::Unknown,
    };
    let snapshot = build_sync_observation_snapshot(
        active_pid,
        is_alive != 0,
        is_paused != 0,
        InputPathObservation {
            channel,
            raw_promoted: observation.raw_promoted != 0,
            raw_active: observation.raw_active != 0,
            direct_input_active: observation.direct_input_active != 0,
            win32_active: observation.win32_active != 0,
            mixed_inputs: observation.mixed_inputs != 0,
            profile_id: observation.profile_id,
            profile_mode: observation.profile_mode,
        },
        AdapterDriftSummary {
            raw_drift_count: drift.raw_drift_count,
            win32_drift_count: drift.win32_drift_count,
            direct_input_drift_count: drift.direct_input_drift_count,
        },
    );
    unsafe { out_snapshot.write(snapshot.into()) };
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn payload_core_diagnostics_buffer_create(capacity: usize) -> *mut AdapterDiagnosticsBuffer {
    Box::into_raw(Box::new(AdapterDiagnosticsBuffer::new(capacity)))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_diagnostics_buffer_destroy(buffer: *mut AdapterDiagnosticsBuffer) {
    if buffer.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(buffer));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_diagnostics_buffer_push_event(
    buffer: *mut AdapterDiagnosticsBuffer,
    event: *const PayloadAdapterDiagnosticsEventInterop,
) -> u32 {
    if buffer.is_null() || event.is_null() {
        return 0;
    }
    let event = unsafe { &*event };
    let channel = match event.channel_kind {
        1 => crate::runtime::InputChannelKind::Win32,
        2 => crate::runtime::InputChannelKind::RawInput,
        3 => crate::runtime::InputChannelKind::DirectInput,
        _ => crate::runtime::InputChannelKind::Unknown,
    };
    let event_kind = match event.event_kind {
        1 => AdapterDiagnosticsEventKind::LogicalChanged,
        2 => AdapterDiagnosticsEventKind::AdapterEmitted,
        3 => AdapterDiagnosticsEventKind::PauseRelease,
        4 => AdapterDiagnosticsEventKind::ClearApplied,
        _ => AdapterDiagnosticsEventKind::LogicalChanged,
    };
    unsafe {
        (&mut *buffer).push(AdapterDiagnosticsEvent {
            tick_ms: event.tick_ms,
            event_kind,
            channel_kind: channel,
            vkey: event.vkey,
            desired_down: event.desired_down != 0,
            projected_before: event.projected_before != 0,
            projected_after: event.projected_after != 0,
            forced_release: event.forced_release != 0,
            reason_code: event.reason_code,
        });
    }
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_diagnostics_buffer_latest(
    buffer: *const AdapterDiagnosticsBuffer,
    out_event: *mut PayloadAdapterDiagnosticsEventInterop,
) -> u32 {
    if buffer.is_null() || out_event.is_null() {
        return 0;
    }
    let Some(event) = (unsafe { &*buffer }).latest() else {
        return 0;
    };
    unsafe { out_event.write(event.into()) };
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn payload_core_diagnostics_buffer_copy_latest_n(
    buffer: *const AdapterDiagnosticsBuffer,
    limit: usize,
    out_events: *mut PayloadAdapterDiagnosticsEventInterop,
    out_capacity: usize,
) -> usize {
    if buffer.is_null() || out_events.is_null() || out_capacity == 0 {
        return 0;
    }
    let events = (unsafe { &*buffer }).copy_latest_n(limit.min(out_capacity));
    for (idx, event) in events.iter().enumerate() {
        unsafe { out_events.add(idx).write((*event).into()) };
    }
    events.len()
}
