// Payload/dllmain.cpp
#include "pch.h"
#include <atomic>
#include <string>
#include "runtime/payload_runtime.h"

#ifdef _DEBUG
#include <vector>
#include <cwctype>
#endif

// 声明外部模块的启动接口 (在 namespace 中)
namespace Helper { void Start(); void Stop(); }
namespace Sync   { void Start(); void Stop(); }

// 原子锁：APC 注入可能会触发多次 LoadLibrary，必须防止重复初始化
std::atomic<bool> g_IsInitialized(false);
static HMODULE g_selfModule = nullptr;
static bool g_enableHelper = true;
static bool g_enableSync = true;
static bool g_enableStealth = false;
static bool g_strictStartupRollback = true;
static std::wstring g_payloadSuccessFilePath;
static bool g_payloadSuccessWritten = false;
static volatile LONG g_syncStarted = 0;
static volatile LONG g_helperStarted = 0;

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

static std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
    {
        return right;
    }
    if (right.empty())
    {
        return left;
    }
    std::wstring result = left;
    wchar_t last = result[result.size() - 1];
    if (last != L'\\' && last != L'/')
    {
        result.push_back(L'\\');
    }
    result.append(right);
    return result;
}

static bool ReadIniBool(const std::wstring& path, const wchar_t* key, bool defaultValue)
{
    wchar_t buffer[16] = {0};
    DWORD read = GetPrivateProfileStringW(L"payload", key, defaultValue ? L"true" : L"false", buffer,
        static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());
    if (read == 0)
    {
        return defaultValue;
    }
    if (_wcsicmp(buffer, L"1") == 0 || _wcsicmp(buffer, L"true") == 0 ||
        _wcsicmp(buffer, L"yes") == 0 || _wcsicmp(buffer, L"on") == 0)
    {
        return true;
    }
    if (_wcsicmp(buffer, L"0") == 0 || _wcsicmp(buffer, L"false") == 0 ||
        _wcsicmp(buffer, L"no") == 0 || _wcsicmp(buffer, L"off") == 0)
    {
        return false;
    }
    return defaultValue;
}

static void LoadPayloadConfig()
{
    std::wstring baseDir = GetModuleDirectory(g_selfModule);
    std::wstring configPath = JoinPath(JoinPath(baseDir, L"config"), L"payload.ini");
    g_enableHelper = ReadIniBool(configPath, L"EnableHelper", true);
    g_enableSync = ReadIniBool(configPath, L"EnableSync", true);
    g_enableStealth = ReadIniBool(configPath, L"EnableStealth", false);
    g_strictStartupRollback = ReadIniBool(configPath, L"StrictStartupRollback", true);
}

// 在启动失败时回滚已启动模块，避免部分成功导致状态不一致。
static void RollbackStartedModules(bool stopSync, bool stopHelper)
{
    if (stopHelper)
    {
        try
        {
            Helper::Stop();
        }
        catch (...)
        {
            OutputDebugStringA("[Unified] Helper rollback failed.");
        }
        InterlockedExchange(&g_helperStarted, 0);
    }

    if (stopSync)
    {
        try
        {
            Sync::Stop();
        }
        catch (...)
        {
            OutputDebugStringA("[Unified] Sync rollback failed.");
        }
        InterlockedExchange(&g_syncStarted, 0);
    }
}

