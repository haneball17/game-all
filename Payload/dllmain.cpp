// Payload/dllmain.cpp
#include "pch.h"
#include <atomic>

// 声明外部模块的启动接口 (在 namespace 中)
namespace Helper { void Start(); void Stop(); }
namespace Sync   { void Start(); void Stop(); }

// 原子锁：APC 注入可能会触发多次 LoadLibrary，必须防止重复初始化
std::atomic<bool> g_IsInitialized(false);

// 统一初始化线程
DWORD WINAPI UnifiedWorkerThread(LPVOID lpParam) {
    // 1. 启动同步模块 (通常 Hook 底层输入建议先启动)
    try {
        Sync::Start();
    } catch (...) {
        OutputDebugStringA("[Unified] Sync module failed to start.");
    }

    // 2. 启动辅助模块 (Helper)
    try {
        Helper::Start();
    } catch (...) {
        OutputDebugStringA("[Unified] Helper module failed to start.");
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // 核心改动：使用原子操作确保只执行一次
        // expected 为 false，如果 g_IsInitialized 是 false，则将其设为 true 并返回 true
        // 否则返回 false (代表已经被其他线程初始化了)
        {
            bool expected = false;
            if (g_IsInitialized.compare_exchange_strong(expected, true)) {
                DisableThreadLibraryCalls(hModule);
                
                // 必须在独立线程中初始化，避免卡死 DllMain (Loader Lock)
                CreateThread(NULL, 0, UnifiedWorkerThread, NULL, 0, NULL);
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        // 进程退出时清理
        break;
    }
    return TRUE;
}