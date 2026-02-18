#include "payload_runtime.h"

namespace {

volatile LONG g_stealth_enabled = 0;
volatile LONG g_init_state = static_cast<LONG>(PayloadRuntime::InitState::kNotStarted);

} // namespace

namespace PayloadRuntime {

void SetStealthEnabled(bool enabled)
{
    InterlockedExchange(&g_stealth_enabled, enabled ? 1 : 0);
}

bool IsStealthEnabled()
{
    return InterlockedCompareExchange(&g_stealth_enabled, 0, 0) == 1;
}

void SetInitState(InitState state)
{
    InterlockedExchange(&g_init_state, static_cast<LONG>(state));
}

InitState GetInitState()
{
    return static_cast<InitState>(InterlockedCompareExchange(&g_init_state, 0, 0));
}

const wchar_t* InitStateToText(InitState state)
{
    switch (state)
    {
    case InitState::kNotStarted:
        return L"not_started";
    case InitState::kLoadingConfig:
        return L"loading_config";
    case InitState::kStartingSync:
        return L"starting_sync";
    case InitState::kStartingHelper:
        return L"starting_helper";
    case InitState::kRunning:
        return L"running";
    case InitState::kDegraded:
        return L"degraded";
    case InitState::kStopping:
        return L"stopping";
    case InitState::kStopped:
        return L"stopped";
    default:
        return L"unknown";
    }
}

} // namespace PayloadRuntime
