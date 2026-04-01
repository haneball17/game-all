use std::collections::VecDeque;

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
}
