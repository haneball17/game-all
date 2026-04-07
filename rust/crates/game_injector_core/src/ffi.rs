use crate::{
    InjectorConfig, InjectorConfigInterop, InjectorConfigView,
    runtime::{
        AttemptOutcomeSummary, BackendExecutionResult, HeartbeatObservationResult,
        HelperHeartbeatDecision, InjectionRetryDecision, InjectionRetryRuntime, InjectorWatchRuntime,
        SuccessObservationResult, WindowProbeResult, build_runtime_plan_from_text,
        evaluate_helper_heartbeat, finish_attempt_with_observation, summarize_attempt_outcome,
    },
};
use game_core_protocols::{HELPER_STATUS_V5_SIZE, HELPER_STATUS_V5_VERSION};
use std::sync::OnceLock;

static DEFAULT_INI_TEXT_UTF8: OnceLock<Box<[u8]>> = OnceLock::new();

fn default_ini_text_utf8() -> &'static [u8] {
    DEFAULT_INI_TEXT_UTF8.get_or_init(|| {
        InjectorConfig::default_ini_text()
            .into_bytes()
            .into_boxed_slice()
    })
}

/// 第一阶段仅暴露稳定、低风险的 C ABI：
/// - 默认配置数值视图
/// - Helper 协议版本/尺寸
///
/// 后续再扩展到真正的配置文件读取与执行流程。
#[unsafe(no_mangle)]
pub extern "C" fn injector_core_default_view() -> InjectorConfigView {
    InjectorConfigView::from(&InjectorConfig::default())
}

#[unsafe(no_mangle)]
/// 解析并按 Windows 规则归一化 UTF-8 编码的 injector 配置文本。
///
/// # Safety
/// - `text_ptr` 必须指向长度为 `text_len` 的可读 UTF-8 字节缓冲区；
/// - `base_dir_ptr` 必须指向长度为 `base_dir_len` 的可读 UTF-8 基础目录缓冲区；
/// - `out_config` 必须指向可写的 `InjectorConfigInterop` 存储；
/// - 调用方必须保证这些指针在本函数返回前始终有效。
pub unsafe extern "C" fn injector_core_load_config_utf8(
    text_ptr: *const u8,
    text_len: usize,
    base_dir_ptr: *const u8,
    base_dir_len: usize,
    out_config: *mut InjectorConfigInterop,
) -> u32 {
    if text_ptr.is_null() || base_dir_ptr.is_null() || out_config.is_null() {
        return 0;
    }

    // SAFETY: 调用方承诺 `text_ptr` 指向长度为 `text_len` 的只读 UTF-8 缓冲区，
    // 这里仅在函数作用域内按字节切片读取，不持久化借用。
    let text = unsafe { std::slice::from_raw_parts(text_ptr, text_len) };
    // SAFETY: 调用方承诺 `base_dir_ptr` 指向长度为 `base_dir_len` 的只读 UTF-8 缓冲区，
    // 这里仅做瞬时切片读取。
    let base_dir = unsafe { std::slice::from_raw_parts(base_dir_ptr, base_dir_len) };
    let text = String::from_utf8_lossy(text);
    let base_dir = String::from_utf8_lossy(base_dir);

    let Some((_, interop)) = build_runtime_plan_from_text(&text, &base_dir) else {
        return 0;
    };
    // SAFETY: 调用方承诺 `out_config` 指向可写的 `InjectorConfigInterop`，
    // 这里执行一次按值写入。
    unsafe {
        out_config.write(interop);
    }
    1
}

