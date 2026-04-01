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

GAME_INJECTOR_CORE_API InjectorConfigView injector_core_default_view(void);
GAME_INJECTOR_CORE_API uint32_t injector_core_parse_ini_text_utf8(
    const uint8_t* text_ptr,
    size_t text_len,
    InjectorConfigInterop* out_config);
GAME_INJECTOR_CORE_API const char* injector_core_default_ini_text_utf8(void);
GAME_INJECTOR_CORE_API size_t injector_core_default_ini_text_utf8_len(void);
GAME_INJECTOR_CORE_API uint32_t injector_core_helper_status_version(void);
GAME_INJECTOR_CORE_API uint32_t injector_core_helper_status_size(void);

#ifdef __cplusplus
}
#endif
