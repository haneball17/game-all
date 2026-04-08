#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GAME_CONTROL_CORE_API
#define GAME_CONTROL_CORE_API
#endif

typedef struct ControlWindowSnapshotInterop {
    uint32_t foreground_is_dnf;
    uint32_t foreground_process_id;
    uint32_t master_process_id;
    uint32_t total_count;
} ControlWindowSnapshotInterop;

typedef struct ControlForegroundTrackerInterop {
    uint32_t last_foreground_pid;
    uint64_t last_foreground_tick_ms;
} ControlForegroundTrackerInterop;

typedef struct ControlForegroundDecisionInterop {
    uint32_t last_foreground_pid;
    uint64_t last_foreground_tick_ms;
    uint32_t effective_foreground_pid;
    uint32_t effective_foreground_is_dnf;
    uint32_t auto_paused;
} ControlForegroundDecisionInterop;

typedef struct ControlPublishHeaderInterop {
    uint32_t flags;
    uint32_t active_pid;
    uint32_t profile_id;
    uint32_t profile_mode;
    uint64_t last_tick;
} ControlPublishHeaderInterop;

typedef struct ControlHeartbeatPlanInterop {
    uint32_t should_align_physical_input;
    uint32_t should_publish_snapshot;
} ControlHeartbeatPlanInterop;

typedef struct ControlKeyStateCore ControlKeyStateCore;

GAME_CONTROL_CORE_API ControlForegroundDecisionInterop game_control_core_evaluate_foreground(
    ControlWindowSnapshotInterop snapshot,
    ControlForegroundTrackerInterop tracker,
    uint64_t now_ms,
    uint64_t foreground_grace_ms,
    uint32_t disable_auto_pause);

GAME_CONTROL_CORE_API ControlPublishHeaderInterop game_control_core_build_publish_header(
    uint32_t user_paused,
    uint32_t auto_paused,
    uint32_t force_clear,
    uint32_t effective_foreground_is_dnf,
    uint32_t effective_foreground_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick);

GAME_CONTROL_CORE_API uint32_t game_control_core_finalize_publish_profile(
    uint32_t profile_mode,
    uint32_t mapping_behavior_replace,
    const uint8_t* mapping_source_mask_ptr,
    size_t mapping_source_len,
    uint8_t* block_mask_ptr,
    size_t block_mask_len);

GAME_CONTROL_CORE_API void game_control_core_finalize_input_mask(
    uint32_t profile_mode,
    uint32_t mapping_behavior_replace,
    const uint8_t* mapping_source_mask_ptr,
    size_t mapping_source_len,
    uint8_t* input_mask_ptr,
    size_t input_mask_len);

GAME_CONTROL_CORE_API void game_control_core_build_physical_alignment_plan(
    uint32_t paused,
    uint32_t effective_foreground_is_dnf,
    const uint8_t* input_mask_ptr,
    const uint8_t* physical_down_ptr,
    size_t len,
    uint8_t* out_apply_mask_ptr,
    uint8_t* out_desired_down_ptr);

GAME_CONTROL_CORE_API ControlHeartbeatPlanInterop game_control_core_build_heartbeat_plan(
    uint32_t shared_memory_ready);

GAME_CONTROL_CORE_API ControlKeyStateCore* game_control_core_key_state_create(void);
GAME_CONTROL_CORE_API void game_control_core_key_state_destroy(ControlKeyStateCore* state);
GAME_CONTROL_CORE_API uint32_t game_control_core_key_state_set_state(
    ControlKeyStateCore* state,
    uint32_t vkey,
    uint32_t is_down);
GAME_CONTROL_CORE_API void game_control_core_key_state_clear(ControlKeyStateCore* state);
GAME_CONTROL_CORE_API void game_control_core_key_state_copy_edge_counters(
    const ControlKeyStateCore* state,
    uint32_t* out_edge_ptr,
    size_t out_len);
GAME_CONTROL_CORE_API void game_control_core_key_state_build_effective(
    ControlKeyStateCore* state,
    const uint8_t* repeat_mask_ptr,
    size_t repeat_mask_len,
    uint32_t repeat_interval_ms,
    uint64_t now_ms,
    uint8_t* out_effective_down_ptr,
    uint32_t* out_effective_edge_ptr,
    size_t out_len);

#ifdef __cplusplus
}
#endif
