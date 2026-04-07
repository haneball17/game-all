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

typedef struct PayloadKeyDecisionInterop {
    uint32_t is_valid;
    uint32_t is_alive;
    uint32_t is_paused;
    uint32_t should_clear;
    uint32_t is_bypass_process;
    uint32_t target_marked;
    uint32_t block_marked;
    uint32_t should_block;
    uint32_t desired_down;
    uint32_t active_pid;
    uint32_t flags;
    uint32_t profile_id;
    uint32_t profile_mode;
    uint64_t last_tick;
} PayloadKeyDecisionInterop;

typedef struct PayloadLogicalKeyDecisionInterop {
    uint32_t is_valid;
    uint32_t is_alive;
    uint32_t is_paused;
    uint32_t should_clear;
    uint32_t is_bypass_process;
    uint32_t target_marked;
    uint32_t block_marked;
    uint32_t should_block;
    uint32_t desired_down;
    uint32_t pressed_edge;
    uint32_t released_edge;
    uint32_t is_direction;
    uint32_t pair_conflict;
    uint32_t repeat_allowed;
    uint32_t active_pid;
    uint32_t flags;
    uint32_t profile_id;
    uint32_t profile_mode;
    uint64_t last_tick;
} PayloadLogicalKeyDecisionInterop;

typedef struct PayloadChannelEmitDecisionInterop {
    uint32_t emit_action;
    uint32_t desired_down;
    uint32_t projected_down_before;
    uint32_t projected_down_after;
    uint32_t should_block;
    uint32_t suppress_repeat;
    uint32_t transition_reason;
} PayloadChannelEmitDecisionInterop;

typedef struct PayloadPathDecisionInterop {
    uint32_t is_valid;
    uint32_t is_alive;
    uint32_t is_paused;
    uint32_t should_clear;
    uint32_t is_bypass_process;
    uint32_t should_spoof_focus;
    uint32_t can_process_keys;
    uint32_t should_use_mapping;
    uint32_t active_pid;
    uint32_t flags;
    uint32_t profile_id;
    uint32_t profile_mode;
    uint64_t last_tick;
} PayloadPathDecisionInterop;

typedef struct PayloadInputPathObservationInterop {
    uint32_t channel_kind;
    uint32_t raw_promoted;
    uint32_t raw_active;
    uint32_t direct_input_active;
    uint32_t win32_active;
    uint32_t mixed_inputs;
    uint32_t profile_id;
    uint32_t profile_mode;
} PayloadInputPathObservationInterop;

typedef struct PayloadAdapterProjectedStateInterop {
    uint32_t desired_down;
    uint32_t raw_projected;
    uint32_t win32_projected;
    uint32_t direct_input_projected;
    uint32_t raw_drift;
    uint32_t win32_drift;
    uint32_t direct_input_drift;
    uint32_t any_drift;
} PayloadAdapterProjectedStateInterop;

typedef struct PayloadAdapterDriftSummaryInterop {
    uint32_t raw_drift_count;
    uint32_t win32_drift_count;
    uint32_t direct_input_drift_count;
} PayloadAdapterDriftSummaryInterop;

typedef struct PayloadPauseReleaseDecisionInterop {
    uint32_t should_emit;
    uint32_t vkey;
    uint32_t is_down;
    uint32_t had_projected;
    uint32_t reason;
} PayloadPauseReleaseDecisionInterop;

typedef struct PayloadClearResetDecisionInterop {
    uint32_t should_clear_logical;
    uint32_t should_clear_projected;
    uint32_t raw_projected_cleared;
    uint32_t win32_projected_cleared;
    uint32_t direct_input_projected_cleared;
} PayloadClearResetDecisionInterop;

typedef struct PayloadProjectedStateUpdateInterop {
    uint32_t changed;
    uint32_t projected_before;
    uint32_t projected_after;
    uint32_t transition_reason;
} PayloadProjectedStateUpdateInterop;