static std::wstring BuildPayloadSuccessFilePath()
{
    std::wstring baseDir = GetModuleDirectory(g_selfModule);
    std::wstring logDir = JoinPath(baseDir, L"logs");
    CreateDirectoryW(logDir.c_str(), nullptr);

    wchar_t modulePath[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(g_selfModule, modulePath, MAX_PATH);
    std::wstring fileName = L"game-payload";
    if (length > 0 && length < MAX_PATH)
    {
        wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
        if (!lastSlash)
        {
            lastSlash = wcsrchr(modulePath, L'/');
        }
        fileName = lastSlash ? (lastSlash + 1) : modulePath;
        size_t dot = fileName.rfind(L'.');
        if (dot != std::wstring::npos)
        {
            fileName = fileName.substr(0, dot);
        }
    }

    std::wstring successName = L"successfile_" + fileName + L"_" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    return JoinPath(logDir, successName);
}

static void WritePayloadSuccessFile()
{
    if (g_payloadSuccessWritten)
    {
        return;
    }

    g_payloadSuccessFilePath = BuildPayloadSuccessFilePath();
    HANDLE file = CreateFileW(
        g_payloadSuccessFilePath.c_str(),
        FILE_GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t buffer[64] = {0};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(wcslen(buffer) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
    g_payloadSuccessWritten = true;
}

static void RemovePayloadSuccessFile()
{
    std::wstring path = g_payloadSuccessFilePath;
    if (path.empty())
    {
        path = BuildPayloadSuccessFilePath();
    }
    if (path.empty())
    {
        return;
    }
    DeleteFileW(path.c_str());
}

#ifdef _DEBUG
static std::wstring ReadPayloadSessionId(const std::wstring& baseDir)
{
    std::wstring sessionFile = baseDir + L"\\logs\\session.current";
    HANDLE file = CreateFileW(
        sessionFile.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return L"";
    }
    char buffer[64] = {0};
    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0)
    {
        return L"";
    }
    buffer[read] = '\0';
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, nullptr, 0);
    if (wlen <= 1)
    {
        return L"";
    }
    std::wstring wide(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buffer, -1, &wide[0], wlen - 1);
    // 简单去空白
    while (!wide.empty() && iswspace(wide.front()))
    {
        wide.erase(wide.begin());
    }
    while (!wide.empty() && iswspace(wide.back()))
    {
        wide.pop_back();
    }
    return wide;
}

static std::wstring BuildPayloadSessionIdNow()
{
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t buffer[32] = {0};
    swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return std::wstring(buffer);
}

static std::wstring g_payloadDebugLogPath;

static void ArchivePayloadDebugLog()
{
    if (g_payloadDebugLogPath.empty())
    {
        return;
    }
    std::wstring baseDir = GetModuleDirectory(g_selfModule);
    if (baseDir.empty())
    {
        return;
    }
    std::wstring sessionId = ReadPayloadSessionId(baseDir);
    if (sessionId.empty())
    {
        return;
    }
    std::wstring logsDir = baseDir + L"\\logs";
    std::wstring sessionDir = logsDir + L"\\session_" + sessionId;
    CreateDirectoryW(logsDir.c_str(), nullptr);
    CreateDirectoryW(sessionDir.c_str(), nullptr);

    size_t pos = g_payloadDebugLogPath.find_last_of(L"\\/");
    std::wstring fileName = (pos == std::wstring::npos) ? g_payloadDebugLogPath : g_payloadDebugLogPath.substr(pos + 1);
    if (fileName.empty())
    {
        return;
    }
    std::wstring dest = sessionDir + L"\\" + fileName;
    CopyFileW(g_payloadDebugLogPath.c_str(), dest.c_str(), FALSE);
}

static void AppendPayloadDebugLog(const std::wstring& message)
{
    std::wstring baseDir = GetModuleDirectory(g_selfModule);
    std::wstring logDir = baseDir + L"\\logs\\payload";
    CreateDirectoryW((baseDir + L"\\logs").c_str(), nullptr);
    CreateDirectoryW(logDir.c_str(), nullptr);
    std::wstring sessionId = ReadPayloadSessionId(baseDir);
    if (sessionId.empty())
    {
        sessionId = BuildPayloadSessionIdNow();
    }
    std::wstring logPath = logDir + L"\\payload_" + sessionId + L"_" + std::to_wstring(GetCurrentProcessId()) + L".log";
    if (g_payloadDebugLogPath.empty())
    {
        g_payloadDebugLogPath = logPath;
    }

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
    UNREFERENCED_PARAMETER(lpParam);
#ifdef _DEBUG
    // Debug 模式记录 payload 启动信息。
    AppendPayloadDebugLog(L"payload 线程启动");
#endif
    PayloadRuntime::SetInitState(PayloadRuntime::InitState::kLoadingConfig);
    LoadPayloadConfig();
    PayloadRuntime::SetStealthEnabled(g_enableStealth);
    // 即使模块被禁用也需要写入 successfile，保证注入判定一致。
    WritePayloadSuccessFile();
#ifdef _DEBUG
    AppendPayloadDebugLog(std::wstring(L"配置载入：EnableSync=") + (g_enableSync ? L"true" : L"false") +
        L" EnableHelper=" + (g_enableHelper ? L"true" : L"false") +
        L" EnableStealth=" + (g_enableStealth ? L"true" : L"false") +
        L" StrictStartupRollback=" + (g_strictStartupRollback ? L"true" : L"false"));
#endif

    bool syncStarted = false;
    bool helperStarted = false;
    bool syncStartFailed = false;
    bool helperStartFailed = false;

    // 1. 启动同步模块 (通常 Hook 底层输入建议先启动)
    PayloadRuntime::SetInitState(PayloadRuntime::InitState::kStartingSync);
    try {
        if (g_enableSync)
        {
            Sync::Start();
            syncStarted = true;
            InterlockedExchange(&g_syncStarted, 1);
        }
        else
        {
            OutputDebugStringW(L"[Unified] Sync 模块已禁用");
        }
    } catch (...) {
        OutputDebugStringA("[Unified] Sync module failed to start.");
        syncStartFailed = true;
    }

    // 2. 启动辅助模块 (Helper)
    PayloadRuntime::SetInitState(PayloadRuntime::InitState::kStartingHelper);
    try {
        if (g_enableHelper)
        {
            Helper::Start();
            helperStarted = true;
            InterlockedExchange(&g_helperStarted, 1);
        }
        else
        {
            OutputDebugStringW(L"[Unified] Helper 模块已禁用");
        }
    } catch (...) {
        OutputDebugStringA("[Unified] Helper module failed to start.");
        helperStartFailed = true;
    }

    const bool hasStartFailure =
        (g_enableSync && (syncStartFailed || !syncStarted)) ||
        (g_enableHelper && (helperStartFailed || !helperStarted));

    if (hasStartFailure && g_strictStartupRollback)
    {
        OutputDebugStringA("[Unified] Startup failed, strict rollback is enabled.");
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kDegraded);
        RollbackStartedModules(syncStarted, helperStarted);
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kStopped);
#ifdef _DEBUG
        AppendPayloadDebugLog(L"启动失败，已执行严格回滚并停止");
#endif
    }
    else if (hasStartFailure)
    {
        OutputDebugStringA("[Unified] Startup failed, run in degraded mode.");
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kDegraded);
#ifdef _DEBUG
        AppendPayloadDebugLog(L"启动失败，进入降级运行模式");
