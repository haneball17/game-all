#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GAME_HELPER_CORE_API
#define GAME_HELPER_CORE_API
#endif

typedef struct HelperStatusContractDecisionInterop {
    uint32_t contract_ok;
    uint32_t process_alive;
} HelperStatusContractDecisionInterop;

typedef struct HelperStatusSnapshotInputInterop {
    uint64_t last_tick_ms;
    uint32_t pid;
    uint32_t process_alive;
    uint32_t auto_transparent_enabled;
    uint32_t fullscreen_attack_target;
    uint32_t fullscreen_attack_patch_on;
    int32_t attract_mode;
    uint32_t attract_positive;
    uint32_t gather_items_enabled;
    uint32_t damage_enabled;
    int32_t damage_multiplier;
    uint32_t invincible_enabled;
    uint32_t summon_enabled;
    uint64_t summon_last_tick;
    uint32_t fullscreen_skill_enabled;
    uint32_t fullscreen_skill_active;
    uint32_t fullscreen_skill_hotkey;
    uint32_t hotkey_enabled;
} HelperStatusSnapshotInputInterop;

typedef struct HelperControlApplyPlanInterop {
    uint32_t apply_fullscreen_attack_target;
    uint32_t fullscreen_attack_target;
    uint32_t apply_fullscreen_skill_enabled;
    uint32_t fullscreen_skill_enabled;
    uint32_t apply_auto_transparent_enabled;
    uint32_t auto_transparent_enabled;
    uint32_t apply_hotkey_enabled;
    uint32_t hotkey_enabled;
    uint32_t apply_attract_mode;
    int32_t attract_mode;
    uint32_t apply_attract_enabled;
    uint32_t attract_enabled;
    uint32_t apply_attract_positive;
    uint32_t attract_positive;
    uint32_t apply_gather_items_enabled;
    uint32_t gather_items_enabled;
    uint32_t apply_damage_multiplier;
    int32_t damage_multiplier;
    uint32_t apply_damage_enabled;
    uint32_t damage_enabled;
    uint32_t apply_invincible_enabled;
    uint32_t invincible_enabled;
    uint32_t summon_sequence;
    uint32_t action_sequence;
} HelperControlApplyPlanInterop;

typedef struct HelperControlRuntimeStateInterop {
    uint32_t last_summon_sequence;
    uint32_t last_action_sequence;
    uint8_t fullscreen_attack;
    uint8_t fullscreen_skill;
    uint8_t auto_transparent;
    uint8_t attract;
    uint8_t hotkey_enabled;
} HelperControlRuntimeStateInterop;

typedef struct HelperControlTickDecisionInterop {
    uint32_t last_summon_sequence;
    uint32_t last_action_sequence;
    uint8_t fullscreen_attack;
    uint8_t fullscreen_skill;
    uint8_t auto_transparent;
    uint8_t attract;
    uint8_t hotkey_enabled;
    uint32_t control_changed;
    uint32_t should_apply_overrides;
    uint32_t summon_sequence_changed;
    uint32_t action_sequence_changed;
} HelperControlTickDecisionInterop;

GAME_HELPER_CORE_API uint32_t game_helper_core_evaluate_status_contract(
    const void* snapshot,
    HelperStatusContractDecisionInterop* out_decision);

GAME_HELPER_CORE_API uint32_t game_helper_core_evaluate_control_contract(
    const void* snapshot);

GAME_HELPER_CORE_API uint32_t game_helper_core_decode_control_apply_plan(
    const void* snapshot,
    HelperControlApplyPlanInterop* out_plan);

GAME_HELPER_CORE_API uint32_t game_helper_core_build_status_snapshot(
    const HelperStatusSnapshotInputInterop* input,
    void* out_snapshot);

GAME_HELPER_CORE_API uint32_t game_helper_core_evaluate_control_tick(
    const HelperControlRuntimeStateInterop* state,
    const void* snapshot,
    HelperControlTickDecisionInterop* out_decision);

#ifdef __cplusplus
}
#endif