typedef struct PayloadSyncObservationSnapshotInterop {
    uint32_t channel_kind;
    uint32_t active_pid;
    uint32_t is_alive;
    uint32_t is_paused;
    uint32_t raw_promoted;
    uint32_t raw_active;
    uint32_t direct_input_active;
    uint32_t win32_active;
    uint32_t mixed_inputs;
    uint32_t raw_drift_count;
    uint32_t win32_drift_count;
    uint32_t direct_input_drift_count;
    uint32_t profile_id;
    uint32_t profile_mode;
} PayloadSyncObservationSnapshotInterop;

typedef struct PayloadAdapterDiagnosticsEventInterop {
    uint64_t tick_ms;
    uint32_t event_kind;
    uint32_t channel_kind;
    uint32_t vkey;
    uint32_t desired_down;
    uint32_t projected_before;
    uint32_t projected_after;
    uint32_t forced_release;
    uint32_t reason_code;
} PayloadAdapterDiagnosticsEventInterop;

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

GAME_PAYLOAD_CORE_API uint32_t payload_core_evaluate_key_state_header(
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    uint32_t target_marked,
    uint32_t block_marked,
    uint32_t keyboard_down,
    uint32_t force_release,
    PayloadKeyDecisionInterop* out_decision);

GAME_PAYLOAD_CORE_API uint32_t payload_core_evaluate_logical_key_header(
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    uint32_t vkey,
    uint32_t target_marked,
    uint32_t block_marked,
    uint32_t keyboard_down,
    uint32_t edge_counter,
    uint32_t pair_vkey,
    uint32_t pair_target_marked,
    uint32_t pair_keyboard_down,
    uint32_t pair_edge_counter,
    uint32_t force_release,
    uint32_t previous_desired_down,
    uint32_t repeat_allowed,
    PayloadLogicalKeyDecisionInterop* out_decision);

GAME_PAYLOAD_CORE_API uint32_t payload_core_decide_channel_emit(
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    uint32_t vkey,
    uint32_t target_marked,
    uint32_t block_marked,
    uint32_t keyboard_down,
    uint32_t edge_counter,
    uint32_t pair_vkey,
    uint32_t pair_target_marked,
    uint32_t pair_keyboard_down,
    uint32_t pair_edge_counter,
    uint32_t force_release,
    uint32_t previous_desired_down,
    uint32_t repeat_allowed,
    uint32_t projected_down_before,
    uint32_t observed_down,
    PayloadLogicalKeyDecisionInterop* out_logical,
    PayloadChannelEmitDecisionInterop* out_emit);

GAME_PAYLOAD_CORE_API uint32_t payload_core_evaluate_path_decision_header(
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    uint32_t mapping_mode_value,
    PayloadPathDecisionInterop* out_decision);
GAME_PAYLOAD_CORE_API uint32_t payload_core_observe_input_path(
    uint32_t raw_promoted,
    uint32_t raw_data_count,
    uint32_t raw_buffer_count,
    uint32_t di_state_count,
    uint32_t di_data_count,
    uint32_t win32_async_count,
    uint32_t win32_keyboard_count,
    uint32_t profile_id,
    uint32_t profile_mode,
    PayloadInputPathObservationInterop* out_observation);
GAME_PAYLOAD_CORE_API uint32_t payload_core_evaluate_adapter_projected_state(
    uint32_t desired_down,
    uint32_t raw_projected,
    uint32_t win32_projected,
    uint32_t direct_input_projected,
    PayloadAdapterProjectedStateInterop* out_state);
GAME_PAYLOAD_CORE_API uint32_t payload_core_summarize_adapter_drift(
    const uint8_t* logical_desired_ptr,
    const uint8_t* raw_projected_ptr,
    const uint8_t* win32_projected_ptr,
    const uint8_t* direct_input_projected_ptr,
    size_t len,
    PayloadAdapterDriftSummaryInterop* out_summary);
