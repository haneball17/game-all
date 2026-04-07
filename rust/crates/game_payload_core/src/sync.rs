use game_core_protocols::SHARED_KEYBOARD_KEY_COUNT;

pub const DIRECTION_KEYS: [usize; 4] = [0x25, 0x26, 0x27, 0x28];

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProjectedChannelKind {
    Raw = 1,
    Win32 = 2,
    DirectInput = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DirectionReleasePolicy {
    pub extra_release_pulses: u8,
}

impl Default for DirectionReleasePolicy {
    fn default() -> Self {
        Self {
            extra_release_pulses: 2,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DirectionConvergenceState {
    pressed: [bool; SHARED_KEYBOARD_KEY_COUNT],
    release_pulses_remaining: [u8; SHARED_KEYBOARD_KEY_COUNT],
    policy: DirectionReleasePolicy,
}

impl DirectionConvergenceState {
    pub fn new(policy: DirectionReleasePolicy) -> Self {
        Self {
            pressed: [false; SHARED_KEYBOARD_KEY_COUNT],
            release_pulses_remaining: [0; SHARED_KEYBOARD_KEY_COUNT],
            policy,
        }
    }

    /// 注册物理按键事件。
    ///
    /// 对方向键而言，释放时会进入额外抬起脉冲窗口，
    /// 用于在执行端多输入路径不完全一致时做收敛。
    pub fn on_key_event(&mut self, vkey: usize, is_down: bool) {
        if vkey >= SHARED_KEYBOARD_KEY_COUNT {
            return;
        }

        self.pressed[vkey] = is_down;
        if is_down {
            self.release_pulses_remaining[vkey] = 0;
        } else if DIRECTION_KEYS.contains(&vkey) {
            self.release_pulses_remaining[vkey] = self.policy.extra_release_pulses;
        }
    }

    pub fn is_pressed(&self, vkey: usize) -> bool {
        self.pressed.get(vkey).copied().unwrap_or(false)
    }

    pub fn release_pulses_remaining(&self, vkey: usize) -> u8 {
        self.release_pulses_remaining
            .get(vkey)
            .copied()
            .unwrap_or(0)
    }

    /// 生成当前帧的“强制抬起掩码”，并消费一次脉冲。
    pub fn take_force_release_mask(&mut self) -> [u8; SHARED_KEYBOARD_KEY_COUNT] {
        let mut mask = [0u8; SHARED_KEYBOARD_KEY_COUNT];
        for &vkey in &DIRECTION_KEYS {
            if self.release_pulses_remaining[vkey] > 0 {
                mask[vkey] = 1;
                self.release_pulses_remaining[vkey] -= 1;
            }
        }
        mask
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SnapshotCachePolicy {
    pub max_age_ms: u64,
}

impl Default for SnapshotCachePolicy {
    fn default() -> Self {
        Self { max_age_ms: 8 }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CacheDecision {
    Reuse,
    Refresh,
}

impl SnapshotCachePolicy {
    /// 规则固定：
    /// - 缓存超时则刷新
    /// - 任意方向键仍在释放收敛窗口时强制刷新
    pub fn decide(&self, age_ms: u64, convergence: &DirectionConvergenceState) -> CacheDecision {
        if age_ms > self.max_age_ms {
            return CacheDecision::Refresh;
        }

        if DIRECTION_KEYS
            .iter()
            .any(|&vkey| convergence.release_pulses_remaining(vkey) > 0)
        {
            return CacheDecision::Refresh;
        }

        CacheDecision::Reuse
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SyncStateStore {
    logical_desired: [u8; SHARED_KEYBOARD_KEY_COUNT],
    raw_projected: [u8; SHARED_KEYBOARD_KEY_COUNT],
    win32_projected: [u8; SHARED_KEYBOARD_KEY_COUNT],
    direct_input_projected: [u8; SHARED_KEYBOARD_KEY_COUNT],
}

impl Default for SyncStateStore {
    fn default() -> Self {
        Self {
            logical_desired: [0; SHARED_KEYBOARD_KEY_COUNT],
            raw_projected: [0; SHARED_KEYBOARD_KEY_COUNT],
            win32_projected: [0; SHARED_KEYBOARD_KEY_COUNT],
            direct_input_projected: [0; SHARED_KEYBOARD_KEY_COUNT],
        }
    }
}

impl SyncStateStore {
    pub fn set_logical_desired(&mut self, vkey: usize, down: bool) {
        if vkey < SHARED_KEYBOARD_KEY_COUNT {
            self.logical_desired[vkey] = if down { 1 } else { 0 };
        }
    }

    pub fn logical_desired(&self, vkey: usize) -> bool {
        self.logical_desired.get(vkey).copied().unwrap_or(0) != 0
    }

    pub fn set_projected(&mut self, channel: ProjectedChannelKind, vkey: usize, down: bool) {
        if vkey >= SHARED_KEYBOARD_KEY_COUNT {
            return;
        }
        let value = if down { 1 } else { 0 };
        match channel {
            ProjectedChannelKind::Raw => self.raw_projected[vkey] = value,
            ProjectedChannelKind::Win32 => self.win32_projected[vkey] = value,
            ProjectedChannelKind::DirectInput => self.direct_input_projected[vkey] = value,
        }
    }

    pub fn projected(&self, channel: ProjectedChannelKind, vkey: usize) -> bool {
        if vkey >= SHARED_KEYBOARD_KEY_COUNT {
            return false;
        }
        match channel {
            ProjectedChannelKind::Raw => self.raw_projected[vkey] != 0,
            ProjectedChannelKind::Win32 => self.win32_projected[vkey] != 0,
            ProjectedChannelKind::DirectInput => self.direct_input_projected[vkey] != 0,
        }
    }

    pub fn clear_logical_desired(&mut self) {
        self.logical_desired.fill(0);
    }

    pub fn clear_all_projected(&mut self) {
        self.raw_projected.fill(0);
        self.win32_projected.fill(0);
        self.direct_input_projected.fill(0);
    }

    pub fn clear_projected_channel(&mut self, channel: ProjectedChannelKind) {
        match channel {
            ProjectedChannelKind::Raw => self.raw_projected.fill(0),
            ProjectedChannelKind::Win32 => self.win32_projected.fill(0),
            ProjectedChannelKind::DirectInput => self.direct_input_projected.fill(0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn direction_release_generates_extra_release_pulses() {
        let mut state = DirectionConvergenceState::new(DirectionReleasePolicy {
            extra_release_pulses: 2,
        });
        state.on_key_event(0x25, true);
        state.on_key_event(0x25, false);

        let first = state.take_force_release_mask();
        let second = state.take_force_release_mask();
        let third = state.take_force_release_mask();

        assert_eq!(first[0x25], 1);
        assert_eq!(second[0x25], 1);
        assert_eq!(third[0x25], 0);
    }

    #[test]
    fn non_direction_key_does_not_enter_release_window() {
        let mut state = DirectionConvergenceState::new(DirectionReleasePolicy::default());
        state.on_key_event(0x41, true);
        state.on_key_event(0x41, false);
        assert_eq!(state.take_force_release_mask()[0x41], 0);
    }

    #[test]
    fn cache_policy_refreshes_during_release_window() {
        let mut state = DirectionConvergenceState::new(DirectionReleasePolicy::default());
        state.on_key_event(0x27, false);
        let policy = SnapshotCachePolicy { max_age_ms: 8 };
        assert_eq!(policy.decide(1, &state), CacheDecision::Refresh);
        let _ = state.take_force_release_mask();
        let _ = state.take_force_release_mask();
        assert_eq!(policy.decide(1, &state), CacheDecision::Reuse);
        assert_eq!(policy.decide(20, &state), CacheDecision::Refresh);
    }

    #[test]
    fn sync_state_store_tracks_logical_and_projected_states() {
        let mut store = SyncStateStore::default();
        store.set_logical_desired(0x25, true);
        store.set_projected(ProjectedChannelKind::Raw, 0x25, true);
        store.set_projected(ProjectedChannelKind::Win32, 0x25, false);

        assert!(store.logical_desired(0x25));
        assert!(store.projected(ProjectedChannelKind::Raw, 0x25));
        assert!(!store.projected(ProjectedChannelKind::Win32, 0x25));
    }

    #[test]
    fn sync_state_store_clears_channels_independently() {
        let mut store = SyncStateStore::default();
        store.set_projected(ProjectedChannelKind::Raw, 0x25, true);
        store.set_projected(ProjectedChannelKind::Win32, 0x25, true);
        store.set_projected(ProjectedChannelKind::DirectInput, 0x25, true);
        store.clear_projected_channel(ProjectedChannelKind::Raw);

        assert!(!store.projected(ProjectedChannelKind::Raw, 0x25));
        assert!(store.projected(ProjectedChannelKind::Win32, 0x25));
        assert!(store.projected(ProjectedChannelKind::DirectInput, 0x25));

        store.clear_all_projected();
        assert!(!store.projected(ProjectedChannelKind::Win32, 0x25));
        assert!(!store.projected(ProjectedChannelKind::DirectInput, 0x25));
    }
}
