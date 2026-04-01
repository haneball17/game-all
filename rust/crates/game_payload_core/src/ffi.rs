use crate::{
    runtime::{KeyDecision, RuntimeDecision, evaluate_key_state_header, evaluate_runtime_header, evaluate_runtime_state},
    sync::{DirectionConvergenceState, DirectionReleasePolicy, SnapshotCachePolicy},
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

#[unsafe(no_mangle)]
/// 基于共享快照、当前进程和心跳窗口计算最小运行时状态决策。
///
/// # Safety
/// - `snapshot_ptr` 必须指向可读的 `SharedKeyboardStateV2` 结构；
/// - `out_decision` 必须指向可写的 `PayloadRuntimeDecisionInterop`；
/// - 调用期间上述指针必须保持有效。
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
    // SAFETY: 调用方保证 snapshot_ptr/out_decision 有效且分别可读/可写。
    let snapshot = unsafe { &*snapshot_ptr };
    let decision = evaluate_runtime_state(
        snapshot,
        mapping_size,
        current_pid,
        now_tick,
        heartbeat_timeout_ms,
    );
    // SAFETY: 调用方保证 out_decision 指向可写内存。
    unsafe {
        out_decision.write(decision.into());
    }
    1
}

#[unsafe(no_mangle)]
/// 基于共享快照头字段、当前进程和心跳窗口计算最小运行时状态决策。
///
/// # Safety
/// - `out_decision` 必须指向可写的 `PayloadRuntimeDecisionInterop`；
/// - 调用期间该指针必须保持有效。
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
    // SAFETY: 调用方保证 out_decision 指向可写内存。
    unsafe {
        out_decision.write(decision.into());
    }
    1
}

#[unsafe(no_mangle)]
/// 基于共享快照头字段和单键状态计算执行端按键决策。
///
/// # Safety
/// - `out_decision` 必须指向可写的 `PayloadKeyDecisionInterop`；
/// - 调用期间该指针必须保持有效。
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
    // SAFETY: 调用方保证 out_decision 指向可写内存。
    unsafe {
        out_decision.write(decision.into());
    }
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
/// 释放由 Rust 创建的方向键收敛状态对象。
///
/// # Safety
/// - `state` 必须是 `payload_core_convergence_create` 返回的原始指针；
/// - 该指针只能释放一次，且释放后不能继续使用。
pub unsafe extern "C" fn payload_core_convergence_destroy(state: *mut DirectionConvergenceState) {
    if state.is_null() {
        return;
    }
    // SAFETY: 调用方保证该指针来自 Box::into_raw，且仅释放一次。
    unsafe {
        drop(Box::from_raw(state));
    }
}

#[unsafe(no_mangle)]
/// 向方向键收敛状态机提交按键事件。
///
/// # Safety
/// - `state` 必须指向由 `payload_core_convergence_create` 创建的有效对象。
pub unsafe extern "C" fn payload_core_convergence_on_key_event(
    state: *mut DirectionConvergenceState,
    vkey: u32,
    is_down: u32,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    // SAFETY: 调用方保证 state 有效可写。
    let state = unsafe { &mut *state };
    state.on_key_event(vkey as usize, is_down != 0);
    1
}

#[unsafe(no_mangle)]
/// 输出当前帧的强制抬起掩码。
///
/// # Safety
/// - `state` 必须指向有效状态对象；
/// - `out_mask` 必须指向至少 `out_len` 字节的可写缓冲区；
/// - `out_len` 必须不小于 256。
pub unsafe extern "C" fn payload_core_convergence_take_force_release_mask(
    state: *mut DirectionConvergenceState,
    out_mask: *mut u8,
    out_len: usize,
) -> u32 {
    if state.is_null() || out_mask.is_null() || out_len < SHARED_KEYBOARD_KEY_COUNT {
        return 0;
    }
    // SAFETY: 调用方保证 state 有效可写。
    let state = unsafe { &mut *state };
    let mask = state.take_force_release_mask();
    // SAFETY: 调用方保证 out_mask 至少可写 256 字节。
    let out = unsafe { std::slice::from_raw_parts_mut(out_mask, out_len) };
    out[..SHARED_KEYBOARD_KEY_COUNT].copy_from_slice(&mask);
    1
}

#[unsafe(no_mangle)]
/// 判定当前快照缓存是否必须刷新。
///
/// # Safety
/// - `state` 必须指向有效状态对象。
pub unsafe extern "C" fn payload_core_convergence_should_refresh(
    state: *const DirectionConvergenceState,
    age_ms: u64,
    max_age_ms: u64,
) -> u32 {
    if state.is_null() {
        return 0;
    }
    // SAFETY: 调用方保证 state 有效可读。
    let state = unsafe { &*state };
    let policy = SnapshotCachePolicy { max_age_ms };
    u32::from(matches!(
        policy.decide(age_ms, state),
        crate::sync::CacheDecision::Refresh
    ))
}
