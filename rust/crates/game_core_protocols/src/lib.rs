//! 协议镜像层。
//!
//! 说明：本 crate 只镜像 `Shared/Protocols/协议说明.md` 中的协议，
//! 不改变现有协议版本、共享内存名称和尺寸。

#![allow(non_snake_case)]

use core::mem::size_of;

pub const HELPER_STATUS_V5_VERSION: u32 = 5;
pub const HELPER_CONTROL_V4_VERSION: u32 = 4;
pub const SHARED_KEYBOARD_STATE_V2_VERSION: u32 = 2;

pub const HELPER_STATUS_MAPPING_GLOBAL_PREFIX: &str = "Global\\GameHelperStatus_";
pub const HELPER_STATUS_MAPPING_LOCAL_PREFIX: &str = "Local\\GameHelperStatus_";
pub const HELPER_CONTROL_MAPPING_GLOBAL_PREFIX: &str = "Global\\GameHelperControl_";
pub const HELPER_CONTROL_MAPPING_LOCAL_PREFIX: &str = "Local\\GameHelperControl_";
pub const SHARED_KEYBOARD_STATE_MAPPING_NAME: &str = "Local\\DNFSyncBox.KeyboardState.V2";

pub const SHARED_KEYBOARD_KEY_COUNT: usize = 256;
pub const SYNC_FLAG_PAUSED: u32 = 0x01;
pub const SYNC_FLAG_CLEAR: u32 = 0x02;

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct HelperStatusV5 {
    pub Version: u32,
    pub Size: u32,
    pub LastTickMs: u64,
    pub Pid: u32,
    pub ProcessAlive: i32,
    pub AutoTransparentEnabled: i32,
    pub FullscreenAttackTarget: i32,
    pub FullscreenAttackPatchOn: i32,
    pub AttractMode: i32,
    pub AttractPositive: i32,
    pub GatherItemsEnabled: i32,
    pub DamageEnabled: i32,
    pub DamageMultiplier: i32,
    pub InvincibleEnabled: i32,
    pub SummonEnabled: i32,
    pub SummonLastTick: u64,
    pub FullscreenSkillEnabled: i32,
    pub FullscreenSkillActive: i32,
    pub FullscreenSkillHotkey: u32,
    pub HotkeyEnabled: i32,
    pub PlayerName: [u16; 32],
}

