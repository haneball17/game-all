#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GAME_INJECTOR_CORE_API
#define GAME_INJECTOR_CORE_API
#endif

// 与 Rust 侧 InjectorConfigView 一一对应，只暴露第一阶段稳定的数值配置视图。
typedef struct InjectorConfigView {
    uint32_t scan_interval_ms;
    uint32_t inject_delay_ms;
    uint32_t window_wait_timeout_ms;
    uint32_t window_poll_interval_ms;
    uint32_t post_window_delay_ms;
    uint32_t max_retries;
    uint32_t retry_interval_ms;
    uint32_t success_timeout_ms;
    uint32_t success_interval_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t heartbeat_interval_ms;
    uint32_t inject_backend;
    uint32_t success_observer_mode;
    uint32_t watch_mode;
    uint32_t idle_exit_seconds;
    uint32_t max_concurrent_tasks;
} InjectorConfigView;

typedef struct InjectorConfigInterop {
    char process_name[64];
    char dll_path[1024];
    char output_dir[1024];
    InjectorConfigView view;
} InjectorConfigInterop;

typedef struct InjectorWatchRuntime InjectorWatchRuntime;
typedef struct InjectionRetryRuntime InjectionRetryRuntime;

typedef struct InjectorHelperHeartbeatDecisionInterop {
    uint32_t contract_ok;
    uint32_t heartbeat_ok;
} InjectorHelperHeartbeatDecisionInterop;

typedef struct InjectorRetryDecisionInterop {
    uint32_t attempt;
    uint32_t success_by_file;
    uint32_t success_by_heartbeat;
    uint32_t succeeded;
    uint32_t should_retry;
    uint32_t retry_delay_ms;
    uint32_t finished;
} InjectorRetryDecisionInterop;

typedef struct InjectorWindowProbeResultInterop {
    uint32_t ok;
    uint32_t timed_out;
    uint32_t used_hint;
    uint32_t error_code;
} InjectorWindowProbeResultInterop;

typedef struct InjectorSuccessObservationResultInterop {
    uint32_t observed;
    uint32_t timed_out;
    uint32_t used_fallback;
    uint32_t error_code;
} InjectorSuccessObservationResultInterop;

typedef struct InjectorHeartbeatObservationResultInterop {
    uint32_t observed;
    uint32_t contract_ok;
    uint32_t mapping_found;
    uint32_t timed_out;
    uint32_t error_code;
} InjectorHeartbeatObservationResultInterop;

typedef struct InjectorBackendExecutionResultInterop {
    uint32_t started;
    uint32_t configured_backend;
    uint32_t effective_backend;
    uint32_t downgraded;
    uint32_t error_code;
} InjectorBackendExecutionResultInterop;

GAME_INJECTOR_CORE_API InjectorConfigView injector_core_default_view(void);
GAME_INJECTOR_CORE_API uint32_t injector_core_load_config_utf8(
    const uint8_t* text_ptr,
    size_t text_len,
    const uint8_t* base_dir_ptr,
    size_t base_dir_len,
    InjectorConfigInterop* out_config);
GAME_INJECTOR_CORE_API uint32_t injector_core_parse_ini_text_utf8(
    const uint8_t* text_ptr,
    size_t text_len,
    InjectorConfigInterop* out_config);
GAME_INJECTOR_CORE_API const char* injector_core_default_ini_text_utf8(void);
GAME_INJECTOR_CORE_API size_t injector_core_default_ini_text_utf8_len(void);
GAME_INJECTOR_CORE_API uint32_t injector_core_helper_status_version(void);
GAME_INJECTOR_CORE_API uint32_t injector_core_helper_status_size(void);
GAME_INJECTOR_CORE_API InjectorHelperHeartbeatDecisionInterop injector_core_evaluate_helper_heartbeat(
    uint32_t status_version,
    uint32_t status_size,
    uint32_t process_alive,
    uint64_t last_tick,
    uint64_t now_tick,
    uint64_t timeout_ms);
GAME_INJECTOR_CORE_API InjectionRetryRuntime* injector_core_retry_runtime_create(
    uint32_t max_retries,
    uint32_t retry_interval_ms);
GAME_INJECTOR_CORE_API void injector_core_retry_runtime_destroy(
    InjectionRetryRuntime* runtime);
GAME_INJECTOR_CORE_API uint32_t injector_core_retry_runtime_can_attempt(
    const InjectionRetryRuntime* runtime);
GAME_INJECTOR_CORE_API uint32_t injector_core_retry_runtime_current_attempt(
    const InjectionRetryRuntime* runtime);
GAME_INJECTOR_CORE_API uint32_t injector_core_retry_runtime_finish_attempt(
    InjectionRetryRuntime* runtime,
    uint32_t apc_queued,
    uint32_t success_by_file,
    uint32_t success_by_heartbeat,
    InjectorRetryDecisionInterop* out_decision);
GAME_INJECTOR_CORE_API uint32_t injector_core_retry_runtime_finish_attempt_with_results(
    InjectionRetryRuntime* runtime,
    const InjectorBackendExecutionResultInterop* backend,
    const InjectorSuccessObservationResultInterop* success,
    const InjectorHeartbeatObservationResultInterop* heartbeat,
    InjectorRetryDecisionInterop* out_decision);
GAME_INJECTOR_CORE_API InjectorWatchRuntime* injector_core_watch_runtime_create(
    const InjectorConfigInterop* config,
    uint64_t now_tick);
GAME_INJECTOR_CORE_API void injector_core_watch_runtime_destroy(
    InjectorWatchRuntime* runtime);
GAME_INJECTOR_CORE_API uint32_t injector_core_watch_runtime_observe_processes(
    InjectorWatchRuntime* runtime,
    uint64_t now_tick,
    const uint32_t* pid_ptr,
    size_t pid_len);
GAME_INJECTOR_CORE_API size_t injector_core_watch_runtime_collect_pending(
    const InjectorWatchRuntime* runtime,
    size_t max_count,
    uint32_t* out_pid_buffer,
    size_t out_capacity);
GAME_INJECTOR_CORE_API uint32_t injector_core_watch_runtime_mark_started(
    InjectorWatchRuntime* runtime,
    uint32_t pid);
GAME_INJECTOR_CORE_API uint32_t injector_core_watch_runtime_mark_finished(
    InjectorWatchRuntime* runtime,
    uint32_t pid,
    uint32_t succeeded);
GAME_INJECTOR_CORE_API size_t injector_core_watch_runtime_collect_removals(
    InjectorWatchRuntime* runtime,
    uint32_t* out_pid_buffer,
    size_t out_capacity);
GAME_INJECTOR_CORE_API uint32_t injector_core_watch_runtime_should_exit_idle(
    const InjectorWatchRuntime* runtime,
    uint64_t now_tick);
GAME_INJECTOR_CORE_API size_t injector_core_watch_runtime_task_count(
    const InjectorWatchRuntime* runtime);

#ifdef __cplusplus
}
#endif
