use std::collections::VecDeque;

use crate::runtime::{AdapterDriftSummary, InputChannelKind, InputPathObservation};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiagnosticChannel {
    Control = 1,
    Win32 = 2,
    RawInput = 3,
    DirectInput = 4,
    Focus = 5,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DiagnosticEvent {
    pub tick_ms: u64,
    pub channel: DiagnosticChannel,
    pub vkey: u32,
    pub is_down: bool,
    pub cache_age_ms: u64,
    pub forced_release: bool,
}

#[derive(Debug, Clone)]
pub struct DiagnosticBuffer {
    capacity: usize,
    events: VecDeque<DiagnosticEvent>,
}

impl DiagnosticBuffer {
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity,
            events: VecDeque::with_capacity(capacity),
        }
    }

    pub fn push(&mut self, event: DiagnosticEvent) {
        if self.capacity == 0 {
            return;
        }
        while self.events.len() >= self.capacity {
            self.events.pop_front();
        }
        self.events.push_back(event);
    }

    pub fn len(&self) -> usize {
        self.events.len()
    }

    pub fn is_empty(&self) -> bool {
        self.events.is_empty()
    }

    pub fn latest(&self) -> Option<DiagnosticEvent> {
        self.events.back().copied()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SyncObservationSnapshot {
    pub channel: InputChannelKind,
    pub active_pid: u32,
    pub is_alive: bool,
    pub is_paused: bool,
    pub raw_promoted: bool,
    pub raw_active: bool,
    pub direct_input_active: bool,
    pub win32_active: bool,
    pub mixed_inputs: bool,
    pub raw_drift_count: u32,
    pub win32_drift_count: u32,
    pub direct_input_drift_count: u32,
    pub profile_id: u32,
    pub profile_mode: u32,
}

pub fn build_sync_observation_snapshot(
    active_pid: u32,
    is_alive: bool,
    is_paused: bool,
    observation: InputPathObservation,
    drift: AdapterDriftSummary,
) -> SyncObservationSnapshot {
    SyncObservationSnapshot {
        channel: observation.channel,
        active_pid,
        is_alive,
        is_paused,
        raw_promoted: observation.raw_promoted,
        raw_active: observation.raw_active,
        direct_input_active: observation.direct_input_active,
        win32_active: observation.win32_active,
        mixed_inputs: observation.mixed_inputs,
        raw_drift_count: drift.raw_drift_count,
        win32_drift_count: drift.win32_drift_count,
        direct_input_drift_count: drift.direct_input_drift_count,
        profile_id: observation.profile_id,
        profile_mode: observation.profile_mode,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn diagnostic_buffer_keeps_latest_events() {
        let mut buffer = DiagnosticBuffer::new(2);
        buffer.push(DiagnosticEvent {
            tick_ms: 1,
            channel: DiagnosticChannel::Control,
            vkey: 0x25,
            is_down: true,
            cache_age_ms: 0,
            forced_release: false,
        });
        buffer.push(DiagnosticEvent {
            tick_ms: 2,
            channel: DiagnosticChannel::RawInput,
            vkey: 0x25,
            is_down: false,
            cache_age_ms: 4,
            forced_release: true,
        });
        buffer.push(DiagnosticEvent {
            tick_ms: 3,
            channel: DiagnosticChannel::DirectInput,
            vkey: 0x27,
            is_down: false,
            cache_age_ms: 2,
            forced_release: true,
        });

        assert_eq!(buffer.len(), 2);
        let latest = buffer.latest().expect("latest");
        assert_eq!(latest.tick_ms, 3);
        assert_eq!(latest.channel, DiagnosticChannel::DirectInput);
    }

    #[test]
    fn sync_observation_snapshot_combines_observation_and_drift() {
        let snapshot = build_sync_observation_snapshot(
            123,
            true,
            false,
            InputPathObservation {
                channel: InputChannelKind::RawInput,
                raw_promoted: true,
                raw_active: true,
                direct_input_active: false,
                win32_active: true,
                mixed_inputs: true,
                profile_id: 7,
                profile_mode: 3,
            },
            AdapterDriftSummary {
                raw_drift_count: 2,
                win32_drift_count: 1,
                direct_input_drift_count: 0,
            },
        );

        assert_eq!(snapshot.channel, InputChannelKind::RawInput);
        assert_eq!(snapshot.active_pid, 123);
        assert!(snapshot.is_alive);
        assert!(!snapshot.is_paused);
        assert!(snapshot.mixed_inputs);
        assert_eq!(snapshot.raw_drift_count, 2);
        assert_eq!(snapshot.profile_mode, 3);
    }
}
