use game_core_protocols::{
    SHARED_KEYBOARD_STATE_V2_SIZE, SHARED_KEYBOARD_STATE_V2_VERSION, SYNC_FLAG_CLEAR,
    SYNC_FLAG_PAUSED, SharedKeyboardStateV2,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeDecision {
    pub is_valid: bool,
    pub is_alive: bool,
    pub is_paused: bool,
    pub should_clear: bool,
    pub is_bypass_process: bool,
    pub active_pid: u32,
    pub flags: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KeyDecision {
    pub runtime: RuntimeDecision,
    pub target_marked: bool,
    pub block_marked: bool,
    pub should_block: bool,
    pub desired_down: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LogicalKeyDecision {
    pub key: KeyDecision,
    pub pressed_edge: bool,
    pub released_edge: bool,
    pub is_direction: bool,
    pub pair_conflict: bool,
    pub repeat_allowed: bool,
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EmitAction {
    None = 0,
    Press = 1,
    Release = 2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ChannelEmitDecision {
    pub emit_action: EmitAction,
    pub desired_down: bool,
    pub projected_down_before: bool,
    pub projected_down_after: bool,
    pub should_block: bool,
    pub suppress_repeat: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PathDecision {
    pub runtime: RuntimeDecision,
    pub should_spoof_focus: bool,
    pub can_process_keys: bool,
    pub should_use_mapping: bool,
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InputChannelKind {
    Unknown = 0,
    Win32 = 1,
    RawInput = 2,
    DirectInput = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InputPathObservation {
    pub channel: InputChannelKind,
    pub raw_promoted: bool,
    pub raw_active: bool,
    pub direct_input_active: bool,
    pub win32_active: bool,
    pub mixed_inputs: bool,
    pub profile_id: u32,
    pub profile_mode: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AdapterProjectedState {
    pub desired_down: bool,
    pub raw_projected: bool,
    pub win32_projected: bool,
    pub direct_input_projected: bool,
    pub raw_drift: bool,
    pub win32_drift: bool,
    pub direct_input_drift: bool,
    pub any_drift: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AdapterDriftSummary {
    pub raw_drift_count: u32,
    pub win32_drift_count: u32,
    pub direct_input_drift_count: u32,
}

fn is_direction_vkey(vkey: u32) -> bool {
    matches!(vkey, 0x25..=0x28)
}

fn resolve_direction_conflict(
    desired_down: bool,
    key_edge: u32,
    pair_desired_down: bool,
    pair_edge: u32,
) -> (bool, bool) {
    if !(desired_down && pair_desired_down) {
        return (desired_down, false);
    }

    if key_edge > pair_edge {
        return (true, true);
    }
    if pair_edge > key_edge {
        return (false, true);
    }

    (false, true)
}

#[allow(clippy::too_many_arguments)]
pub fn evaluate_runtime_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
) -> RuntimeDecision {
    let is_alive = last_tick != 0
        && now_tick >= last_tick
        && now_tick.saturating_sub(last_tick) <= heartbeat_timeout_ms;
    let is_paused = (flags & SYNC_FLAG_PAUSED) != 0;
    let should_clear = (flags & SYNC_FLAG_CLEAR) != 0;
    let is_bypass_process = active_pid != 0 && active_pid == current_pid;

    RuntimeDecision {
        is_valid: true,
        is_alive,
        is_paused,
        should_clear,
        is_bypass_process,
        active_pid,
        flags,
        profile_id,
        profile_mode,
        last_tick,
    }
}

pub fn evaluate_runtime_state(
    snapshot: &SharedKeyboardStateV2,
    mapping_size: usize,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
) -> RuntimeDecision {
    let is_valid = snapshot.Version == SHARED_KEYBOARD_STATE_V2_VERSION
        && mapping_size >= SHARED_KEYBOARD_STATE_V2_SIZE as usize;
    let mut decision = evaluate_runtime_header(
        snapshot.Flags,
        snapshot.ActivePid,
        snapshot.ProfileId,
        snapshot.ProfileMode,
        snapshot.LastTick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
    );
    decision.is_valid = is_valid;
    decision.is_alive = decision.is_alive && is_valid;
    decision
}

#[allow(clippy::too_many_arguments)]
pub fn evaluate_key_state_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    target_marked: bool,
    block_marked: bool,
    keyboard_down: bool,
    force_release: bool,
) -> KeyDecision {
    let runtime = evaluate_runtime_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
    );
    let should_block = runtime.is_alive && !runtime.is_paused && block_marked;
    let desired_down = runtime.is_alive
        && !runtime.is_paused
        && !runtime.should_clear
        && target_marked
        && keyboard_down
        && !force_release;

    KeyDecision {
        runtime,
        target_marked,
        block_marked,
        should_block,
        desired_down,
    }
}

#[allow(clippy::too_many_arguments)]
pub fn evaluate_logical_key_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    vkey: u32,
    target_marked: bool,
    block_marked: bool,
    keyboard_down: bool,
    edge_counter: u32,
    pair_vkey: u32,
    pair_target_marked: bool,
    pair_keyboard_down: bool,
    pair_edge_counter: u32,
    force_release: bool,
    previous_desired_down: bool,
    repeat_allowed: bool,
) -> LogicalKeyDecision {
    let key = evaluate_key_state_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
        target_marked,
        block_marked,
        keyboard_down,
        force_release,
    );

    let is_direction = is_direction_vkey(vkey);
    let mut desired_down = key.desired_down;
    let mut pair_conflict = false;

    if is_direction && is_direction_vkey(pair_vkey) {
        let pair_desired = key.runtime.is_alive
            && !key.runtime.is_paused
            && !key.runtime.should_clear
            && pair_target_marked
            && pair_keyboard_down;
        let (resolved, conflicted) =
            resolve_direction_conflict(desired_down, edge_counter, pair_desired, pair_edge_counter);
        desired_down = resolved;
        pair_conflict = conflicted;
    }

    let repeat_allowed = repeat_allowed && !is_direction;
    let pressed_edge = desired_down && !previous_desired_down;
    let released_edge = !desired_down && previous_desired_down;

    LogicalKeyDecision {
        key: KeyDecision {
            desired_down,
            ..key
        },
        pressed_edge,
        released_edge,
        is_direction,
        pair_conflict,
        repeat_allowed,
    }
}

pub fn decide_channel_emit(
    logical: LogicalKeyDecision,
    projected_down_before: bool,
    observed_down: bool,
) -> ChannelEmitDecision {
    let mut emit_action = EmitAction::None;
    let desired_down = logical.key.desired_down;
    let mut projected_down_after = projected_down_before;
    let suppress_repeat = desired_down
        && projected_down_before
        && observed_down
        && !logical.repeat_allowed
        && !logical.pressed_edge;

    if logical.key.should_block {
        if projected_down_before {
            emit_action = EmitAction::Release;
            projected_down_after = false;
        }
    } else if desired_down != projected_down_before {
        emit_action = if desired_down {
            EmitAction::Press
        } else {
            EmitAction::Release
        };
        projected_down_after = desired_down;
    }

    ChannelEmitDecision {
        emit_action,
        desired_down,
        projected_down_before,
        projected_down_after,
        should_block: logical.key.should_block,
        suppress_repeat,
    }
}

#[allow(clippy::too_many_arguments)]
pub fn evaluate_path_decision_header(
    flags: u32,
    active_pid: u32,
    profile_id: u32,
    profile_mode: u32,
    last_tick: u64,
    current_pid: u32,
    now_tick: u64,
    heartbeat_timeout_ms: u64,
    mapping_mode_value: u32,
) -> PathDecision {
    let runtime = evaluate_runtime_header(
        flags,
        active_pid,
        profile_id,
        profile_mode,
        last_tick,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
    );
    let can_process_keys = runtime.is_alive && !runtime.is_paused && !runtime.is_bypass_process;
    let should_spoof_focus = can_process_keys && runtime.active_pid != 0 && !runtime.should_clear;
    let should_use_mapping = can_process_keys && runtime.profile_mode == mapping_mode_value;

    PathDecision {
        runtime,
        should_spoof_focus,
        can_process_keys,
        should_use_mapping,
    }
}

#[allow(clippy::too_many_arguments)]
pub fn observe_input_path(
    raw_promoted: bool,
    raw_data_count: u32,
    raw_buffer_count: u32,
    di_state_count: u32,
    di_data_count: u32,
    win32_async_count: u32,
    win32_keyboard_count: u32,
    profile_id: u32,
    profile_mode: u32,
) -> InputPathObservation {
    let raw_active = raw_data_count.saturating_add(raw_buffer_count) > 0;
    let direct_input_active = di_state_count.saturating_add(di_data_count) > 0;
    let win32_active = win32_async_count.saturating_add(win32_keyboard_count) > 0;
    let active_count = u32::from(raw_active) + u32::from(direct_input_active) + u32::from(win32_active);

    let channel = if raw_active {
        InputChannelKind::RawInput
    } else if direct_input_active {
        InputChannelKind::DirectInput
    } else if win32_active {
        InputChannelKind::Win32
    } else {
        InputChannelKind::Unknown
    };

    InputPathObservation {
        channel,
        raw_promoted,
        raw_active,
        direct_input_active,
        win32_active,
        mixed_inputs: active_count > 1,
        profile_id,
        profile_mode,
    }
}

pub fn evaluate_adapter_projected_state(
    desired_down: bool,
    raw_projected: bool,
    win32_projected: bool,
    direct_input_projected: bool,
) -> AdapterProjectedState {
    let raw_drift = raw_projected != desired_down;
    let win32_drift = win32_projected != desired_down;
    let direct_input_drift = direct_input_projected != desired_down;
    AdapterProjectedState {
        desired_down,
        raw_projected,
        win32_projected,
        direct_input_projected,
        raw_drift,
        win32_drift,
        direct_input_drift,
        any_drift: raw_drift || win32_drift || direct_input_drift,
    }
}

pub fn summarize_adapter_drift(
    logical_desired: &[u8],
    raw_projected: &[u8],
    win32_projected: &[u8],
    direct_input_projected: &[u8],
) -> AdapterDriftSummary {
    let len = logical_desired
        .len()
        .min(raw_projected.len())
        .min(win32_projected.len())
        .min(direct_input_projected.len());
    let mut summary = AdapterDriftSummary {
        raw_drift_count: 0,
        win32_drift_count: 0,
        direct_input_drift_count: 0,
    };
    for idx in 0..len {
        let state = evaluate_adapter_projected_state(
            logical_desired[idx] != 0,
            raw_projected[idx] != 0,
            win32_projected[idx] != 0,
            direct_input_projected[idx] != 0,
        );
        summary.raw_drift_count += u32::from(state.raw_drift);
        summary.win32_drift_count += u32::from(state.win32_drift);
        summary.direct_input_drift_count += u32::from(state.direct_input_drift);
    }
    summary
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn runtime_state_marks_alive_and_bypass() {
        let snapshot = SharedKeyboardStateV2 {
            ActivePid: 123,
            LastTick: 1000,
            ..SharedKeyboardStateV2::default()
        };
        let state = evaluate_runtime_state(
            &snapshot,
            SHARED_KEYBOARD_STATE_V2_SIZE as usize,
            123,
            1200,
            600,
        );
        assert!(state.is_valid);
        assert!(state.is_alive);
        assert!(state.is_bypass_process);
        assert!(!state.is_paused);
        assert!(!state.should_clear);
    }

    #[test]
    fn runtime_header_marks_alive_and_bypass() {
        let state = evaluate_runtime_header(0, 123, 1, 2, 1000, 123, 1200, 600);
        assert!(state.is_valid);
        assert!(state.is_alive);
        assert!(state.is_bypass_process);
        assert!(!state.is_paused);
        assert!(!state.should_clear);
        assert_eq!(state.profile_id, 1);
        assert_eq!(state.profile_mode, 2);
    }

    #[test]
    fn runtime_state_marks_paused_and_clear() {
        let snapshot = SharedKeyboardStateV2 {
            Flags: SYNC_FLAG_PAUSED | SYNC_FLAG_CLEAR,
            LastTick: 1000,
            ..SharedKeyboardStateV2::default()
        };
        let state = evaluate_runtime_state(
            &snapshot,
            SHARED_KEYBOARD_STATE_V2_SIZE as usize,
            456,
            1100,
            500,
        );
        assert!(state.is_valid);
        assert!(state.is_alive);
        assert!(state.is_paused);
        assert!(state.should_clear);
        assert!(!state.is_bypass_process);
    }

    #[test]
    fn runtime_state_marks_stale_snapshot_as_not_alive() {
        let snapshot = SharedKeyboardStateV2 {
            LastTick: 1000,
            ..SharedKeyboardStateV2::default()
        };
        let state = evaluate_runtime_state(
            &snapshot,
            SHARED_KEYBOARD_STATE_V2_SIZE as usize,
            456,
            3000,
            500,
        );
        assert!(state.is_valid);
        assert!(!state.is_alive);
    }

    #[test]
    fn key_state_forces_release_even_when_snapshot_is_down() {
        let state =
            evaluate_key_state_header(0, 321, 9, 3, 1000, 111, 1100, 500, true, false, true, true);
        assert!(state.runtime.is_alive);
        assert!(state.target_marked);
        assert!(!state.desired_down);
        assert!(!state.should_block);
    }

    #[test]
    fn key_state_blocks_non_target_blacklist_key() {
        let state = evaluate_key_state_header(
            0, 321, 9, 2, 1000, 111, 1100, 500, false, true, false, false,
        );
        assert!(state.runtime.is_alive);
        assert!(!state.target_marked);
        assert!(state.should_block);
        assert!(!state.desired_down);
    }

    #[test]
    fn path_state_spoofs_focus_for_background_slave() {
        let state = evaluate_path_decision_header(0, 321, 9, 3, 1000, 111, 1100, 500, 3);
        assert!(state.runtime.is_alive);
        assert!(!state.runtime.is_bypass_process);
        assert!(state.can_process_keys);
        assert!(state.should_spoof_focus);
        assert!(state.should_use_mapping);
    }

    #[test]
    fn path_state_stops_processing_when_paused() {
        let state =
            evaluate_path_decision_header(SYNC_FLAG_PAUSED, 321, 9, 3, 1000, 111, 1100, 500, 3);
        assert!(state.runtime.is_alive);
        assert!(state.runtime.is_paused);
        assert!(!state.can_process_keys);
        assert!(!state.should_spoof_focus);
        assert!(!state.should_use_mapping);
    }

    #[test]
    fn logical_direction_uses_newer_edge_as_winner() {
        let logical = evaluate_logical_key_header(
            0, 100, 1, 2, 1000, 200, 1100, 500, 0x25, true, false, true, 10, 0x27, true, true, 11,
            false, false, false,
        );
        assert!(!logical.key.desired_down);
        assert!(logical.pair_conflict);
        assert!(!logical.pressed_edge);
    }

    #[test]
    fn logical_direction_releases_both_when_edge_ties() {
        let logical = evaluate_logical_key_header(
            0, 100, 1, 2, 1000, 200, 1100, 500, 0x25, true, false, true, 10, 0x27, true, true, 10,
            false, true, false,
        );
        assert!(!logical.key.desired_down);
        assert!(logical.released_edge);
        assert!(logical.pair_conflict);
    }

    #[test]
    fn channel_emit_suppresses_repeat_when_already_projected() {
        let logical = evaluate_logical_key_header(
            0, 100, 1, 2, 1000, 200, 1100, 500, 0x25, true, false, true, 10, 0, false, false, 0,
            false, true, false,
        );
        let emit = decide_channel_emit(logical, true, true);
        assert_eq!(emit.emit_action, EmitAction::None);
        assert!(emit.suppress_repeat);
    }

    #[test]
    fn channel_emit_presses_on_false_to_true_transition() {
        let logical = evaluate_logical_key_header(
            0, 100, 1, 2, 1000, 200, 1100, 500, 0x41, true, false, true, 3, 0, false, false, 0,
            false, false, false,
        );
        let emit = decide_channel_emit(logical, false, true);
        assert_eq!(emit.emit_action, EmitAction::Press);
        assert!(emit.projected_down_after);
    }

    #[test]
    fn channel_emit_releases_projected_key_when_paused() {
        let logical = evaluate_logical_key_header(
            SYNC_FLAG_PAUSED,
            100,
            1,
            2,
            1000,
            200,
            1100,
            500,
            0x25,
            true,
            false,
            true,
            3,
            0,
            false,
            false,
            0,
            false,
            true,
            false,
        );
        let emit = decide_channel_emit(logical, true, true);
        assert_eq!(emit.emit_action, EmitAction::Release);
        assert!(!emit.projected_down_after);
        assert!(!emit.desired_down);
    }

    #[test]
    fn channel_emit_keeps_silent_when_paused_and_not_projected() {
        let logical = evaluate_logical_key_header(
            SYNC_FLAG_PAUSED,
            100,
            1,
            2,
            1000,
            200,
            1100,
            500,
            0x25,
            true,
            false,
            true,
            3,
            0,
            false,
            false,
            0,
            false,
            false,
            false,
        );
        let emit = decide_channel_emit(logical, false, true);
        assert_eq!(emit.emit_action, EmitAction::None);
        assert!(!emit.projected_down_after);
        assert!(!emit.desired_down);
    }

    #[test]
    fn input_path_observation_preserves_existing_priority_and_marks_mixed() {
        let raw = observe_input_path(true, 3, 1, 2, 0, 1, 0, 7, 3);
        assert_eq!(raw.channel, InputChannelKind::RawInput);
        assert!(raw.mixed_inputs);
        assert!(raw.raw_promoted);

        let di = observe_input_path(false, 0, 0, 2, 1, 0, 0, 7, 3);
        assert_eq!(di.channel, InputChannelKind::DirectInput);
        assert!(!di.mixed_inputs);

        let win = observe_input_path(false, 0, 0, 0, 0, 4, 1, 7, 3);
        assert_eq!(win.channel, InputChannelKind::Win32);
        assert!(!win.mixed_inputs);
    }

    #[test]
    fn adapter_projected_state_reports_drift() {
        let state = evaluate_adapter_projected_state(true, false, true, false);
        assert!(state.raw_drift);
        assert!(!state.win32_drift);
        assert!(state.direct_input_drift);
        assert!(state.any_drift);
    }

    #[test]
    fn summarize_adapter_drift_counts_each_channel() {
        let summary = summarize_adapter_drift(
            &[1, 0, 1],
            &[0, 0, 1],
            &[1, 1, 1],
            &[0, 0, 0],
        );
        assert_eq!(summary.raw_drift_count, 1);
        assert_eq!(summary.win32_drift_count, 1);
        assert_eq!(summary.direct_input_drift_count, 2);
    }
}
