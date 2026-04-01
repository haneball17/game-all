use game_core_protocols::{
    SHARED_KEYBOARD_STATE_V2_VERSION, SHARED_KEYBOARD_STATE_V2_SIZE, SYNC_FLAG_CLEAR,
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
}
