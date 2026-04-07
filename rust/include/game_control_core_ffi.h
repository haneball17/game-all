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

#ifdef __cplusplus
}
#endif