impl Default for HelperStatusV5 {
    fn default() -> Self {
        Self {
            Version: HELPER_STATUS_V5_VERSION,
            Size: size_of::<Self>() as u32,
            LastTickMs: 0,
            Pid: 0,
            ProcessAlive: 0,
            AutoTransparentEnabled: 0,
            FullscreenAttackTarget: 0,
            FullscreenAttackPatchOn: 0,
            AttractMode: 0,
            AttractPositive: 0,
            GatherItemsEnabled: 0,
            DamageEnabled: 0,
            DamageMultiplier: 0,
            InvincibleEnabled: 0,
            SummonEnabled: 0,
            SummonLastTick: 0,
            FullscreenSkillEnabled: 0,
            FullscreenSkillActive: 0,
            FullscreenSkillHotkey: 0,
            HotkeyEnabled: 0,
            PlayerName: [0; 32],
        }
    }
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct HelperControlV4 {
    pub Version: u32,
    pub Size: u32,
    pub Pid: u32,
    pub LastUpdateTick: u32,
    pub FullscreenAttack: u8,
    pub FullscreenSkill: u8,
    pub AutoTransparent: u8,
    pub Attract: u8,
    pub HotkeyEnabled: u8,
    pub Reserved0: u8,
    pub Reserved1: u8,
    pub Reserved2: u8,
    pub SummonSequence: u32,
    pub ActionSequence: u32,
    pub ActionMask: u32,
    pub DesiredFullscreenAttack: u8,
    pub DesiredFullscreenSkill: u8,
    pub DesiredAutoTransparent: u8,
    pub DesiredAttractEnabled: u8,
    pub DesiredAttractMode: u8,
    pub DesiredAttractPositive: u8,
    pub DesiredHotkeyEnabled: u8,
    pub DesiredGatherItemsEnabled: u8,
    pub DesiredDamageMultiplier: u32,
    pub DesiredDamageEnabled: u8,
    pub DesiredInvincibleEnabled: u8,
    pub Reserved3: u8,
    pub Reserved4: u8,
    pub Reserved5: u8,
    pub Reserved6: u8,
    pub Reserved7: u8,
    pub Reserved8: u8,
}

impl Default for HelperControlV4 {
    fn default() -> Self {
        Self {
            Version: HELPER_CONTROL_V4_VERSION,
            Size: size_of::<Self>() as u32,
            Pid: 0,
            LastUpdateTick: 0,
            FullscreenAttack: 0,
            FullscreenSkill: 0,
            AutoTransparent: 0,
            Attract: 0,
            HotkeyEnabled: 0,
            Reserved0: 0,
            Reserved1: 0,
            Reserved2: 0,
            SummonSequence: 0,
            ActionSequence: 0,
            ActionMask: 0,
            DesiredFullscreenAttack: 0,
            DesiredFullscreenSkill: 0,
            DesiredAutoTransparent: 0,
            DesiredAttractEnabled: 0,
            DesiredAttractMode: 0,
            DesiredAttractPositive: 0,
            DesiredHotkeyEnabled: 0,
            DesiredGatherItemsEnabled: 0,
            DesiredDamageMultiplier: 0,
            DesiredDamageEnabled: 0,
            DesiredInvincibleEnabled: 0,
            Reserved3: 0,
            Reserved4: 0,
            Reserved5: 0,
            Reserved6: 0,
            Reserved7: 0,
            Reserved8: 0,
        }
    }
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct SharedKeyboardStateV2 {
    pub Version: u32,
    pub Seq: u32,
    pub Flags: u32,
    pub ActivePid: u32,
    pub ProfileId: u32,
    pub ProfileMode: u32,
    pub LastTick: u64,
    pub KeyboardState: [u8; SHARED_KEYBOARD_KEY_COUNT],
    pub EdgeCounter: [u32; SHARED_KEYBOARD_KEY_COUNT],
    pub TargetMask: [u8; SHARED_KEYBOARD_KEY_COUNT],
    pub BlockMask: [u8; SHARED_KEYBOARD_KEY_COUNT],
}

impl Default for SharedKeyboardStateV2 {
    fn default() -> Self {
        Self {
            Version: SHARED_KEYBOARD_STATE_V2_VERSION,
            Seq: 0,
            Flags: 0,
            ActivePid: 0,
            ProfileId: 0,
            ProfileMode: 0,
            LastTick: 0,
            KeyboardState: [0; SHARED_KEYBOARD_KEY_COUNT],
            EdgeCounter: [0; SHARED_KEYBOARD_KEY_COUNT],
            TargetMask: [0; SHARED_KEYBOARD_KEY_COUNT],
            BlockMask: [0; SHARED_KEYBOARD_KEY_COUNT],
        }
    }
}

pub const HELPER_STATUS_V5_SIZE: u32 = size_of::<HelperStatusV5>() as u32;
pub const HELPER_CONTROL_V4_SIZE: u32 = size_of::<HelperControlV4>() as u32;
pub const SHARED_KEYBOARD_STATE_V2_SIZE: u32 = size_of::<SharedKeyboardStateV2>() as u32;

pub fn helper_status_mapping_name(pid: u32, use_global: bool) -> String {
    if use_global {
        format!("{HELPER_STATUS_MAPPING_GLOBAL_PREFIX}{pid}")
    } else {
        format!("{HELPER_STATUS_MAPPING_LOCAL_PREFIX}{pid}")
    }
}

pub fn helper_control_mapping_name(pid: u32, use_global: bool) -> String {
    if use_global {
        format!("{HELPER_CONTROL_MAPPING_GLOBAL_PREFIX}{pid}")
    } else {
        format!("{HELPER_CONTROL_MAPPING_LOCAL_PREFIX}{pid}")
    }
}

pub fn is_valid_helper_status(snapshot: &HelperStatusV5) -> bool {
    snapshot.Version == HELPER_STATUS_V5_VERSION && snapshot.Size == HELPER_STATUS_V5_SIZE
}

pub fn is_valid_helper_control(snapshot: &HelperControlV4) -> bool {
    snapshot.Version == HELPER_CONTROL_V4_VERSION && snapshot.Size == HELPER_CONTROL_V4_SIZE
}

pub fn is_valid_shared_keyboard_state(
    snapshot: &SharedKeyboardStateV2,
    mapping_size: usize,
) -> bool {
    snapshot.Version == SHARED_KEYBOARD_STATE_V2_VERSION
        && mapping_size >= SHARED_KEYBOARD_STATE_V2_SIZE as usize
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn helper_status_layout_matches_protocol() {
        assert_eq!(HELPER_STATUS_V5_SIZE, 152);
        assert_eq!(core::mem::offset_of!(HelperStatusV5, Version), 0);
        assert_eq!(core::mem::offset_of!(HelperStatusV5, Size), 4);
        assert_eq!(core::mem::offset_of!(HelperStatusV5, LastTickMs), 8);
        assert_eq!(core::mem::offset_of!(HelperStatusV5, Pid), 16);
        assert_eq!(core::mem::offset_of!(HelperStatusV5, SummonLastTick), 64);
        assert_eq!(core::mem::offset_of!(HelperStatusV5, PlayerName), 88);
    }

    #[test]
    fn helper_control_layout_matches_protocol() {
        assert_eq!(HELPER_CONTROL_V4_SIZE, 56);
        assert_eq!(core::mem::offset_of!(HelperControlV4, Version), 0);
        assert_eq!(core::mem::offset_of!(HelperControlV4, LastUpdateTick), 12);
        assert_eq!(core::mem::offset_of!(HelperControlV4, SummonSequence), 24);
        assert_eq!(
            core::mem::offset_of!(HelperControlV4, DesiredDamageMultiplier),
            44
        );
        assert_eq!(core::mem::offset_of!(HelperControlV4, Reserved8), 55);
    }

    #[test]
    fn shared_keyboard_layout_matches_protocol() {
        assert_eq!(SHARED_KEYBOARD_STATE_V2_SIZE, 1824);
        assert_eq!(core::mem::offset_of!(SharedKeyboardStateV2, Version), 0);
        assert_eq!(core::mem::offset_of!(SharedKeyboardStateV2, Seq), 4);
        assert_eq!(core::mem::offset_of!(SharedKeyboardStateV2, LastTick), 24);
        assert_eq!(
            core::mem::offset_of!(SharedKeyboardStateV2, KeyboardState),
            32
        );
        assert_eq!(
            core::mem::offset_of!(SharedKeyboardStateV2, EdgeCounter),
            288
        );
        assert_eq!(
            core::mem::offset_of!(SharedKeyboardStateV2, TargetMask),
            1312
        );
        assert_eq!(
            core::mem::offset_of!(SharedKeyboardStateV2, BlockMask),
            1568
        );
    }

    #[test]
    fn mapping_names_match_existing_scheme() {
        assert_eq!(
            helper_status_mapping_name(123, true),
            "Global\\GameHelperStatus_123"
        );
        assert_eq!(
            helper_status_mapping_name(123, false),
            "Local\\GameHelperStatus_123"
        );
        assert_eq!(
            helper_control_mapping_name(456, true),
            "Global\\GameHelperControl_456"
        );
        assert_eq!(
            SHARED_KEYBOARD_STATE_MAPPING_NAME,
            "Local\\DNFSyncBox.KeyboardState.V2"
        );
    }
}
