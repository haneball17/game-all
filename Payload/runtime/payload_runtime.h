#pragma once

#include <windows.h>

namespace PayloadRuntime {

// Payload 统一初始化状态。
enum class InitState : LONG {
    kNotStarted = 0,
    kLoadingConfig = 1,
    kStartingSync = 2,
    kStartingHelper = 3,
    kRunning = 4,
    kDegraded = 5,
    kStopping = 6,
    kStopped = 7,
};

// 统一控制隐蔽相关能力，默认关闭。
void SetStealthEnabled(bool enabled);
bool IsStealthEnabled();

// 统一记录初始化状态，便于日志与故障排查。
void SetInitState(InitState state);
InitState GetInitState();
const wchar_t* InitStateToText(InitState state);

} // namespace PayloadRuntime