#[unsafe(no_mangle)]
/// 解析 UTF-8 编码的 injector.ini 文本，输出第一阶段稳定的互操作配置视图。
///
/// # Safety
/// - `text_ptr` 必须指向长度为 `text_len` 的可读 UTF-8（或 UTF-8 兼容）字节缓冲区；
/// - `out_config` 必须指向一块可写的 `InjectorConfigInterop` 内存；
/// - 调用方必须保证这两个指针在本函数返回前始终有效。
pub unsafe extern "C" fn injector_core_parse_ini_text_utf8(
    text_ptr: *const u8,
    text_len: usize,
    out_config: *mut InjectorConfigInterop,
) -> u32 {
    if text_ptr.is_null() || out_config.is_null() {
        return 0;
    }

    // SAFETY: 调用方承诺 `text_ptr` 指向长度为 `text_len` 的只读缓冲区；
    // 这里仅在函数作用域内按字节切片读取，不越界、不持久化借用。
    let text = unsafe { std::slice::from_raw_parts(text_ptr, text_len) };
    let text = String::from_utf8_lossy(text);
    let config = InjectorConfig::parse_ini(&text);
    let Some(interop) = InjectorConfigInterop::from_config(&config) else {
        return 0;
    };
    // SAFETY: 调用方承诺 `out_config` 指向可写的 `InjectorConfigInterop` 存储；
    // 这里执行一次按值写入，不会重复释放或越界访问。
    unsafe {
        out_config.write(interop);
    }
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_default_ini_text_utf8() -> *const u8 {
    default_ini_text_utf8().as_ptr()
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_default_ini_text_utf8_len() -> usize {
    default_ini_text_utf8().len()
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_helper_status_version() -> u32 {
    HELPER_STATUS_V5_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_helper_status_size() -> u32 {
    HELPER_STATUS_V5_SIZE
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorHelperHeartbeatDecisionInterop {
    pub contract_ok: u32,
    pub heartbeat_ok: u32,
}

impl From<HelperHeartbeatDecision> for InjectorHelperHeartbeatDecisionInterop {
    fn from(value: HelperHeartbeatDecision) -> Self {
        Self {
            contract_ok: u32::from(value.contract_ok),
            heartbeat_ok: u32::from(value.heartbeat_ok),
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorRetryDecisionInterop {
    pub attempt: u32,
    pub backend_started: u32,
    pub success_by_file: u32,
    pub success_by_heartbeat: u32,
    pub success_source: u32,
    pub used_heartbeat_fallback: u32,
    pub succeeded: u32,
    pub should_retry: u32,
    pub retry_delay_ms: u32,
    pub finished: u32,
}

impl From<InjectionRetryDecision> for InjectorRetryDecisionInterop {
    fn from(value: InjectionRetryDecision) -> Self {
        Self {
            attempt: value.attempt,
            backend_started: u32::from(value.backend_started),
            success_by_file: u32::from(value.success_by_file),
            success_by_heartbeat: u32::from(value.success_by_heartbeat),
            success_source: value.success_source,
            used_heartbeat_fallback: u32::from(value.used_heartbeat_fallback),
            succeeded: u32::from(value.succeeded),
            should_retry: u32::from(value.should_retry),
            retry_delay_ms: value.retry_delay_ms,
            finished: u32::from(value.finished),
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorWindowProbeResultInterop {
    pub ok: u32,
    pub timed_out: u32,
    pub used_hint: u32,
    pub error_code: u32,
}

impl From<WindowProbeResult> for InjectorWindowProbeResultInterop {
    fn from(value: WindowProbeResult) -> Self {
        Self {
            ok: u32::from(value.ok),
            timed_out: u32::from(value.timed_out),
            used_hint: u32::from(value.used_hint),
            error_code: value.error_code,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorSuccessObservationResultInterop {
    pub observed: u32,
    pub timed_out: u32,
    pub used_fallback: u32,
    pub error_code: u32,
}

impl From<SuccessObservationResult> for InjectorSuccessObservationResultInterop {
    fn from(value: SuccessObservationResult) -> Self {
        Self {
            observed: u32::from(value.observed),
            timed_out: u32::from(value.timed_out),
            used_fallback: u32::from(value.used_fallback),
            error_code: value.error_code,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorHeartbeatObservationResultInterop {
    pub observed: u32,
    pub contract_ok: u32,
    pub mapping_found: u32,
    pub timed_out: u32,
    pub error_code: u32,
}

impl From<HeartbeatObservationResult> for InjectorHeartbeatObservationResultInterop {
    fn from(value: HeartbeatObservationResult) -> Self {
        Self {
            observed: u32::from(value.observed),
            contract_ok: u32::from(value.contract_ok),
            mapping_found: u32::from(value.mapping_found),
            timed_out: u32::from(value.timed_out),
            error_code: value.error_code,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorBackendExecutionResultInterop {
    pub started: u32,
    pub configured_backend: u32,
    pub effective_backend: u32,
    pub downgraded: u32,
    pub error_code: u32,
}

impl From<BackendExecutionResult> for InjectorBackendExecutionResultInterop {
    fn from(value: BackendExecutionResult) -> Self {
        Self {
            started: u32::from(value.started),
            configured_backend: value.configured_backend,
            effective_backend: value.effective_backend,
            downgraded: u32::from(value.downgraded),
            error_code: value.error_code,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorAttemptOutcomeSummaryInterop {
    pub succeeded: u32,
    pub should_retry: u32,
    pub outcome_code: u32,
}

impl From<AttemptOutcomeSummary> for InjectorAttemptOutcomeSummaryInterop {
    fn from(value: AttemptOutcomeSummary) -> Self {
        Self {
            succeeded: u32::from(value.succeeded),
            should_retry: u32::from(value.should_retry),
            outcome_code: value.outcome_code as u32,
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_evaluate_helper_heartbeat(
    status_version: u32,
    status_size: u32,
    process_alive: u32,
    last_tick: u64,
    now_tick: u64,
    timeout_ms: u64,
) -> InjectorHelperHeartbeatDecisionInterop {
    evaluate_helper_heartbeat(
        status_version,
        status_size,
        process_alive != 0,
        last_tick,
        now_tick,
        timeout_ms,
    )
    .into()
}

#[unsafe(no_mangle)]
/// 根据结构化平台观测结果推进一次注入尝试。
///
/// # Safety
/// - `runtime` 必须是有效的 retry runtime 指针；
/// - `backend`、`success`、`heartbeat`、`out_decision` 必须指向可读/可写的对应结构体。
pub unsafe extern "C" fn injector_core_retry_runtime_finish_attempt_with_results(
    runtime: *mut InjectionRetryRuntime,
    backend: *const InjectorBackendExecutionResultInterop,
    success: *const InjectorSuccessObservationResultInterop,
    heartbeat: *const InjectorHeartbeatObservationResultInterop,
    out_decision: *mut InjectorRetryDecisionInterop,
) -> u32 {
    if runtime.is_null()
        || backend.is_null()
        || success.is_null()
        || heartbeat.is_null()
        || out_decision.is_null()
    {
        return 0;
    }

    // SAFETY: 调用方承诺这些指针都有效，函数作用域内只做只读/一次写入。
    let backend = unsafe { &*backend };
    // SAFETY: 调用方承诺 `success` 指向有效只读 success 观测结果。
    let success = unsafe { &*success };
    // SAFETY: 调用方承诺 `heartbeat` 指向有效只读 heartbeat 观测结果。
    let heartbeat = unsafe { &*heartbeat };
    // SAFETY: 调用方承诺 `runtime` 有效。
    let decision = unsafe {
        finish_attempt_with_observation(
            &mut *runtime,
            BackendExecutionResult {
                started: backend.started != 0,
                configured_backend: backend.configured_backend,
                effective_backend: backend.effective_backend,
                downgraded: backend.downgraded != 0,
                error_code: backend.error_code,
            },
            SuccessObservationResult {
                observed: success.observed != 0,
                timed_out: success.timed_out != 0,
                used_fallback: success.used_fallback != 0,
                error_code: success.error_code,
            },
            HeartbeatObservationResult {
                observed: heartbeat.observed != 0,
                contract_ok: heartbeat.contract_ok != 0,
                mapping_found: heartbeat.mapping_found != 0,
                timed_out: heartbeat.timed_out != 0,
                error_code: heartbeat.error_code,
            },
        )
    };
    // SAFETY: 调用方承诺 `out_decision` 可写。
    unsafe {
        out_decision.write(decision.into());
    }
    1
}

#[unsafe(no_mangle)]
/// 汇总一次注入尝试的最终结果来源与失败原因。
///
/// # Safety
/// - `backend`、`success`、`heartbeat`、`retry` 必须指向有效只读结构体；
/// - `out_summary` 必须指向可写的 `InjectorAttemptOutcomeSummaryInterop`；
/// - 所有指针在本函数返回前必须保持有效。
pub unsafe extern "C" fn injector_core_summarize_attempt_outcome(
    backend: *const InjectorBackendExecutionResultInterop,
    success: *const InjectorSuccessObservationResultInterop,
    heartbeat: *const InjectorHeartbeatObservationResultInterop,
    retry: *const InjectorRetryDecisionInterop,
    out_summary: *mut InjectorAttemptOutcomeSummaryInterop,
) -> u32 {
    if backend.is_null()
        || success.is_null()
        || heartbeat.is_null()
        || retry.is_null()
        || out_summary.is_null()
    {
        return 0;
    }

    // SAFETY: 调用方承诺 `backend` 指向有效只读后端执行结果。
    let backend = unsafe { &*backend };
    // SAFETY: 调用方承诺 `success` 指向有效只读成功文件观测结果。
    let success = unsafe { &*success };
    // SAFETY: 调用方承诺 `heartbeat` 指向有效只读 heartbeat 观测结果。
    let heartbeat = unsafe { &*heartbeat };
    // SAFETY: 调用方承诺 `retry` 指向有效只读 retry 决策结构。
    let retry = unsafe { &*retry };
    let summary = summarize_attempt_outcome(
        BackendExecutionResult {
            started: backend.started != 0,
            configured_backend: backend.configured_backend,
            effective_backend: backend.effective_backend,
            downgraded: backend.downgraded != 0,
            error_code: backend.error_code,
        },
        SuccessObservationResult {
            observed: success.observed != 0,
            timed_out: success.timed_out != 0,
            used_fallback: success.used_fallback != 0,
            error_code: success.error_code,
        },
        HeartbeatObservationResult {
            observed: heartbeat.observed != 0,
            contract_ok: heartbeat.contract_ok != 0,
            mapping_found: heartbeat.mapping_found != 0,
            timed_out: heartbeat.timed_out != 0,
            error_code: heartbeat.error_code,
        },
        InjectionRetryDecision {
            attempt: retry.attempt,
            backend_started: retry.backend_started != 0,
            success_by_file: retry.success_by_file != 0,
            success_by_heartbeat: retry.success_by_heartbeat != 0,
            success_source: retry.success_source,
            used_heartbeat_fallback: retry.used_heartbeat_fallback != 0,
            succeeded: retry.succeeded != 0,
            should_retry: retry.should_retry != 0,
            retry_delay_ms: retry.retry_delay_ms,
            finished: retry.finished != 0,
        },
    );
    // SAFETY: 调用方承诺 `out_summary` 指向可写输出结构。
    unsafe { out_summary.write(summary.into()) };
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_retry_runtime_create(
    max_retries: u32,
    retry_interval_ms: u32,
) -> *mut InjectionRetryRuntime {
    Box::into_raw(Box::new(InjectionRetryRuntime::new(
        max_retries,
        retry_interval_ms,
    )))
}

#[unsafe(no_mangle)]
/// 释放注入重试状态机。
///
/// # Safety
/// - `runtime` 必须来自 `injector_core_retry_runtime_create`；
/// - 不得重复释放同一指针。
pub unsafe extern "C" fn injector_core_retry_runtime_destroy(
    runtime: *mut InjectionRetryRuntime,
) {
    if runtime.is_null() {
        return;
    }
    // SAFETY: `runtime` 由 `Box::into_raw` 创建，调用方保证仅销毁一次。
    unsafe {
        drop(Box::from_raw(runtime));
    }
}

#[unsafe(no_mangle)]
/// 判断当前是否还允许继续发起新的注入尝试。
///
/// # Safety
/// - `runtime` 必须是有效的 retry runtime 指针。
pub unsafe extern "C" fn injector_core_retry_runtime_can_attempt(
    runtime: *const InjectionRetryRuntime,
) -> u32 {
    if runtime.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    u32::from(unsafe { (*runtime).can_attempt() })
}

#[unsafe(no_mangle)]
/// 返回当前的尝试序号（从 1 开始）。
///
/// # Safety
/// - `runtime` 必须是有效的 retry runtime 指针。
pub unsafe extern "C" fn injector_core_retry_runtime_current_attempt(
    runtime: *const InjectionRetryRuntime,
) -> u32 {
    if runtime.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    unsafe { (*runtime).current_attempt() }
}

#[unsafe(no_mangle)]
/// 根据 APC 排队结果和成功信号，推进一次注入尝试状态机。
///
/// # Safety
/// - `runtime` 必须是有效的 retry runtime 指针；
/// - `out_decision` 必须指向可写的 `InjectorRetryDecisionInterop`。
pub unsafe extern "C" fn injector_core_retry_runtime_finish_attempt(
    runtime: *mut InjectionRetryRuntime,
    apc_queued: u32,
    success_by_file: u32,
    success_by_heartbeat: u32,
    out_decision: *mut InjectorRetryDecisionInterop,
) -> u32 {
    if runtime.is_null() || out_decision.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    let decision = unsafe {
        (*runtime).finish_attempt(apc_queued != 0, success_by_file != 0, success_by_heartbeat != 0)
    };
    // SAFETY: 调用方承诺 `out_decision` 可写。
    unsafe {
        out_decision.write(decision.into());
    }
    1
}

#[unsafe(no_mangle)]
/// 创建 watch_mode 任务状态机。
///
/// # Safety
/// - `config` 必须指向可读的 `InjectorConfigInterop`；
/// - 返回的指针必须最终交由 `injector_core_watch_runtime_destroy` 释放。
pub unsafe extern "C" fn injector_core_watch_runtime_create(
    config: *const InjectorConfigInterop,
    now_tick: u64,
) -> *mut InjectorWatchRuntime {
    if config.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: 调用方承诺 `config` 指向有效只读配置结构体。
    let config = unsafe { &*config };
    let Some(config) = InjectorConfig::from_interop(config) else {
        return std::ptr::null_mut();
    };
    Box::into_raw(Box::new(InjectorWatchRuntime::new(&config, now_tick)))
}

#[unsafe(no_mangle)]
/// 销毁 watch_mode 任务状态机。
///
/// # Safety
/// - `runtime` 必须来自 `injector_core_watch_runtime_create`；
/// - 不得重复释放同一指针。
pub unsafe extern "C" fn injector_core_watch_runtime_destroy(
    runtime: *mut InjectorWatchRuntime,
) {
    if runtime.is_null() {
        return;
    }
    // SAFETY: `runtime` 由 `Box::into_raw` 创建，且调用方保证仅销毁一次。
    unsafe {
        drop(Box::from_raw(runtime));
    }
}

#[unsafe(no_mangle)]
/// 更新当前扫描到的目标进程 PID 列表。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针；
/// - 当 `pid_len > 0` 时，`pid_ptr` 必须指向长度为 `pid_len` 的可读 `u32` 数组。
pub unsafe extern "C" fn injector_core_watch_runtime_observe_processes(
    runtime: *mut InjectorWatchRuntime,
    now_tick: u64,
    pid_ptr: *const u32,
    pid_len: usize,
) -> u32 {
    if runtime.is_null() {
        return 0;
    }
    let pids = if pid_ptr.is_null() || pid_len == 0 {
        &[][..]
    } else {
        // SAFETY: 调用方承诺 `pid_ptr` 指向长度为 `pid_len` 的只读 PID 数组。
        unsafe { std::slice::from_raw_parts(pid_ptr, pid_len) }
    };
    // SAFETY: 调用方承诺 `runtime` 有效，这里仅做可变借用并调用纯逻辑状态机。
    unsafe {
        (*runtime).observe_processes(now_tick, pids);
    }
    1
}

#[unsafe(no_mangle)]
/// 收集当前可启动的待注入 PID 列表。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针；
/// - `out_pid_buffer` 必须指向长度至少为 `out_capacity` 的可写缓冲区。
pub unsafe extern "C" fn injector_core_watch_runtime_collect_pending(
    runtime: *const InjectorWatchRuntime,
    max_count: usize,
    out_pid_buffer: *mut u32,
    out_capacity: usize,
) -> usize {
    if runtime.is_null() || out_pid_buffer.is_null() || out_capacity == 0 {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效，这里只读访问状态机。
    let pending = unsafe { (*runtime).collect_pending(max_count.min(out_capacity)) };
    // SAFETY: 调用方承诺 `out_pid_buffer` 可写且容量不少于 `out_capacity`，
    // 这里最多写入 `pending.len()` 个 PID。
    unsafe {
        std::ptr::copy_nonoverlapping(pending.as_ptr(), out_pid_buffer, pending.len());
    }
    pending.len()
}

#[unsafe(no_mangle)]
/// 将某个 PID 标记为“任务已启动”。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针。
pub unsafe extern "C" fn injector_core_watch_runtime_mark_started(
    runtime: *mut InjectorWatchRuntime,
    pid: u32,
) -> u32 {
    if runtime.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    u32::from(unsafe { (*runtime).mark_started(pid) })
}

#[unsafe(no_mangle)]
/// 将某个 PID 标记为“任务已结束”。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针。
pub unsafe extern "C" fn injector_core_watch_runtime_mark_finished(
    runtime: *mut InjectorWatchRuntime,
    pid: u32,
    succeeded: u32,
) -> u32 {
    if runtime.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    u32::from(unsafe { (*runtime).mark_finished(pid, succeeded != 0) })
}

#[unsafe(no_mangle)]
/// 收集已经可以从 watch_mode 状态机中移除的 PID。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针；
/// - `out_pid_buffer` 必须指向长度至少为 `out_capacity` 的可写缓冲区。
pub unsafe extern "C" fn injector_core_watch_runtime_collect_removals(
    runtime: *mut InjectorWatchRuntime,
    out_pid_buffer: *mut u32,
    out_capacity: usize,
) -> usize {
    if runtime.is_null() || out_pid_buffer.is_null() || out_capacity == 0 {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效，这里会消费可移除任务。
    let removals = unsafe { (*runtime).collect_removals() };
    let count = removals.len().min(out_capacity);
    // SAFETY: 调用方承诺 `out_pid_buffer` 有足够可写容量，这里只写入 `count` 个 PID。
    unsafe {
        std::ptr::copy_nonoverlapping(removals.as_ptr(), out_pid_buffer, count);
    }
    count
}

#[unsafe(no_mangle)]
/// 判断 watch_mode 是否已达到 idle 自动退出条件。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针。
pub unsafe extern "C" fn injector_core_watch_runtime_should_exit_idle(
    runtime: *const InjectorWatchRuntime,
    now_tick: u64,
) -> u32 {
    if runtime.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    u32::from(unsafe { (*runtime).should_exit_idle(now_tick) })
}

#[unsafe(no_mangle)]
/// 返回当前 watch_mode 任务表中的任务数量。
///
/// # Safety
/// - `runtime` 必须是有效的 runtime 指针。
pub unsafe extern "C" fn injector_core_watch_runtime_task_count(
    runtime: *const InjectorWatchRuntime,
) -> usize {
    if runtime.is_null() {
        return 0;
    }
    // SAFETY: 调用方承诺 `runtime` 有效。
    unsafe { (*runtime).task_count() }
}
