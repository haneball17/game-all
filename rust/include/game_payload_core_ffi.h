#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GAME_PAYLOAD_CORE_API
#define GAME_PAYLOAD_CORE_API
#endif

typedef struct PayloadRuntimeDecisionInterop {
    uint32_t is_valid;
    uint32_t is_alive;
    uint32_t is_paused;
    uint32_t should_clear;
    uint32_t is_bypass_process;
    uint32_t active_pid;
    uint32_t flags;
    uint32_t profile_id;
    uint32_t profile_mode;
    uint64_t last_tick;
} PayloadRuntimeDecisionInterop;

GAME_PAYLOAD_CORE_API uint32_t payload_core_evaluate_runtime_state(
    const void* snapshot_ptr,
    size_t mapping_size,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    PayloadRuntimeDecisionInterop* out_decision);

GAME_PAYLOAD_CORE_API uint32_t payload_core_evaluate_runtime_header(
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    PayloadRuntimeDecisionInterop* out_decision);

GAME_PAYLOAD_CORE_API void* payload_core_convergence_create(uint8_t extra_release_pulses);
GAME_PAYLOAD_CORE_API void payload_core_convergence_destroy(void* state);
GAME_PAYLOAD_CORE_API uint32_t payload_core_convergence_on_key_event(
    void* state,
    uint32_t vkey,
    uint32_t is_down);
GAME_PAYLOAD_CORE_API uint32_t payload_core_convergence_take_force_release_mask(
    void* state,
    uint8_t* out_mask,
    size_t out_len);
GAME_PAYLOAD_CORE_API uint32_t payload_core_convergence_should_refresh(
    const void* state,
    uint64_t age_ms,
    uint64_t max_age_ms);

#ifdef __cplusplus
}
#endif