#endif
    }
    else
    {
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kRunning);
#ifdef _DEBUG
        AppendPayloadDebugLog(L"启动成功，进入运行状态");
#endif
    }

#ifdef _DEBUG
    AppendPayloadDebugLog(
        std::wstring(L"payload 模块启动完成，状态=") +
        PayloadRuntime::InitStateToText(PayloadRuntime::GetInitState()));
#endif
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_selfModule = hModule;
        InterlockedExchange(&g_syncStarted, 0);
        InterlockedExchange(&g_helperStarted, 0);
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kNotStarted);
        // 核心改动：使用原子操作确保只执行一次
        // expected 为 false，如果 g_IsInitialized 是 false，则将其设为 true 并返回 true
        // 否则返回 false (代表已经被其他线程初始化了)
        {
            bool expected = false;
            if (g_IsInitialized.compare_exchange_strong(expected, true)) {
                DisableThreadLibraryCalls(hModule);
                
                // 必须在独立线程中初始化，避免卡死 DllMain (Loader Lock)
                HANDLE thread = CreateThread(NULL, 0, UnifiedWorkerThread, NULL, 0, NULL);
                if (thread != NULL)
                {
                    CloseHandle(thread);
                }
                else
                {
                    PayloadRuntime::SetInitState(PayloadRuntime::InitState::kDegraded);
                    OutputDebugStringA("[Unified] Failed to create initialization thread.");
                }
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kStopping);
        // 进程退出时清理 successfile 等轻量资源。
        try {
            if (InterlockedCompareExchange(&g_syncStarted, 0, 0) == 1)
            {
                Sync::Stop();
                InterlockedExchange(&g_syncStarted, 0);
            }
        } catch (...) {
        }
        try {
            if (InterlockedCompareExchange(&g_helperStarted, 0, 0) == 1)
            {
                Helper::Stop();
                InterlockedExchange(&g_helperStarted, 0);
            }
        } catch (...) {
        }
#ifdef _DEBUG
        ArchivePayloadDebugLog();
#endif
        RemovePayloadSuccessFile();
        PayloadRuntime::SetInitState(PayloadRuntime::InitState::kStopped);
        break;
    }
    return TRUE;
}