GAME_PAYLOAD_CORE_API void* payload_core_state_store_create(void);
GAME_PAYLOAD_CORE_API void payload_core_state_store_destroy(void* state);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_set_logical_desired(
    void* state,
    uint32_t vkey,
    uint32_t down);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_get_logical_desired(
    const void* state,
    uint32_t vkey);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_set_projected(
    void* state,
    uint32_t channel_kind,
    uint32_t vkey,
    uint32_t down);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_get_projected(
    const void* state,
    uint32_t channel_kind,
    uint32_t vkey);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_evaluate_logical_key(
    void* state,
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    uint32_t vkey,
    uint32_t target_marked,
    uint32_t block_marked,
    uint32_t keyboard_down,
    uint32_t edge_counter,
    uint32_t pair_vkey,
    uint32_t pair_target_marked,
    uint32_t pair_keyboard_down,
    uint32_t pair_edge_counter,
    uint32_t force_release,
    uint32_t repeat_allowed,
    PayloadLogicalKeyDecisionInterop* out_logical);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_decide_channel_emit(
    void* state,
    uint32_t channel_kind,
    uint32_t flags,
    uint32_t active_pid,
    uint32_t profile_id,
    uint32_t profile_mode,
    uint64_t last_tick,
    uint32_t current_pid,
    uint64_t now_tick,
    uint64_t heartbeat_timeout_ms,
    uint32_t vkey,
    uint32_t target_marked,
    uint32_t block_marked,
    uint32_t keyboard_down,
    uint32_t edge_counter,
    uint32_t pair_vkey,
    uint32_t pair_target_marked,
    uint32_t pair_keyboard_down,
    uint32_t pair_edge_counter,
    uint32_t force_release,
    uint32_t repeat_allowed,
    uint32_t observed_down,
    PayloadLogicalKeyDecisionInterop* out_logical,
    PayloadChannelEmitDecisionInterop* out_emit);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_clear_logical_desired(
    void* state);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_clear_all_projected(
    void* state);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_clear_projected_channel(
    void* state,
    uint32_t channel_kind);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_update_projected(
    void* state,
    uint32_t channel_kind,
    uint32_t vkey,
    uint32_t down,
    PayloadProjectedStateUpdateInterop* out_update);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_pick_pause_release(
    void* state,
    int32_t preferred_vkey,
    PayloadPauseReleaseDecisionInterop* out_decision);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_apply_clear_reset(
    void* state,
    uint32_t clear_logical,
    uint32_t clear_projected,
    PayloadClearResetDecisionInterop* out_decision);
GAME_PAYLOAD_CORE_API uint32_t payload_core_state_store_summarize_drift(
    const void* state,
    PayloadAdapterDriftSummaryInterop* out_summary);
GAME_PAYLOAD_CORE_API uint32_t payload_core_build_sync_observation_snapshot(
    uint32_t active_pid,
    uint32_t is_alive,
    uint32_t is_paused,
    const PayloadInputPathObservationInterop* observation,
    const PayloadAdapterDriftSummaryInterop* drift,
    PayloadSyncObservationSnapshotInterop* out_snapshot);
GAME_PAYLOAD_CORE_API void* payload_core_diagnostics_buffer_create(size_t capacity);
GAME_PAYLOAD_CORE_API void payload_core_diagnostics_buffer_destroy(void* buffer);
GAME_PAYLOAD_CORE_API uint32_t payload_core_diagnostics_buffer_push_event(
    void* buffer,
    const PayloadAdapterDiagnosticsEventInterop* event);
GAME_PAYLOAD_CORE_API uint32_t payload_core_diagnostics_buffer_latest(
    const void* buffer,
    PayloadAdapterDiagnosticsEventInterop* out_event);
GAME_PAYLOAD_CORE_API size_t payload_core_diagnostics_buffer_copy_latest_n(
    const void* buffer,
    size_t limit,
    PayloadAdapterDiagnosticsEventInterop* out_events,
    size_t out_capacity);

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
