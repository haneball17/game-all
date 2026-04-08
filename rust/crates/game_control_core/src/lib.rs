use game_core_protocols::{SYNC_FLAG_CLEAR, SYNC_FLAG_PAUSED};

pub mod ffi;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct WindowSnapshotInput {
    pub foreground_is_dnf: bool,
    pub foreground_process_id: u32,
    pub master_process_id: u32,
    pub total_count: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ForegroundTracker {
    pub last_foreground_pid: u32,
    pub last_foreground_tick_ms: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ForegroundDecision {
    pub tracker: ForegroundTracker,
    pub effective_foreground_pid: u32,
    pub effective_foreground_is_dnf: bool,
    pub auto_paused: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PublishHeader {
    pub flags: u32,
    pub active_pid: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HeartbeatPlan {
    pub should_align_physical_input: bool,
    pub should_publish_snapshot: bool,
}

pub const PROFILE_MODE_MAPPING: u32 = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PublishHeaderInput {
    pub user_paused: bool,
    pub auto_paused: bool,
    pub force_clear: bool,
    pub effective_foreground_is_dnf: bool,
    pub effective_foreground_pid: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
    pub last_tick: u64,
}

pub fn evaluate_foreground_state(
    input: WindowSnapshotInput,
    tracker: ForegroundTracker,
    now_ms: u64,
    foreground_grace_ms: u64,
    disable_auto_pause: bool,
) -> ForegroundDecision {
    if disable_auto_pause {
        let effective_pid = if input.foreground_process_id != 0 {
            input.foreground_process_id
        } else {
            input.master_process_id
        };
        return ForegroundDecision {
            tracker,
            effective_foreground_pid: effective_pid,
            effective_foreground_is_dnf: input.total_count > 0,
            auto_paused: false,
        };
    }

    let mut next_tracker = tracker;
    if input.foreground_is_dnf && input.foreground_process_id != 0 {
        next_tracker.last_foreground_pid = input.foreground_process_id;
        next_tracker.last_foreground_tick_ms = now_ms;
    }

    let grace_active = next_tracker.last_foreground_pid != 0
        && now_ms.saturating_sub(next_tracker.last_foreground_tick_ms) <= foreground_grace_ms;
    let effective_foreground_is_dnf = input.foreground_is_dnf || grace_active;
    let effective_foreground_pid = if input.foreground_is_dnf {
        input.foreground_process_id
    } else if grace_active {
        next_tracker.last_foreground_pid
    } else {
        0
    };

    ForegroundDecision {
        tracker: next_tracker,
        effective_foreground_pid,
        effective_foreground_is_dnf,
        auto_paused: !effective_foreground_is_dnf,
    }
}

pub fn build_publish_header(input: PublishHeaderInput) -> PublishHeader {
    let paused = input.user_paused || input.auto_paused;
    let mut flags = if paused { SYNC_FLAG_PAUSED } else { 0 };
    if input.force_clear {
        flags |= SYNC_FLAG_CLEAR;
    }

    PublishHeader {
        flags,
        active_pid: if input.effective_foreground_is_dnf {
            input.effective_foreground_pid
        } else {
            0
        },
        profile_id: input.profile_id,
        profile_mode: input.profile_mode,
        last_tick: input.last_tick,
    }
}

pub fn finalize_publish_profile(
    profile_mode: u32,
    mapping_behavior_replace: bool,
    mapping_source_mask: &[u8],
    block_mask: &mut [u8],
) -> u32 {
    let reported_mode = if profile_mode != PROFILE_MODE_MAPPING && mapping_behavior_replace {
        PROFILE_MODE_MAPPING
    } else {
        profile_mode
    };

    if reported_mode == PROFILE_MODE_MAPPING {
        let len = mapping_source_mask.len().min(block_mask.len());
        for idx in 0..len {
            if mapping_source_mask[idx] != 0 {
                block_mask[idx] |= 0x01;
            }
        }
    }

    reported_mode
}

pub fn finalize_input_mask(
    profile_mode: u32,
    mapping_behavior_replace: bool,
    mapping_source_mask: &[u8],
    input_mask: &mut [u8],
) {
    if profile_mode != PROFILE_MODE_MAPPING && !mapping_behavior_replace {
        return;
    }

    let len = mapping_source_mask.len().min(input_mask.len());
    for idx in 0..len {
        if mapping_source_mask[idx] != 0 {
            input_mask[idx] = 1;
        }
    }
}

pub fn build_physical_alignment_plan(
    paused: bool,
    effective_foreground_is_dnf: bool,
    input_mask: &[u8],
    physical_down: &[u8],
    out_apply_mask: &mut [u8],
    out_desired_down: &mut [u8],
) {
    let len = input_mask
        .len()
        .min(physical_down.len())
        .min(out_apply_mask.len())
        .min(out_desired_down.len());

    for idx in 0..len {
        out_apply_mask[idx] = 0;
        out_desired_down[idx] = 0;

        if input_mask[idx] == 0 {
            continue;
        }

        let physical_is_down = physical_down[idx] != 0;
        if !physical_is_down {
            out_apply_mask[idx] = 1;
            out_desired_down[idx] = 0;
            continue;
        }

        if !paused && effective_foreground_is_dnf {
            out_apply_mask[idx] = 1;
            out_desired_down[idx] = 1;
        }
    }
}

pub fn build_heartbeat_plan(shared_memory_ready: bool) -> HeartbeatPlan {
    HeartbeatPlan {
        should_align_physical_input: shared_memory_ready,
        should_publish_snapshot: shared_memory_ready,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn disable_auto_pause_uses_master_pid_when_foreground_pid_missing() {
        let decision = evaluate_foreground_state(
            WindowSnapshotInput {
                foreground_is_dnf: false,
                foreground_process_id: 0,
                master_process_id: 123,
                total_count: 2,
            },
            ForegroundTracker::default(),
            1000,
            800,
            true,
        );

        assert!(!decision.auto_paused);
        assert!(decision.effective_foreground_is_dnf);
        assert_eq!(decision.effective_foreground_pid, 123);
    }

    #[test]
    fn foreground_grace_keeps_sync_alive_briefly() {
        let first = evaluate_foreground_state(
            WindowSnapshotInput {
                foreground_is_dnf: true,
                foreground_process_id: 555,
                master_process_id: 111,
                total_count: 2,
            },
            ForegroundTracker::default(),
            1000,
            800,
            false,
        );
        let second = evaluate_foreground_state(
            WindowSnapshotInput {
                foreground_is_dnf: false,
                foreground_process_id: 0,
                master_process_id: 111,
                total_count: 2,
            },
            first.tracker,
            1500,
            800,
            false,
        );
        let third = evaluate_foreground_state(
            WindowSnapshotInput {
                foreground_is_dnf: false,
                foreground_process_id: 0,
                master_process_id: 111,
                total_count: 2,
            },
            first.tracker,
            2000,
            800,
            false,
        );

        assert!(second.effective_foreground_is_dnf);
        assert_eq!(second.effective_foreground_pid, 555);
        assert!(!second.auto_paused);
        assert!(!third.effective_foreground_is_dnf);
        assert_eq!(third.effective_foreground_pid, 0);
        assert!(third.auto_paused);
    }

    #[test]
    fn publish_header_zeros_active_pid_when_effective_foreground_is_not_dnf() {
        let header = build_publish_header(PublishHeaderInput {
            user_paused: false,
            auto_paused: true,
            force_clear: true,
            effective_foreground_is_dnf: false,
            effective_foreground_pid: 777,
            profile_id: 11,
            profile_mode: 3,
            last_tick: 9999,
        });
        assert_eq!(header.flags, SYNC_FLAG_PAUSED | SYNC_FLAG_CLEAR);
        assert_eq!(header.active_pid, 0);
        assert_eq!(header.profile_id, 11);
        assert_eq!(header.profile_mode, 3);
        assert_eq!(header.last_tick, 9999);
    }

    #[test]
    fn finalize_publish_profile_promotes_replace_mode_and_marks_block_mask() {
        let mapping_source_mask = [0u8, 1, 0, 1];
        let mut block_mask = [0u8; 4];
        let reported = finalize_publish_profile(2, true, &mapping_source_mask, &mut block_mask);
        assert_eq!(reported, PROFILE_MODE_MAPPING);
        assert_eq!(block_mask, [0, 1, 0, 1]);
    }

    #[test]
    fn finalize_input_mask_merges_mapping_sources() {
        let mapping_source_mask = [0u8, 1, 0, 1];
        let mut input_mask = [1u8, 0, 0, 0];
        finalize_input_mask(2, true, &mapping_source_mask, &mut input_mask);
        assert_eq!(input_mask, [1, 1, 0, 1]);
    }

    #[test]
    fn physical_alignment_plan_releases_when_not_physically_down() {
        let input_mask = [1u8, 1, 0, 1];
        let physical_down = [0u8, 1, 1, 0];
        let mut apply_mask = [0u8; 4];
        let mut desired_down = [0u8; 4];

        build_physical_alignment_plan(
            false,
            true,
            &input_mask,
            &physical_down,
            &mut apply_mask,
            &mut desired_down,
        );

        assert_eq!(apply_mask, [1, 1, 0, 1]);
        assert_eq!(desired_down, [0, 1, 0, 0]);
    }

    #[test]
    fn heartbeat_plan_skips_work_when_shared_memory_is_not_ready() {
        let not_ready = build_heartbeat_plan(false);
        let ready = build_heartbeat_plan(true);

        assert!(!not_ready.should_align_physical_input);
        assert!(!not_ready.should_publish_snapshot);
        assert!(ready.should_align_physical_input);
        assert!(ready.should_publish_snapshot);
    }
}
