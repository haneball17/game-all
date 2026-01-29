// Payload/dllmain.cpp
#include "pch.h"
#include <atomic>
#include <string>

#ifdef _DEBUG
#include <vector>
#endif

// 声明外部模块的启动接口 (在 namespace 中)
namespace Helper { void Start(); void Stop(); }
namespace Sync   { void Start(); void Stop(); }

// 原子锁：APC 注入可能会触发多次 LoadLibrary，必须防止重复初始化
std::atomic<bool> g_IsInitialized(false);
static HMODULE g_selfModule = nullptr;

#ifdef _DEBUG
static std::wstring GetModuleDirectory(HMODULE module)
{
    if (module == nullptr)
    {
        return L".";
    }
    wchar_t path[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return L".";
    }
    for (int i = static_cast<int>(length) - 1; i >= 0; --i)
    {
        if (path[i] == L'\\' || path[i] == L'/')
        {
            path[i] = L'\0';
            break;
        }
    }
    return path;
}

static void AppendPayloadDebugLog(const std::wstring& message)
{
    std::wstring baseDir = GetModuleDirectory(g_selfModule);
    std::wstring logDir = baseDir + L"\\logs";
    CreateDirectoryW(logDir.c_str(), nullptr);
    std::wstring logPath = logDir + L"\\payload_debug.log";

    HANDLE file = CreateFileW(
        logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t prefix[64] = {0};
    swprintf_s(
        prefix,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    std::wstring line = prefix + message + L"\r\n";
    int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size > 1)
    {
        std::vector<char> utf8(size - 1);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), size - 1, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}
#endif

// 统一初始化线程
DWORD WINAPI UnifiedWorkerThread(LPVOID lpParam) {
#ifdef _DEBUG
    // Debug 模式记录 payload 启动信息。
    AppendPayloadDebugLog(L"payload 线程启动");
#endif
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

#ifdef _DEBUG
    AppendPayloadDebugLog(L"payload 模块启动完成");
#endif
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_selfModule = hModule;
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
        // 进程退出时清理 successfile 等轻量资源。
        if (g_IsInitialized.load()) {
            try {
                Sync::Stop();
            } catch (...) {
            }
            try {
                Helper::Stop();
            } catch (...) {
            }
        }
        break;
    }
    return TRUE;
}
