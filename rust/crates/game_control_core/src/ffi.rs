#![allow(clippy::missing_safety_doc, clippy::undocumented_unsafe_blocks)]

use crate::{
    ForegroundDecision, ForegroundTracker, PublishHeader, PublishHeaderInput, WindowSnapshotInput,
    build_publish_header, evaluate_foreground_state,
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
