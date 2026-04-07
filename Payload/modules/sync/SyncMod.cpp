#define CINTERFACE
#define COBJMACROS

#include <windows.h>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <objbase.h>
#include <strsafe.h>
#include <intrin.h>
#include <string>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <cwctype>

#include "MinHook.h"
#include "game_payload_core_ffi.h"

// ------------------------------
// 全局状态与计数器
// ------------------------------

static HMODULE g_module = nullptr;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static LONG g_logReady = 0;
static LONG g_shouldStop = 0;
static std::wstring g_logPath;
static std::wstring g_successFilePath;
static LONG g_successFileCreated = 0;

static volatile LONG g_countGetAsyncKeyState = 0;
static volatile LONG g_countGetKeyboardState = 0;
static volatile LONG g_countDirectInput8Create = 0;
static volatile LONG g_countCreateDevice = 0;
static volatile LONG g_countGetDeviceState = 0;
static volatile LONG g_countGetDeviceStateFailed = 0;
static volatile LONG g_countGetDeviceStateNotAcquired = 0;
static volatile LONG g_countGetDeviceData = 0;
static volatile LONG g_countAcquire = 0;
static volatile LONG g_countPoll = 0;
static volatile LONG g_countUnacquire = 0;
static volatile LONG g_countRegisterRawInput = 0;
static volatile LONG g_countGetRawInputData = 0;
static volatile LONG g_countGetRawInputBuffer = 0;
static volatile LONG g_countSpoofRawInputData = 0;
static volatile LONG g_countSpoofRawInputBuffer = 0;
static volatile LONG g_countGetMessage = 0;
static volatile LONG g_countPeekMessage = 0;
static volatile LONG g_countSpoofWmInput = 0;
static volatile LONG g_countGetForegroundWindow = 0;
static volatile LONG g_countGetActiveWindow = 0;
static volatile LONG g_countGetFocus = 0;
static volatile LONG g_countSpoofFocus = 0;

// 按键事件状态（诊断用，默认关闭）
static CRITICAL_SECTION g_keyStateLock;
static LONG g_keyStateLockReady = 0;
static BYTE g_keyDownState[256] = {};
static DWORD g_keyDownTick[256] = {};
static DWORD g_keyUpTick[256] = {};
static DWORD g_keyWarnTick[256] = {};
static BYTE g_keyLastChannel[256] = {};
static BYTE g_lastWin32State[256] = {};
static BYTE g_lastDIState[256] = {};
static BYTE g_lastLogicalDesiredState[256] = {};

static LONG g_createDeviceHooked = 0;
static LONG g_deviceHooksHooked = 0;

// ------------------------------
// 共享内存与伪造配置
// ------------------------------

static const wchar_t* kSharedMemoryName = L"Local\\DNFSyncBox.KeyboardState.V2";
static const uint32_t kSharedVersion = 2;
static const uint32_t kFlagPaused = 0x1;
static const uint32_t kFlagClear = 0x2;
// 共享内存心跳超时（毫秒），可通过环境变量覆盖。
static const wchar_t* kSharedTimeoutEnvName = L"DNFSYNC_HEARTBEAT_TIMEOUT_MS";
static const DWORD kDefaultSharedTimeoutMs = 200;
// 共享内存快照缓存（毫秒），降低高频 Hook 读取成本。
static const wchar_t* kSnapshotCacheEnvName = L"DNFSYNC_SNAPSHOT_CACHE_MS";
static const DWORD kDefaultSnapshotCacheMs = 2;
// RawInput 日志开关与采样间隔。
static const wchar_t* kRawInputLogEnvName = L"DNFSYNC_RAWINPUT_LOG";
static const wchar_t* kRawInputLogIntervalEnvName = L"DNFSYNC_RAWINPUT_LOG_INTERVAL_MS";
// 统计日志开关与输出间隔。
static const wchar_t* kStatsEnvName = L"DNFSYNC_STATS";
static const wchar_t* kStatsIntervalEnvName = L"DNFSYNC_STATS_INTERVAL_MS";
// 诊断日志开关与输出间隔。
static const wchar_t* kDiagEnvName = L"DNFSYNC_DIAG";
static const wchar_t* kDiagIntervalEnvName = L"DNFSYNC_DIAG_INTERVAL_MS";
// 按键事件日志开关与输出间隔。
static const wchar_t* kKeyLogEnvName = L"DNFSYNC_KEYLOG";
static const wchar_t* kKeyLogIntervalEnvName = L"DNFSYNC_KEYLOG_INTERVAL_MS";
static const wchar_t* kKeyLogLevelEnvName = L"DNFSYNC_KEYLOG_LEVEL";
static const wchar_t* kKeyUpTimeoutEnvName = L"DNFSYNC_KEYUP_TIMEOUT_MS";
// 与控制端 KeyboardProfileMode 枚举保持一致（Blacklist=2，Mapping=3）。
static const uint32_t kProfileModeMapping = 3;
// 伪造延迟（毫秒），用于在注入后短暂关闭输入伪造以降低崩溃风险。
static const wchar_t* kSpoofDelayEnvName = L"DNFSYNC_SPOOF_DELAY_MS";
static const DWORD kDefaultSpoofDelayMs = 5000;

#pragma pack(push, 1)
struct SharedKeyboardStateV2
{
    uint32_t version;
    uint32_t seq;
    uint32_t flags;
    uint32_t activePid;
    uint32_t profileId;
    uint32_t profileMode;
    uint64_t lastTick;
    uint8_t keyboardState[256];
    uint32_t edgeCounter[256];
    uint8_t targetMask[256];
    uint8_t blockMask[256];
};

#pragma pack(pop)

struct SharedSnapshot
{
    uint32_t seq;
    uint32_t flags;
    uint32_t activePid;
    uint32_t profileId;
    uint32_t profileMode;
    uint64_t lastTick;
    uint8_t keyboardState[256];
    uint32_t edgeCounter[256];
    uint8_t targetMask[256];
    uint8_t blockMask[256];
};

struct SharedSnapshotLite
{
    uint32_t seq;
    uint32_t flags;
    uint32_t activePid;
    uint32_t profileId;
    uint32_t profileMode;
    uint64_t lastTick;
};

static HANDLE g_sharedMapping = nullptr;
static SharedKeyboardStateV2* g_sharedState = nullptr;
static DWORD g_lastSharedAttemptTick = 0;
static LONG g_sharedReadyLogged = 0;
static LONG g_sharedErrorLogged = 0;
static LONG g_sharedVersionLogged = 0;
static LONG g_sharedSizeLogged = 0;
static uint32_t g_lastEdgeCounter[256] = {};
static uint8_t g_lastRawKeyboardState[256] = {};
static uint32_t g_lastProfileId = 0;
static uint32_t g_lastProfileMode = 0;
static uint32_t g_lastClearSeq = 0;
static uint32_t g_lastRawClearSeq = 0;
static volatile LONG g_countSpoofAsync = 0;
static volatile LONG g_countSpoofKeyboard = 0;
static volatile LONG g_countSpoofDeviceState = 0;
static int g_rawScanCursor = 0;
static LONG g_seenRawInputBuffer = 0;

static int g_vkeyToDik[256] = {};
static LONG g_vkeyMapReady = 0;
static LONG g_forceDeviceStateOk = -1;
static LONG g_forceDeviceStateLogged = 0;
static LONG g_sharedTimeoutMs = -1;
static LONG g_snapshotCacheMs = -1;
static LONG g_rawInputLogEnabled = -1;
static LONG g_rawInputLogIntervalMs = -1;
static LONGLONG g_rawInputLogLastTick = 0;
static LONG g_statsEnabled = -1;
static LONG g_statsIntervalMs = -1;
static LONG g_diagEnabled = -1;
static LONG g_diagIntervalMs = -1;
static LONGLONG g_keyLogLastTick = 0;
static LONG g_keyLogEnabled = -1;
static LONG g_keyLogIntervalMs = -1;
static LONG g_keyLogLevel = -1;
static LONG g_keyUpTimeoutMs = -1;
static LONG g_debugConfigLoaded = 0;
static LONG g_cfgRawInputLog = -1;
static LONG g_cfgRawInputLogIntervalMs = -1;
static LONG g_cfgStatsEnabled = -1;
static LONG g_cfgStatsIntervalMs = -1;
static LONG g_cfgDiagEnabled = -1;
static LONG g_cfgDiagIntervalMs = -1;
static LONG g_cfgKeyLogEnabled = -1;
static LONG g_cfgKeyLogIntervalMs = -1;
static LONG g_cfgKeyLogLevel = -1;
static LONG g_cfgKeyUpTimeoutMs = -1;
static LONG g_cfgSpoofDelayMs = -1;
static const uint8_t kDirectionConvergenceExtraReleasePulses = 2;
static thread_local void* t_directionConvergenceState = nullptr;
static thread_local BYTE t_directionLastState[256] = {};
static thread_local BYTE t_forceReleaseMask[256] = {};

// 窗口缓存只由后台线程更新，Hook 回调仅做只读访问
static volatile HWND g_selfWindowCache = nullptr;

static void SetSelfWindowCache(HWND hwnd)
{
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_selfWindowCache), hwnd);
}

static HWND GetSelfWindowCache()
{
    return reinterpret_cast<HWND>(
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_selfWindowCache), nullptr, nullptr));
}
static ULONGLONG g_injectTick = 0;
static LONG g_spoofDelayMs = -1;
static LONG g_spoofDelayLogged = 0;
static LONG g_spoofDelayEndLogged = 0;

static DWORD GetSharedTimeoutMs();
static DWORD GetSnapshotCacheMs();
static bool IsRawInputLogEnabled();
static bool ShouldLogRawInput();
static bool IsDiagEnabled();
static DWORD GetDiagIntervalMs();
static bool IsKeyLogEnabled();
static DWORD GetKeyLogIntervalMs();
static LONG GetKeyLogLevel();
static DWORD GetKeyUpTimeoutMs();
static bool IsSnapshotAlive(const SharedSnapshot& snapshot);
static bool IsSnapshotAliveLite(const SharedSnapshotLite& snapshot);
static bool IsBypassProcess(const SharedSnapshot& snapshot);
static bool IsBypassProcessLite(const SharedSnapshotLite& snapshot);
static bool ShouldBlockKey(const SharedSnapshot& snapshot, int vKey, bool alive, bool paused);
static void LogKeyFix(const wchar_t* reason, int vKey, bool isDown);
static bool IsDirectionVKey(int vKey);
static int GetDirectionPairVKey(int vKey);
static bool ShouldLogKeyEvent();
static std::wstring BuildDebugConfigPath();
static void LoadDebugConfig();
static bool EvaluateRuntimeDecision(const SharedSnapshot& snapshot, PayloadRuntimeDecisionInterop& decision);
static void LogDirectionDecision(const wchar_t* action, int vKey, bool snapshotDown, bool rawDownBefore, bool win32DownBefore, const wchar_t* reason);
static void LogLogicalEdge(int vKey, bool desiredDown, bool pressedEdge, bool releasedEdge);
static void LogAdapterEmit(const wchar_t* adapter, int vKey, int emitAction, bool beforeDown, bool afterDown, const wchar_t* reason);
static void LogDirectionGroupDecision(const wchar_t* pairName, int winnerVKey, int loserVKey, const wchar_t* reason);
static void LogRepeatSuppressed(const wchar_t* adapter, int vKey);
static void LogPauseInterception(const wchar_t* adapter, int vKey, bool hadProjectedDown, const wchar_t* reason);
static bool EvaluateRuntimeDecision(const SharedSnapshotLite& snapshot, PayloadRuntimeDecisionInterop& decision);
static bool EvaluateKeyDecision(const SharedSnapshot& snapshot, int vKey, PayloadKeyDecisionInterop& decision);
static bool EvaluateLogicalKeyDecision(const SharedSnapshot& snapshot, int vKey, PayloadLogicalKeyDecisionInterop& decision);
static bool EvaluateChannelEmitDecision(
    const SharedSnapshot& snapshot,
    int vKey,
    bool projectedDownBefore,
    bool observedDown,
    PayloadLogicalKeyDecisionInterop& logicalDecision,
    PayloadChannelEmitDecisionInterop& emitDecision);
static bool EvaluatePathDecision(const SharedSnapshot& snapshot, PayloadPathDecisionInterop& decision);
static bool EvaluateInputPathObservation(
    LONG rawPromoted,
    LONG rawData,
    LONG rawBuffer,
    LONG diState,
    LONG diData,
    LONG win32Async,
    LONG win32Keyboard,
    PayloadInputPathObservationInterop& observation);
static bool EvaluateAdapterProjectedState(
    bool desiredDown,
    bool rawProjected,
    bool win32Projected,
    bool directInputProjected,
    PayloadAdapterProjectedStateInterop& state);
static void SyncDirectionConvergenceState(void* convergenceState, BYTE lastDirectionState[256], const SharedSnapshot& snapshot);
static void EnsureDirectionConvergenceState();
static bool ShouldForceReleaseKey(int vKey);
static bool TryPickLogicalRawTransition(
    const SharedSnapshot& snapshot,
    int preferredVKey,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut);
static bool TryPickMappingRawTransition(
    const SharedSnapshot& snapshot,
    bool alive,
    bool paused,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut);
static bool TryPickSilentRawTransition(
    const SharedSnapshot& snapshot,
    int preferredVKey,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut);
static bool ShouldPromoteRawKeyboardMessage(HRAWINPUT hRawInput);

static SIZE_T GetViewRegionSize(void* view)
{
    if (!view)
    {
        return 0;
    }
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(view, &mbi, sizeof(mbi)) == 0)
    {
        return 0;
    }
    return mbi.RegionSize;
}

// ------------------------------
// MinHook 目标函数指针
// ------------------------------

using GetAsyncKeyState_t = SHORT(WINAPI*)(int);
static GetAsyncKeyState_t g_origGetAsyncKeyState = nullptr;

using GetKeyboardState_t = BOOL(WINAPI*)(PBYTE);
static GetKeyboardState_t g_origGetKeyboardState = nullptr;

using RegisterRawInputDevices_t = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
static RegisterRawInputDevices_t g_origRegisterRawInputDevices = nullptr;

using GetRawInputData_t = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static GetRawInputData_t g_origGetRawInputData = nullptr;

using GetRawInputBuffer_t = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
static GetRawInputBuffer_t g_origGetRawInputBuffer = nullptr;

using GetMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
static GetMessageW_t g_origGetMessageW = nullptr;

using GetMessageA_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
static GetMessageA_t g_origGetMessageA = nullptr;

using PeekMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageW_t g_origPeekMessageW = nullptr;

using PeekMessageA_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageA_t g_origPeekMessageA = nullptr;

using GetForegroundWindow_t = HWND(WINAPI*)();
static GetForegroundWindow_t g_origGetForegroundWindow = nullptr;

using GetActiveWindow_t = HWND(WINAPI*)();
static GetActiveWindow_t g_origGetActiveWindow = nullptr;

using GetFocus_t = HWND(WINAPI*)();
static GetFocus_t g_origGetFocus = nullptr;

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_origDirectInput8Create = nullptr;

using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8W*, REFGUID, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
static CreateDevice_t g_origCreateDevice = nullptr;

using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
static GetDeviceState_t g_origGetDeviceState = nullptr;

using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static GetDeviceData_t g_origGetDeviceData = nullptr;

using Acquire_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*);
static Acquire_t g_origAcquire = nullptr;

using Unacquire_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*);
static Unacquire_t g_origUnacquire = nullptr;

using Poll_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*);
static Poll_t g_origPoll = nullptr;

// ------------------------------
// 日志工具（UTF-8）
// ------------------------------

static std::wstring GetModuleDirectory()
{
    std::wstring baseDir;
    wchar_t modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(g_module, modulePath, ARRAYSIZE(modulePath));
    if (len > 0 && len < ARRAYSIZE(modulePath))
    {
        for (DWORD i = len; i > 0; i--)
        {
            if (modulePath[i - 1] == L'\\' || modulePath[i - 1] == L'/')
            {
                modulePath[i - 1] = L'\0';
                break;
            }
        }
        baseDir = modulePath;
    }

    if (baseDir.empty())
    {
        wchar_t currentDir[MAX_PATH] = {0};
        if (GetCurrentDirectoryW(ARRAYSIZE(currentDir), currentDir) > 0)
        {
            baseDir = currentDir;
        }
        else
        {
            baseDir = L".";
        }
    }

    return baseDir.empty() ? L"." : baseDir;
}

static std::wstring TrimWide(const std::wstring& value)
{
    size_t start = 0;
    while (start < value.size() && iswspace(value[start]))
    {
        start++;
    }
    size_t end = value.size();
    while (end > start && iswspace(value[end - 1]))
    {
        end--;
    }
    return value.substr(start, end - start);
}

static std::wstring ReadSessionId()
{
    std::wstring baseDir = GetModuleDirectory();
    std::wstring sessionPath = baseDir + L"\\logs\\session.current";
    HANDLE file = CreateFileW(sessionPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    return TrimWide(wide);
}

static std::wstring BuildSessionIdNow()
{
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t buffer[32] = {0};
    swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return std::wstring(buffer);
}

static std::wstring BuildLogPath()
{
    // 日志统一输出到 DLL 所在目录的 logs\\sync\\payload 子目录。
    std::wstring baseDir = GetModuleDirectory();
    std::wstring logDir = baseDir + L"\\logs\\sync\\payload";
    CreateDirectoryW((baseDir + L"\\logs").c_str(), nullptr);
    CreateDirectoryW((baseDir + L"\\logs\\sync").c_str(), nullptr);
    CreateDirectoryW(logDir.c_str(), nullptr);

    std::wstring sessionId = ReadSessionId();
    if (sessionId.empty())
    {
        sessionId = BuildSessionIdNow();
    }

    wchar_t fileName[MAX_PATH] = {0};
    StringCchPrintfW(
        fileName,
        ARRAYSIZE(fileName),
        L"%s\\sync_payload_%s_%lu.log",
        logDir.c_str(),
        sessionId.c_str(),
        GetCurrentProcessId());
    return std::wstring(fileName);
}

static std::wstring GetFileNameFromPath(const std::wstring& path)
{
    if (path.empty())
    {
        return L"";
    }
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return path;
    }
    return path.substr(pos + 1);
}

static void ArchiveLogFile()
{
    if (g_logPath.empty())
    {
        return;
    }

    std::wstring sessionId = ReadSessionId();
    if (sessionId.empty())
    {
        return;
    }

    std::wstring baseDir = GetModuleDirectory();
    std::wstring sessionDir = baseDir + L"\\logs\\session_" + sessionId;
    CreateDirectoryW(sessionDir.c_str(), nullptr);

    std::wstring fileName = GetFileNameFromPath(g_logPath);
    if (fileName.empty())
    {
        return;
    }

    std::wstring dest = sessionDir + L"\\" + fileName;
    CopyFileW(g_logPath.c_str(), dest.c_str(), FALSE);
}

static std::wstring GetModuleBaseName()
{
    wchar_t modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(g_module, modulePath, ARRAYSIZE(modulePath));
    if (len == 0 || len >= ARRAYSIZE(modulePath))
    {
        return L"dnfinput";
    }

    const wchar_t* fileName = modulePath;
    for (DWORD i = 0; i < len; i++)
    {
        if (modulePath[i] == L'\\' || modulePath[i] == L'/')
        {
            fileName = modulePath + i + 1;
        }
    }

    std::wstring name(fileName);
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos)
    {
        name = name.substr(0, dot);
    }

    for (auto& ch : name)
    {
        ch = static_cast<wchar_t>(towlower(ch));
    }

    return name.empty() ? L"dnfinput" : name;
}

static std::wstring BuildSuccessFilePath()
{
    // success file 统一放到 logs 子目录，避免和其他产物混放。
    std::wstring baseDir = GetModuleDirectory();
    std::wstring logDir = baseDir + L"\\logs";
    // 尝试创建目录（允许已存在）
    CreateDirectoryW(logDir.c_str(), nullptr);
    std::wstring dllName = GetModuleBaseName();
    std::wstring fileName = L"successfile_" + dllName + L"_" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    return logDir + L"\\" + fileName;
}

static std::wstring BuildDebugConfigPath()
{
    // 调试配置统一放到 DLL 所在目录的 config 子目录。
    std::wstring baseDir = GetModuleDirectory();
    std::wstring configDir = baseDir + L"\\config";
    return configDir + L"\\sync_debug.ini";
}

// 提前声明，避免在 success file 写入处触发未声明错误
static void WriteUtf8BomIfEmpty(HANDLE file);
static std::wstring GetTimestamp();
static std::string WideToUtf8(const std::wstring& input);

static void TrimWhitespace(wchar_t* text)
{
    if (!text)
    {
        return;
    }
    size_t len = wcslen(text);
    size_t start = 0;
    while (start < len && iswspace(text[start]))
    {
        start++;
    }
    size_t end = len;
    while (end > start && iswspace(text[end - 1]))
    {
        end--;
    }
    if (start > 0 && start < len)
    {
        memmove(text, text + start, (end - start) * sizeof(wchar_t));
    }
    text[end - start] = L'\0';
}

enum class KeyChannel : uint8_t
{
    RawInputData = 1,
    RawInputBuffer = 2,
    Win32 = 3,
    DirectInput = 4
};

static const wchar_t* KeyChannelToText(KeyChannel channel)
{
    switch (channel)
    {
    case KeyChannel::RawInputData:
        return L"RawInputData";
    case KeyChannel::RawInputBuffer:
        return L"RawInputBuffer";
    case KeyChannel::Win32:
        return L"Win32";
    case KeyChannel::DirectInput:
        return L"DirectInput";
    default:
        return L"Unknown";
    }
}

static void EnsureKeyStateLock()
{
    if (InterlockedCompareExchange(&g_keyStateLockReady, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_keyStateLock);
    }
}

static bool TryParseBool(const wchar_t* text, bool* valueOut)
{
    if (!text || !valueOut)
    {
        return false;
    }
    while (*text && iswspace(*text))
    {
        text++;
    }
    if (*text == L'\0')
    {
        return false;
    }

    if (_wcsicmp(text, L"1") == 0 || _wcsicmp(text, L"true") == 0 ||
        _wcsicmp(text, L"yes") == 0 || _wcsicmp(text, L"on") == 0)
    {
        *valueOut = true;
        return true;
    }
    if (_wcsicmp(text, L"0") == 0 || _wcsicmp(text, L"false") == 0 ||
        _wcsicmp(text, L"no") == 0 || _wcsicmp(text, L"off") == 0)
    {
        *valueOut = false;
        return true;
    }

    wchar_t* end = nullptr;
    long parsed = wcstol(text, &end, 10);
    if (end != text)
    {
        *valueOut = parsed != 0;
        return true;
    }

    return false;
}

static bool TryParseInt(const wchar_t* text, long* valueOut)
{
    if (!text || !valueOut)
    {
        return false;
    }
    while (*text && iswspace(*text))
    {
        text++;
    }
    if (*text == L'\0')
    {
        return false;
    }
    wchar_t* end = nullptr;
    long parsed = wcstol(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *valueOut = parsed;
    return true;
}

static bool ReadConfigValue(const std::wstring& path, const wchar_t* section, const wchar_t* key, wchar_t* out, DWORD outSize)
{
    if (!out || outSize == 0)
    {
        return false;
    }
    out[0] = L'\0';
    DWORD len = GetPrivateProfileStringW(section, key, L"", out, outSize, path.c_str());
    if (len == 0)
    {
        return false;
    }
    TrimWhitespace(out);
    return out[0] != L'\0';
}

static void LoadDebugConfig()
{
    if (InterlockedCompareExchange(&g_debugConfigLoaded, 1, 0) != 0)
    {
        return;
    }

    std::wstring path = BuildDebugConfigPath();
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }

    wchar_t value[64] = {0};
    bool boolValue = false;
    long intValue = 0;

    if (ReadConfigValue(path, L"Debug", L"RawInputLog", value, ARRAYSIZE(value)) &&
        TryParseBool(value, &boolValue))
    {
        g_cfgRawInputLog = boolValue ? 1 : 0;
    }
    if (ReadConfigValue(path, L"Debug", L"RawInputLogIntervalMs", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 0)
        {
            intValue = 0;
        }
        if (intValue > 5000)
        {
            intValue = 5000;
        }
        g_cfgRawInputLogIntervalMs = static_cast<LONG>(intValue);
    }
    if (ReadConfigValue(path, L"Debug", L"Stats", value, ARRAYSIZE(value)) &&
        TryParseBool(value, &boolValue))
    {
        g_cfgStatsEnabled = boolValue ? 1 : 0;
    }
    if (ReadConfigValue(path, L"Debug", L"StatsIntervalMs", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 0)
        {
            intValue = 0;
        }
        if (intValue > 60000)
        {
            intValue = 60000;
        }
        g_cfgStatsIntervalMs = static_cast<LONG>(intValue);
    }
    if (ReadConfigValue(path, L"Debug", L"Diag", value, ARRAYSIZE(value)) &&
        TryParseBool(value, &boolValue))
    {
        g_cfgDiagEnabled = boolValue ? 1 : 0;
    }
    if (ReadConfigValue(path, L"Debug", L"DiagIntervalMs", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 0)
        {
            intValue = 0;
        }
        if (intValue > 60000)
        {
            intValue = 60000;
        }
        g_cfgDiagIntervalMs = static_cast<LONG>(intValue);
    }
    if (ReadConfigValue(path, L"Debug", L"SpoofDelayMs", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 0)
        {
            intValue = 0;
        }
        if (intValue > 600000)
        {
            intValue = 600000;
        }
        g_cfgSpoofDelayMs = static_cast<LONG>(intValue);
    }
    if (ReadConfigValue(path, L"Debug", L"KeyLog", value, ARRAYSIZE(value)) &&
        TryParseBool(value, &boolValue))
    {
        g_cfgKeyLogEnabled = boolValue ? 1 : 0;
    }
    if (ReadConfigValue(path, L"Debug", L"KeyLogIntervalMs", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 0)
        {
            intValue = 0;
        }
        if (intValue > 5000)
        {
            intValue = 5000;
        }
        g_cfgKeyLogIntervalMs = static_cast<LONG>(intValue);
    }
    if (ReadConfigValue(path, L"Debug", L"KeyLogLevel", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 1)
        {
            intValue = 1;
        }
        if (intValue > 3)
        {
            intValue = 3;
        }
        g_cfgKeyLogLevel = static_cast<LONG>(intValue);
    }
    if (ReadConfigValue(path, L"Debug", L"KeyUpTimeoutMs", value, ARRAYSIZE(value)) &&
        TryParseInt(value, &intValue))
    {
        if (intValue < 0)
        {
            intValue = 0;
        }
        if (intValue > 5000)
        {
            intValue = 5000;
        }
        g_cfgKeyUpTimeoutMs = static_cast<LONG>(intValue);
    }
}

// 注入成功后创建 success file，写入时间戳便于外部判断
static void WriteSuccessFile()
{
    if (InterlockedCompareExchange(&g_successFileCreated, 1, 1) == 1)
    {
        return;
    }

    g_successFilePath = BuildSuccessFilePath();
    HANDLE file = CreateFileW(
        g_successFilePath.c_str(),
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

    WriteUtf8BomIfEmpty(file);
    std::wstring line = GetTimestamp() + L"\r\n";
    std::string utf8 = WideToUtf8(line);
    if (!utf8.empty())
    {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    CloseHandle(file);
    InterlockedExchange(&g_successFileCreated, 1);
}

// 进程退出时清理 success file，避免残留误判
static void RemoveSuccessFile()
{
    if (g_successFilePath.empty())
    {
        return;
    }

    DeleteFileW(g_successFilePath.c_str());
}

static void WriteUtf8BomIfEmpty(HANDLE file)
{
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size))
    {
        return;
    }
    if (size.QuadPart != 0)
    {
        return;
    }
    const BYTE bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    WriteFile(file, bom, ARRAYSIZE(bom), &written, nullptr);
}

static std::string WideToUtf8(const std::wstring& input)
{
    if (input.empty())
    {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return std::string();
    }
    std::string output(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, &output[0], size - 1, nullptr, nullptr);
    return output;
}

static void WriteLogLine(const std::wstring& line)
{
    if (InterlockedCompareExchange(&g_logReady, 1, 1) != 1)
    {
        return;
    }

    EnterCriticalSection(&g_logLock);

    std::wstring withNewline = line + L"\r\n";
    std::string utf8 = WideToUtf8(withNewline);
    if (!utf8.empty())
    {
        DWORD written = 0;
        WriteFile(g_logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    LeaveCriticalSection(&g_logLock);
}

static std::wstring GetTimestamp()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t buffer[64] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
    return std::wstring(buffer);
}

static void LogInfo(const std::wstring& message)
{
    WriteLogLine(L"[INFO] " + GetTimestamp() + L" " + message);
}

static void LogError(const std::wstring& message)
{
    WriteLogLine(L"[ERROR] " + GetTimestamp() + L" " + message);
}

static void LogProtocolMismatch(const wchar_t* reason, uint32_t expected, uint64_t actual)
{
    wchar_t buffer[200] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"protocol_mismatch: %s expected=%lu actual=%llu",
        reason,
        expected,
        static_cast<unsigned long long>(actual));
    LogError(buffer);
}

static std::wstring AnsiToWide(const char* text)
{
    if (!text)
    {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 0)
    {
        return L"";
    }
    std::wstring output(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &output[0], size - 1);
    return output;
}

static void LogMinHookStatus(const std::wstring& action, MH_STATUS status)
{
    std::wstring detail = AnsiToWide(MH_StatusToString(status));
    LogInfo(action + L" -> " + detail);
}

static void InitializeLogging()
{
    InitializeCriticalSection(&g_logLock);

    std::wstring path = BuildLogPath();
    g_logPath = path;
    size_t slash = path.rfind(L'\\');
    if (slash != std::wstring::npos)
    {
        std::wstring logDir = path.substr(0, slash);
        // 允许目录已存在，失败时继续走默认写入逻辑。
        CreateDirectoryW(logDir.c_str(), nullptr);
    }
    g_logFile = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (g_logFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    WriteUtf8BomIfEmpty(g_logFile);
    InterlockedExchange(&g_logReady, 1);

    LogInfo(L"dnfinput 初始化日志文件成功");
    LogInfo(L"日志路径: " + path);
}

// ------------------------------
// 断链（LDR Unlink）与抹头
// ------------------------------

typedef struct _UNICODE_STRING_T
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING_T, *PUNICODE_STRING_T;

typedef struct _LDR_DATA_TABLE_ENTRY_T
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING_T FullDllName;
    UNICODE_STRING_T BaseDllName;
} LDR_DATA_TABLE_ENTRY_T, *PLDR_DATA_TABLE_ENTRY_T;

typedef struct _PEB_LDR_DATA_T
{
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA_T, *PPEB_LDR_DATA_T;

typedef struct _PEB_T
{
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PPEB_LDR_DATA_T Ldr;
} PEB_T, *PPEB_T;

static PPEB_T GetPeb()
{
#if defined(_M_IX86)
    return reinterpret_cast<PPEB_T>(__readfsdword(0x30));
#else
    return nullptr;
#endif
}

static bool UnlinkFromPeb(HMODULE module)
{
    // 目的：把自身从 PEB 模块链表移除，降低被枚举发现的概率
    PPEB_T peb = GetPeb();
    if (!peb || !peb->Ldr || !module)
    {
        return false;
    }

    LIST_ENTRY* head = &peb->Ldr->InLoadOrderModuleList;
    for (LIST_ENTRY* entry = head->Flink; entry != head; entry = entry->Flink)
    {
        auto* data = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY_T, InLoadOrderLinks);
        if (data && data->DllBase == module)
        {
            auto removeEntry = [](LIST_ENTRY* item)
            {
                if (!item || !item->Flink || !item->Blink)
                {
                    return;
                }
                item->Blink->Flink = item->Flink;
                item->Flink->Blink = item->Blink;
                item->Flink = item;
                item->Blink = item;
            };

            removeEntry(&data->InLoadOrderLinks);
            removeEntry(&data->InMemoryOrderLinks);
            removeEntry(&data->InInitializationOrderLinks);
            return true;
        }
    }

    return false;
}

static bool ErasePeHeader(HMODULE module)
{
    // 目的：清除 PE 头部特征，降低内存特征扫描命中率
    if (!module)
    {
        return false;
    }

    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }

    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS32>(reinterpret_cast<BYTE*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        return false;
    }

    SIZE_T headerSize = nt->OptionalHeader.SizeOfHeaders;
    if (headerSize == 0)
    {
        return false;
    }

    // 保守抹头：最大 4KB，避免影响过多内存区域
    const SIZE_T eraseSize = (headerSize > 0x1000) ? 0x1000 : headerSize;
    DWORD oldProtect = 0;
    if (!VirtualProtect(module, eraseSize, PAGE_READWRITE, &oldProtect))
    {
        return false;
    }

    SecureZeroMemory(module, eraseSize);
    VirtualProtect(module, eraseSize, oldProtect, &oldProtect);
    return true;
}

// ------------------------------
// 共享内存读取与快照
// ------------------------------

static bool EnsureSharedMemory()
{
    if (g_sharedState)
    {
        return true;
    }

    DWORD now = GetTickCount();
    if (now - g_lastSharedAttemptTick < 1000)
    {
        return false;
    }
    g_lastSharedAttemptTick = now;

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedMemoryName);
    if (!mapping)
    {
        if (InterlockedCompareExchange(&g_sharedErrorLogged, 1, 0) == 0)
        {
            LogError(L"共享内存未就绪，等待控制端启动");
        }
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SharedKeyboardStateV2));
    if (!view)
    {
        CloseHandle(mapping);
        return false;
    }

    SIZE_T regionSize = GetViewRegionSize(view);
    SIZE_T expectSize = sizeof(SharedKeyboardStateV2);
    if (regionSize < expectSize)
    {
        if (InterlockedCompareExchange(&g_sharedSizeLogged, 1, 0) == 0)
        {
            LogProtocolMismatch(L"size_mismatch", static_cast<uint32_t>(expectSize), regionSize);
        }
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        return false;
    }

    g_sharedMapping = mapping;
    g_sharedState = static_cast<SharedKeyboardStateV2*>(view);

    if (InterlockedCompareExchange(&g_sharedReadyLogged, 1, 0) == 0)
    {
        LogInfo(L"共享内存已连接");
    }

    return true;
}

static bool ReadSharedSnapshot(SharedSnapshot& snapshot)
{
    if (!EnsureSharedMemory() || !g_sharedState)
    {
        return false;
    }

    if (g_sharedState->version != kSharedVersion)
    {
        if (InterlockedCompareExchange(&g_sharedVersionLogged, 1, 0) == 0)
        {
            LogProtocolMismatch(L"version_mismatch", kSharedVersion, g_sharedState ? g_sharedState->version : 0);
        }
        return false;
    }

    const SharedKeyboardStateV2* snapshotState = g_sharedState;
    for (int i = 0; i < 3; i++)
    {
        uint32_t seq1 = snapshotState->seq;
        if ((seq1 & 1) != 0)
        {
            continue;
        }

        snapshot.seq = seq1;
        snapshot.flags = snapshotState->flags;
        snapshot.activePid = snapshotState->activePid;
        snapshot.profileId = snapshotState->profileId;
        snapshot.profileMode = snapshotState->profileMode;
        snapshot.lastTick = snapshotState->lastTick;
        memcpy(snapshot.keyboardState, snapshotState->keyboardState, sizeof(snapshot.keyboardState));
        memcpy(snapshot.edgeCounter, snapshotState->edgeCounter, sizeof(snapshot.edgeCounter));
        memcpy(snapshot.targetMask, snapshotState->targetMask, sizeof(snapshot.targetMask));
        memcpy(snapshot.blockMask, snapshotState->blockMask, sizeof(snapshot.blockMask));

        uint32_t seq2 = snapshotState->seq;
        if (seq1 == seq2 && (seq2 & 1) == 0)
        {
            g_lastProfileId = snapshot.profileId;
            g_lastProfileMode = snapshot.profileMode;
            return true;
        }
    }

    return false;
}

static void ApplyClearIfNeeded(const SharedSnapshot& snapshot)
{
    if ((snapshot.flags & kFlagClear) == 0)
    {
        return;
    }

    if (snapshot.seq == g_lastClearSeq)
    {
        return;
    }

    g_lastClearSeq = snapshot.seq;
    for (int i = 0; i < 256; i++)
    {
        g_lastEdgeCounter[i] = snapshot.edgeCounter[i];
        g_lastLogicalDesiredState[i] = 0;
    }
}

static void ApplyRawClearIfNeeded(const SharedSnapshot& snapshot)
{
    if ((snapshot.flags & kFlagClear) == 0)
    {
        return;
    }

    if (snapshot.seq == g_lastRawClearSeq)
    {
        return;
    }

    g_lastRawClearSeq = snapshot.seq;
    // 清键时重置 RawInput 伪造态，避免后台出现卡键或残留按下。
    memset(g_lastRawKeyboardState, 0, sizeof(g_lastRawKeyboardState));
    memset(g_lastWin32State, 0, sizeof(g_lastWin32State));
    memset(g_lastDIState, 0, sizeof(g_lastDIState));
}

static void BuildRawKeyboardEvent(int vKey, bool isDown, RAWKEYBOARD& keyboard)
{
    // MapVirtualKeyW 的扩展键会在高位标记，RawInput 需要 RI_KEY_E0/E1。
    UINT scan = MapVirtualKeyW(static_cast<UINT>(vKey), MAPVK_VK_TO_VSC_EX);
    USHORT flags = 0;
    // 某些扩展键（例如方向键）在部分环境下不会带 0x100，导致缺少 E0 标记。
    // 这里兜底补齐，避免后台把方向键当成小键盘键处理。
    bool isExtended = (scan & 0x100) != 0;
    if (!isExtended)
    {
        switch (vKey)
        {
            case VK_LEFT:
            case VK_UP:
            case VK_RIGHT:
            case VK_DOWN:
                isExtended = true;
                break;
        }
    }
    if (isExtended)
    {
        flags |= RI_KEY_E0;
    }
    if ((scan & 0x200) != 0)
    {
        flags |= RI_KEY_E1;
    }
    if (!isDown)
    {
        flags |= RI_KEY_BREAK;
    }

    keyboard.MakeCode = static_cast<USHORT>(scan & 0xFF);
    keyboard.Flags = flags;
    keyboard.Reserved = 0;
    keyboard.VKey = static_cast<USHORT>(vKey);
    keyboard.Message = isDown ? WM_KEYDOWN : WM_KEYUP;
    keyboard.ExtraInformation = 0;
}

static bool TryPickSilentRawTransition(
    const SharedSnapshot& snapshot,
    int preferredVKey,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut)
{
    (void)snapshot;
    if (!vKeyOut || !isDownOut)
    {
        return false;
    }

    auto chooseRelease = [&](int vKey, const wchar_t* reason) -> bool {
        if (vKey < 0 || vKey >= 256)
        {
            return false;
        }

        const bool rawDownBefore = (g_lastRawKeyboardState[vKey] & 0x80) != 0;
        if (!rawDownBefore)
        {
            return false;
        }

        g_lastRawKeyboardState[vKey] = 0;
        g_lastLogicalDesiredState[vKey] = 0;
        *vKeyOut = vKey;
        *isDownOut = false;
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        LogPauseInterception(L"RawInput", vKey, true, reason);
        return true;
    };

    if (preferredVKey >= 0 && preferredVKey < 256 && chooseRelease(preferredVKey, L"paused_release_preferred"))
    {
        return true;
    }

    if (IsDirectionVKey(preferredVKey))
    {
        const int pair = GetDirectionPairVKey(preferredVKey);
        if (pair >= 0 && chooseRelease(pair, L"paused_release_pair"))
        {
            return true;
        }
    }

    for (int vKey : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN})
    {
        if (chooseRelease(vKey, L"paused_release_direction"))
        {
            return true;
        }
    }

    for (int vKey = 0; vKey < 256; vKey++)
    {
        if (chooseRelease(vKey, L"paused_release_stale"))
        {
            return true;
        }
    }

    if (preferredVKey >= 0 && preferredVKey < 256)
    {
        g_lastLogicalDesiredState[preferredVKey] = 0;
        *vKeyOut = preferredVKey;
        *isDownOut = false;
        if (reasonOut)
        {
            *reasonOut = L"paused_neutralize";
        }
        LogPauseInterception(L"RawInput", preferredVKey, false, L"paused_neutralize");
        return true;
    }

    return false;
}

static bool TryPickMappingRawTransition(
    const SharedSnapshot& snapshot,
    bool alive,
    bool paused,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut)
{
    if (!vKeyOut || !isDownOut)
    {
        return false;
    }

    const bool allowDown = alive && !paused;
    int start = g_rawScanCursor & 0xFF;

    // 只在逻辑边沿变化时输出真实 transition，不再发送 neutral raw 或重复 down。
    for (int i = 0; i < 256; i++)
    {
        int idx = (start + i) & 0xFF;
        if (snapshot.targetMask[idx] == 0)
        {
            continue;
        }

        bool desiredDown = allowDown && (snapshot.keyboardState[idx] & 0x80) != 0;
        bool lastDown = (g_lastRawKeyboardState[idx] & 0x80) != 0;
        if (desiredDown != lastDown)
        {
            g_lastRawKeyboardState[idx] = desiredDown ? 0x80 : 0x00;
            g_rawScanCursor = (idx + 1) & 0xFF;
            *vKeyOut = idx;
            *isDownOut = desiredDown;
            if (reasonOut)
            {
                *reasonOut = desiredDown ? L"mapping_edge_down" : L"mapping_edge_up";
            }
            return true;
        }
    }

    if (allowDown)
    {
        bool hasActiveTarget = false;
        for (int i = 0; i < 256; i++)
        {
            int idx = (start + i) & 0xFF;
            if (snapshot.targetMask[idx] == 0)
            {
                continue;
            }

            if ((snapshot.keyboardState[idx] & 0x80) != 0)
            {
                hasActiveTarget = true;
                break;
            }
        }
        if (hasActiveTarget && IsKeyLogEnabled() && GetKeyLogLevel() >= 2)
        {
            wchar_t buffer[256] = {0};
            StringCchPrintfW(
                buffer,
                ARRAYSIZE(buffer),
                L"[EMIT] %s adapter=RawInputMapping emit=none reason=raw_neutral_suppressed",
                GetTimestamp().c_str());
            WriteLogLine(buffer);
        }
    }

    return false;
}

static bool EvaluateRuntimeDecision(
    uint32_t flags,
    uint32_t activePid,
    uint32_t profileId,
    uint32_t profileMode,
    uint64_t lastTick,
    PayloadRuntimeDecisionInterop& decision)
{
    memset(&decision, 0, sizeof(decision));
    return payload_core_evaluate_runtime_header(
               flags,
               activePid,
               profileId,
               profileMode,
               lastTick,
               GetCurrentProcessId(),
               GetTickCount64(),
               GetSharedTimeoutMs(),
               &decision) != 0 &&
           decision.is_valid != 0;
}

static bool EvaluateRuntimeDecision(const SharedSnapshot& snapshot, PayloadRuntimeDecisionInterop& decision)
{
    return EvaluateRuntimeDecision(
        snapshot.flags,
        snapshot.activePid,
        snapshot.profileId,
        snapshot.profileMode,
        snapshot.lastTick,
        decision);
}

static bool EvaluateRuntimeDecision(const SharedSnapshotLite& snapshot, PayloadRuntimeDecisionInterop& decision)
{
    return EvaluateRuntimeDecision(
        snapshot.flags,
        snapshot.activePid,
        snapshot.profileId,
        snapshot.profileMode,
        snapshot.lastTick,
        decision);
}

static bool EvaluateKeyDecision(const SharedSnapshot& snapshot, int vKey, PayloadKeyDecisionInterop& decision)
{
    if (vKey < 0 || vKey >= 256)
    {
        memset(&decision, 0, sizeof(decision));
        return false;
    }

    memset(&decision, 0, sizeof(decision));
    return payload_core_evaluate_key_state_header(
               snapshot.flags,
               snapshot.activePid,
               snapshot.profileId,
               snapshot.profileMode,
               snapshot.lastTick,
               GetCurrentProcessId(),
               GetTickCount64(),
               GetSharedTimeoutMs(),
               snapshot.targetMask[vKey] != 0 ? 1u : 0u,
               snapshot.blockMask[vKey] != 0 ? 1u : 0u,
               (snapshot.keyboardState[vKey] & 0x80) != 0 ? 1u : 0u,
               ShouldForceReleaseKey(vKey) ? 1u : 0u,
               &decision) != 0 &&
           decision.is_valid != 0;
}

static bool EvaluateLogicalKeyDecision(const SharedSnapshot& snapshot, int vKey, PayloadLogicalKeyDecisionInterop& decision)
{
    if (vKey < 0 || vKey >= 256)
    {
        memset(&decision, 0, sizeof(decision));
        return false;
    }

    const int pairVKey = GetDirectionPairVKey(vKey);
    memset(&decision, 0, sizeof(decision));
    const bool ok = payload_core_evaluate_logical_key_header(
                        snapshot.flags,
                        snapshot.activePid,
                        snapshot.profileId,
                        snapshot.profileMode,
                        snapshot.lastTick,
                        GetCurrentProcessId(),
                        GetTickCount64(),
                        GetSharedTimeoutMs(),
                        static_cast<uint32_t>(vKey),
                        snapshot.targetMask[vKey] != 0 ? 1u : 0u,
                        snapshot.blockMask[vKey] != 0 ? 1u : 0u,
                        (snapshot.keyboardState[vKey] & 0x80) != 0 ? 1u : 0u,
                        snapshot.edgeCounter[vKey],
                        pairVKey >= 0 ? static_cast<uint32_t>(pairVKey) : 0u,
                        pairVKey >= 0 && snapshot.targetMask[pairVKey] != 0 ? 1u : 0u,
                        pairVKey >= 0 && (snapshot.keyboardState[pairVKey] & 0x80) != 0 ? 1u : 0u,
                        pairVKey >= 0 ? snapshot.edgeCounter[pairVKey] : 0u,
                        ShouldForceReleaseKey(vKey) ? 1u : 0u,
                        g_lastLogicalDesiredState[vKey] != 0 ? 1u : 0u,
                        0u,
                        &decision) != 0 &&
                    decision.is_valid != 0;
    if (!ok)
    {
        return false;
    }

    if (decision.pressed_edge != 0 || decision.released_edge != 0)
    {
        LogLogicalEdge(vKey, decision.desired_down != 0, decision.pressed_edge != 0, decision.released_edge != 0);
    }

    if (decision.pair_conflict != 0 && IsDirectionVKey(vKey))
    {
        const int pair = GetDirectionPairVKey(vKey);
        if (pair >= 0)
        {
            const bool selfDown = decision.desired_down != 0;
            const bool pairDown =
                snapshot.targetMask[pair] != 0 &&
                (snapshot.keyboardState[pair] & 0x80) != 0 &&
                !ShouldForceReleaseKey(pair);
            if (selfDown != pairDown)
            {
                LogDirectionGroupDecision(
                    (vKey == VK_LEFT || vKey == VK_RIGHT) ? L"LR" : L"UD",
                    selfDown ? vKey : pair,
                    selfDown ? pair : vKey,
                    L"edge_counter");
            }
            else
            {
                LogDirectionGroupDecision(
                    (vKey == VK_LEFT || vKey == VK_RIGHT) ? L"LR" : L"UD",
                    0,
                    0,
                    L"edge_tie_release");
            }
        }
    }

    g_lastLogicalDesiredState[vKey] = decision.desired_down != 0 ? 1 : 0;
    return true;
}

static bool EvaluateChannelEmitDecision(
    const SharedSnapshot& snapshot,
    int vKey,
    bool projectedDownBefore,
    bool observedDown,
    PayloadLogicalKeyDecisionInterop& logicalDecision,
    PayloadChannelEmitDecisionInterop& emitDecision)
{
    memset(&logicalDecision, 0, sizeof(logicalDecision));
    memset(&emitDecision, 0, sizeof(emitDecision));
    if (vKey < 0 || vKey >= 256)
    {
        return false;
    }

    const int pairVKey = GetDirectionPairVKey(vKey);
    const bool ok = payload_core_decide_channel_emit(
                        snapshot.flags,
                        snapshot.activePid,
                        snapshot.profileId,
                        snapshot.profileMode,
                        snapshot.lastTick,
                        GetCurrentProcessId(),
                        GetTickCount64(),
                        GetSharedTimeoutMs(),
                        static_cast<uint32_t>(vKey),
                        snapshot.targetMask[vKey] != 0 ? 1u : 0u,
                        snapshot.blockMask[vKey] != 0 ? 1u : 0u,
                        (snapshot.keyboardState[vKey] & 0x80) != 0 ? 1u : 0u,
                        snapshot.edgeCounter[vKey],
                        pairVKey >= 0 ? static_cast<uint32_t>(pairVKey) : 0u,
                        pairVKey >= 0 && snapshot.targetMask[pairVKey] != 0 ? 1u : 0u,
                        pairVKey >= 0 && (snapshot.keyboardState[pairVKey] & 0x80) != 0 ? 1u : 0u,
                        pairVKey >= 0 ? snapshot.edgeCounter[pairVKey] : 0u,
                        ShouldForceReleaseKey(vKey) ? 1u : 0u,
                        g_lastLogicalDesiredState[vKey] != 0 ? 1u : 0u,
                        0u,
                        projectedDownBefore ? 1u : 0u,
                        observedDown ? 1u : 0u,
                        &logicalDecision,
                        &emitDecision) != 0 &&
                    logicalDecision.is_valid != 0;
    if (!ok)
    {
        return false;
    }

    if (logicalDecision.pressed_edge != 0 || logicalDecision.released_edge != 0)
    {
        LogLogicalEdge(vKey, logicalDecision.desired_down != 0, logicalDecision.pressed_edge != 0, logicalDecision.released_edge != 0);
    }
    g_lastLogicalDesiredState[vKey] = logicalDecision.desired_down != 0 ? 1 : 0;
    return true;
}

static bool EvaluatePathDecision(const SharedSnapshot& snapshot, PayloadPathDecisionInterop& decision)
{
    memset(&decision, 0, sizeof(decision));
    return payload_core_evaluate_path_decision_header(
               snapshot.flags,
               snapshot.activePid,
               snapshot.profileId,
               snapshot.profileMode,
               snapshot.lastTick,
               GetCurrentProcessId(),
               GetTickCount64(),
               GetSharedTimeoutMs(),
               kProfileModeMapping,
               &decision) != 0 &&
           decision.is_valid != 0;
}

static bool IsDirectionVKey(int vKey)
{
    switch (vKey)
    {
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
            return true;
        default:
            return false;
    }
}

static int GetDirectionPairVKey(int vKey)
{
    switch (vKey)
    {
        case VK_LEFT:
            return VK_RIGHT;
        case VK_RIGHT:
            return VK_LEFT;
        case VK_UP:
            return VK_DOWN;
        case VK_DOWN:
            return VK_UP;
        default:
            return -1;
    }
}

static void SyncDirectionConvergenceState(void* convergenceState, BYTE lastDirectionState[256], const SharedSnapshot& snapshot)
{
    if (!convergenceState || !lastDirectionState)
    {
        return;
    }

    PayloadRuntimeDecisionInterop unusedDecision = {};
    if (!EvaluateRuntimeDecision(snapshot, unusedDecision))
    {
        return;
    }

    for (int vKey = 0; vKey < 256; vKey++)
    {
        if (!IsDirectionVKey(vKey))
        {
            continue;
        }

        PayloadLogicalKeyDecisionInterop logicalDecision = {};
        if (!EvaluateLogicalKeyDecision(snapshot, vKey, logicalDecision))
        {
            continue;
        }

        const BYTE desiredDown = logicalDecision.desired_down != 0 ? 1 : 0;

        if (lastDirectionState[vKey] == desiredDown)
        {
            continue;
        }

        lastDirectionState[vKey] = desiredDown;
        payload_core_convergence_on_key_event(convergenceState, static_cast<uint32_t>(vKey), desiredDown ? 1u : 0u);
    }
}

static void EnsureDirectionConvergenceState()
{
    if (!t_directionConvergenceState)
    {
        t_directionConvergenceState = payload_core_convergence_create(kDirectionConvergenceExtraReleasePulses);
        memset(t_directionLastState, 0, sizeof(t_directionLastState));
        memset(t_forceReleaseMask, 0, sizeof(t_forceReleaseMask));
    }
}

static bool ShouldForceReleaseKey(int vKey)
{
    return vKey >= 0 &&
           vKey < 256 &&
           t_directionConvergenceState != nullptr &&
           t_forceReleaseMask[vKey] != 0;
}

static void ResolveDirectionPair(BYTE desiredState[256], const uint32_t edgeCounter[256], int firstVKey, int secondVKey)
{
    if (!desiredState || !edgeCounter)
    {
        return;
    }

    if (desiredState[firstVKey] == 0 || desiredState[secondVKey] == 0)
    {
        return;
    }

    if (edgeCounter[firstVKey] > edgeCounter[secondVKey])
    {
        desiredState[secondVKey] = 0;
        return;
    }

    if (edgeCounter[secondVKey] > edgeCounter[firstVKey])
    {
        desiredState[firstVKey] = 0;
        return;
    }

    desiredState[firstVKey] = 0;
    desiredState[secondVKey] = 0;
}

static bool BuildDirectionDesiredState(
    const SharedSnapshot& snapshot,
    const PayloadRuntimeDecisionInterop& runtimeDecision,
    BYTE desiredState[256])
{
    if (!desiredState)
    {
        return false;
    }

    memset(desiredState, 0, 256);
    if (runtimeDecision.is_alive == 0 || runtimeDecision.is_paused != 0 || runtimeDecision.should_clear != 0)
    {
        return true;
    }

    for (int vKey = 0; vKey < 256; vKey++)
    {
        if (!IsDirectionVKey(vKey))
        {
            continue;
        }

        desiredState[vKey] =
            (snapshot.targetMask[vKey] != 0 && (snapshot.keyboardState[vKey] & 0x80) != 0) ? 1 : 0;
    }

    ResolveDirectionPair(desiredState, snapshot.edgeCounter, VK_LEFT, VK_RIGHT);
    ResolveDirectionPair(desiredState, snapshot.edgeCounter, VK_UP, VK_DOWN);
    return true;
}

static bool TryPickDirectionTransition(
    const SharedSnapshot& snapshot,
    const PayloadRuntimeDecisionInterop& runtimeDecision,
    int preferredVKey,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut)
{
    if (!vKeyOut || !isDownOut)
    {
        return false;
    }

    BYTE desiredState[256] = {};
    if (!BuildDirectionDesiredState(snapshot, runtimeDecision, desiredState))
    {
        return false;
    }

    auto chooseRelease = [&](int vKey, const wchar_t* reason) -> bool {
        if (!IsDirectionVKey(vKey))
        {
            return false;
        }

        const bool rawDownBefore = (g_lastRawKeyboardState[vKey] & 0x80) != 0;
        if (!rawDownBefore)
        {
            return false;
        }

        if (desiredState[vKey] != 0 && !ShouldForceReleaseKey(vKey))
        {
            return false;
        }

        g_lastRawKeyboardState[vKey] = 0;
        *vKeyOut = vKey;
        *isDownOut = false;
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        LogDirectionDecision(
            L"force_up_raw",
            vKey,
            desiredState[vKey] != 0,
            rawDownBefore,
            g_lastWin32State[vKey] != 0,
            reason);
        return true;
    };

    auto choosePress = [&](int vKey, const wchar_t* reason) -> bool {
        if (!IsDirectionVKey(vKey))
        {
            return false;
        }

        const bool snapshotDown = desiredState[vKey] != 0;
        const bool rawDownBefore = (g_lastRawKeyboardState[vKey] & 0x80) != 0;
        if (!snapshotDown || rawDownBefore)
        {
            return false;
        }

        g_lastRawKeyboardState[vKey] = 0x80;
        *vKeyOut = vKey;
        *isDownOut = true;
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        LogDirectionDecision(
            L"press_raw",
            vKey,
            true,
            rawDownBefore,
            g_lastWin32State[vKey] != 0,
            reason);
        return true;
    };

    if (chooseRelease(preferredVKey, L"preferred_release"))
    {
        return true;
    }

    for (int vKey : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN})
    {
        if (vKey == preferredVKey)
        {
            continue;
        }
        if (chooseRelease(vKey, ShouldForceReleaseKey(vKey) ? L"force_release_mask" : L"stale_raw_down"))
        {
            return true;
        }
    }

    if (choosePress(preferredVKey, L"preferred_press"))
    {
        return true;
    }

    for (int vKey : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN})
    {
        if (vKey == preferredVKey)
        {
            continue;
        }
        if (choosePress(vKey, L"group_winner_press"))
        {
            return true;
        }
    }

    return false;
}

static bool HasMappingRawTransition(const SharedSnapshot& snapshot, bool alive, bool paused)
{
    const bool allowDown = alive && !paused;
    for (int idx = 0; idx < 256; idx++)
    {
        if (snapshot.targetMask[idx] == 0)
        {
            continue;
        }

        const bool desiredDown = allowDown && (snapshot.keyboardState[idx] & 0x80) != 0;
        const bool projectedDown = (g_lastRawKeyboardState[idx] & 0x80) != 0;
        if (desiredDown != projectedDown)
        {
            return true;
        }
    }

    return false;
}

static bool TryPickLogicalRawTransition(
    const SharedSnapshot& snapshot,
    int preferredVKey,
    int* vKeyOut,
    bool* isDownOut,
    const wchar_t** reasonOut)
{
    if (!vKeyOut || !isDownOut || preferredVKey < 0 || preferredVKey >= 256)
    {
        return false;
    }

    const bool preferredObservedDown = (snapshot.keyboardState[preferredVKey] & 0x80) != 0;
    auto tryEmit = [&](int vKey, bool observedDown, const wchar_t* adapter, const wchar_t* reason, int requiredAction) -> bool {
        if (vKey < 0 || vKey >= 256)
        {
            return false;
        }

        PayloadLogicalKeyDecisionInterop logicalDecision = {};
        PayloadChannelEmitDecisionInterop emitDecision = {};
        if (!EvaluateChannelEmitDecision(
                snapshot,
                vKey,
                (g_lastRawKeyboardState[vKey] & 0x80) != 0,
                observedDown,
                logicalDecision,
                emitDecision))
        {
            return false;
        }

        if (emitDecision.suppress_repeat != 0)
        {
            LogRepeatSuppressed(adapter, vKey);
        }

        if (requiredAction >= 0 && static_cast<int>(emitDecision.emit_action) != requiredAction)
        {
            return false;
        }

        if (emitDecision.emit_action == 0)
        {
            return false;
        }

        const bool beforeDown = (g_lastRawKeyboardState[vKey] & 0x80) != 0;
        const bool afterDown = emitDecision.projected_down_after != 0;
        g_lastRawKeyboardState[vKey] = afterDown ? 0x80 : 0x00;
        *vKeyOut = vKey;
        *isDownOut = emitDecision.emit_action == 1;
        if (reasonOut)
        {
            *reasonOut = reason;
        }
        LogAdapterEmit(adapter, vKey, static_cast<int>(emitDecision.emit_action), beforeDown, afterDown, reason);
        return true;
    };

    if (IsDirectionVKey(preferredVKey))
    {
        const int pairVKey = GetDirectionPairVKey(preferredVKey);
        const int order[] = {pairVKey, preferredVKey, VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN};

        for (int vKey : order)
        {
            if (!IsDirectionVKey(vKey))
            {
                continue;
            }
            const bool observedDown = (vKey == preferredVKey) ? preferredObservedDown : false;
            if (tryEmit(vKey, observedDown, L"RawInput", vKey == pairVKey ? L"direction_release_first" : L"logical_release", 2))
            {
                return true;
            }
        }

        if (tryEmit(preferredVKey, preferredObservedDown, L"RawInput", L"logical_press", 1))
        {
            return true;
        }

        for (int vKey : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN})
        {
            if (vKey == preferredVKey)
            {
                continue;
            }
            if (tryEmit(vKey, false, L"RawInput", L"group_winner_press", 1))
            {
                return true;
            }
        }

        return false;
    }

    return tryEmit(preferredVKey, preferredObservedDown, L"RawInput", L"logical_emit", -1);
}

static bool IsSnapshotAlive(const SharedSnapshot& snapshot)
{
    PayloadRuntimeDecisionInterop decision = {};
    if (EvaluateRuntimeDecision(snapshot, decision))
    {
        return decision.is_alive != 0;
    }

    if (snapshot.lastTick == 0)
    {
        return false;
    }

    ULONGLONG now = GetTickCount64();
    return now - snapshot.lastTick <= static_cast<ULONGLONG>(GetSharedTimeoutMs());
}

static bool IsSnapshotAliveLite(const SharedSnapshotLite& snapshot)
{
    PayloadRuntimeDecisionInterop decision = {};
    if (EvaluateRuntimeDecision(snapshot, decision))
    {
        return decision.is_alive != 0;
    }

    if (snapshot.lastTick == 0)
    {
        return false;
    }

    ULONGLONG now = GetTickCount64();
    return now - snapshot.lastTick <= static_cast<ULONGLONG>(GetSharedTimeoutMs());
}

static bool IsBypassProcess(const SharedSnapshot& snapshot)
{
    PayloadRuntimeDecisionInterop decision = {};
    if (EvaluateRuntimeDecision(snapshot, decision))
    {
        return decision.is_bypass_process != 0;
    }

    if (snapshot.activePid == 0)
    {
        return false;
    }

    return snapshot.activePid == GetCurrentProcessId();
}

static bool IsBypassProcessLite(const SharedSnapshotLite& snapshot)
{
    PayloadRuntimeDecisionInterop decision = {};
    if (EvaluateRuntimeDecision(snapshot, decision))
    {
        return decision.is_bypass_process != 0;
    }

    if (snapshot.activePid == 0)
    {
        return false;
    }

    return snapshot.activePid == GetCurrentProcessId();
}

static bool ShouldBlockKey(const SharedSnapshot& snapshot, int vKey, bool alive, bool paused)
{
    // 仅对控制端显式标记的拦截键生效，避免误伤非黑名单键。
    if (vKey < 0 || vKey >= 256)
    {
        return false;
    }

    if (!alive || paused)
    {
        return false;
    }

    return snapshot.blockMask[vKey] != 0;
}

static void EnsureVkeyToDikMap()
{
    // DirectInput 键盘使用 DIK 扫描码索引 256 字节状态数组，这里把 vKey 映射到 DIK
    // 以复用共享内存的键盘状态；扩展键需要补 0x80。
    if (InterlockedCompareExchange(&g_vkeyMapReady, 1, 0) != 0)
    {
        return;
    }

    for (int i = 0; i < 256; i++)
    {
        g_vkeyToDik[i] = -1;
    }

    for (int vKey = 0; vKey < 256; vKey++)
    {
        UINT scan = MapVirtualKeyW(static_cast<UINT>(vKey), MAPVK_VK_TO_VSC_EX);
        if (scan == 0)
        {
            continue;
        }

        int dik = static_cast<int>(scan & 0xFF);
        if ((scan & 0x100) != 0)
        {
            dik |= 0x80;
        }

        if (dik >= 0 && dik < 256)
        {
            g_vkeyToDik[vKey] = dik;
        }
    }
}

static bool ShouldForceDeviceStateOk()
{
    LONG cached = InterlockedCompareExchange(&g_forceDeviceStateOk, -1, -1);
    if (cached != -1)
    {
        return cached != 0;
    }

    wchar_t value[8] = {0};
    DWORD len = GetEnvironmentVariableW(L"DNFSYNC_FORCE_DI_OK", value, ARRAYSIZE(value));
    bool enabled = false;
    if (len > 0)
    {
        wchar_t ch = value[0];
        enabled = (ch == L'1' || ch == L'y' || ch == L'Y' || ch == L't' || ch == L'T');
    }

    InterlockedExchange(&g_forceDeviceStateOk, enabled ? 1 : 0);
    return enabled;
}

/// <summary>
/// 读取共享内存心跳超时配置（单位毫秒）。
/// </summary>
static DWORD GetSharedTimeoutMs()
{
    LONG cached = InterlockedCompareExchange(&g_sharedTimeoutMs, -1, -1);
    if (cached != -1)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD timeout = kDefaultSharedTimeoutMs;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kSharedTimeoutEnvName, value, ARRAYSIZE(value));
    if (len > 0)
    {
        wchar_t* end = nullptr;
        unsigned long parsed = wcstoul(value, &end, 10);
        if (end != value)
        {
            if (parsed < 50)
            {
                parsed = 50;
            }
            if (parsed > 60000)
            {
                parsed = 60000;
            }
            timeout = static_cast<DWORD>(parsed);
        }
    }

    InterlockedExchange(&g_sharedTimeoutMs, static_cast<LONG>(timeout));
    return timeout;
}

/// <summary>
/// 读取共享内存快照缓存时间（单位毫秒），用于降低高频读取成本。
/// </summary>
static DWORD GetSnapshotCacheMs()
{
    LONG cached = InterlockedCompareExchange(&g_snapshotCacheMs, -1, -1);
    if (cached != -1)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD cacheMs = kDefaultSnapshotCacheMs;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kSnapshotCacheEnvName, value, ARRAYSIZE(value));
    if (len > 0)
    {
        wchar_t* end = nullptr;
        unsigned long parsed = wcstoul(value, &end, 10);
        if (end != value)
        {
            if (parsed > 50)
            {
                parsed = 50;
            }
            cacheMs = static_cast<DWORD>(parsed);
        }
    }

    InterlockedExchange(&g_snapshotCacheMs, static_cast<LONG>(cacheMs));
    return cacheMs;
}

static bool IsRawInputLogEnabled()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgRawInputLog, -1, -1);
    if (cfg >= 0)
    {
        return cfg != 0;
    }

    LONG cached = InterlockedCompareExchange(&g_rawInputLogEnabled, -1, -1);
    if (cached != -1)
    {
        return cached != 0;
    }

    wchar_t value[8] = {0};
    DWORD len = GetEnvironmentVariableW(kRawInputLogEnvName, value, ARRAYSIZE(value));
    bool enabled = false;
    if (len > 0)
    {
        wchar_t ch = value[0];
        enabled = (ch == L'1' || ch == L'y' || ch == L'Y' || ch == L't' || ch == L'T');
    }

    InterlockedExchange(&g_rawInputLogEnabled, enabled ? 1 : 0);
    return enabled;
}

static DWORD GetRawInputLogIntervalMs()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgRawInputLogIntervalMs, -1, -1);
    if (cfg >= 0)
    {
        return static_cast<DWORD>(cfg);
    }

    LONG cached = InterlockedCompareExchange(&g_rawInputLogIntervalMs, -1, -1);
    if (cached != -1)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD interval = 200;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kRawInputLogIntervalEnvName, value, ARRAYSIZE(value));
    if (len > 0)
    {
        wchar_t* end = nullptr;
        unsigned long parsed = wcstoul(value, &end, 10);
        if (end != value)
        {
            if (parsed > 5000)
            {
                parsed = 5000;
            }
            interval = static_cast<DWORD>(parsed);
        }
    }

    InterlockedExchange(&g_rawInputLogIntervalMs, static_cast<LONG>(interval));
    return interval;
}

static bool ShouldLogRawInput()
{
    if (!IsRawInputLogEnabled())
    {
        return false;
    }

    DWORD interval = GetRawInputLogIntervalMs();
    if (interval == 0)
    {
        return true;
    }

    ULONGLONG now = GetTickCount64();
    LONGLONG last = InterlockedCompareExchange64(&g_rawInputLogLastTick, 0, 0);
    if (now - static_cast<ULONGLONG>(last) >= interval)
    {
        InterlockedExchange64(&g_rawInputLogLastTick, static_cast<LONGLONG>(now));
        return true;
    }

    return false;
}

static bool IsStatsEnabled()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgStatsEnabled, -1, -1);
    if (cfg >= 0)
    {
        return cfg != 0;
    }

    LONG cached = InterlockedCompareExchange(&g_statsEnabled, -1, -1);
    if (cached >= 0)
    {
        return cached != 0;
    }

    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kStatsEnvName, value, ARRAYSIZE(value));
    bool enabled = false;
    if (len > 0 && len < ARRAYSIZE(value))
    {
        if (_wcsicmp(value, L"1") == 0 ||
            _wcsicmp(value, L"true") == 0 ||
            _wcsicmp(value, L"yes") == 0 ||
            _wcsicmp(value, L"on") == 0)
        {
            enabled = true;
        }
    }
    InterlockedExchange(&g_statsEnabled, enabled ? 1 : 0);
    return enabled;
}

static DWORD GetStatsIntervalMs()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgStatsIntervalMs, -1, -1);
    if (cfg >= 0)
    {
        return static_cast<DWORD>(cfg);
    }

    LONG cached = InterlockedCompareExchange(&g_statsIntervalMs, -1, -1);
    if (cached >= 0)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD interval = 1000;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kStatsIntervalEnvName, value, ARRAYSIZE(value));
    if (len > 0 && len < ARRAYSIZE(value))
    {
        DWORD parsed = _wtoi(value);
        if (parsed > 0 && parsed <= 60000)
        {
            interval = parsed;
        }
    }
    InterlockedExchange(&g_statsIntervalMs, static_cast<LONG>(interval));
    return interval;
}

static bool IsDiagEnabled()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgDiagEnabled, -1, -1);
    if (cfg >= 0)
    {
        return cfg != 0;
    }

    LONG cached = InterlockedCompareExchange(&g_diagEnabled, -1, -1);
    if (cached >= 0)
    {
        return cached != 0;
    }

    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kDiagEnvName, value, ARRAYSIZE(value));
    bool enabled = false;
    if (len > 0 && len < ARRAYSIZE(value))
    {
        if (_wcsicmp(value, L"1") == 0 ||
            _wcsicmp(value, L"true") == 0 ||
            _wcsicmp(value, L"yes") == 0 ||
            _wcsicmp(value, L"on") == 0)
        {
            enabled = true;
        }
    }
    InterlockedExchange(&g_diagEnabled, enabled ? 1 : 0);
    return enabled;
}

static DWORD GetDiagIntervalMs()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgDiagIntervalMs, -1, -1);
    if (cfg >= 0)
    {
        return static_cast<DWORD>(cfg);
    }

    LONG cached = InterlockedCompareExchange(&g_diagIntervalMs, -1, -1);
    if (cached >= 0)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD interval = 1000;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kDiagIntervalEnvName, value, ARRAYSIZE(value));
    if (len > 0 && len < ARRAYSIZE(value))
    {
        DWORD parsed = _wtoi(value);
        if (parsed > 0 && parsed <= 60000)
        {
            interval = parsed;
        }
    }
    InterlockedExchange(&g_diagIntervalMs, static_cast<LONG>(interval));
    return interval;
}

static bool IsKeyLogEnabled()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgKeyLogEnabled, -1, -1);
    if (cfg >= 0)
    {
        return cfg != 0;
    }

    LONG cached = InterlockedCompareExchange(&g_keyLogEnabled, -1, -1);
    if (cached >= 0)
    {
        return cached != 0;
    }

    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kKeyLogEnvName, value, ARRAYSIZE(value));
    bool enabled = false;
    if (len > 0 && len < ARRAYSIZE(value))
    {
        if (_wcsicmp(value, L"1") == 0 ||
            _wcsicmp(value, L"true") == 0 ||
            _wcsicmp(value, L"yes") == 0 ||
            _wcsicmp(value, L"on") == 0)
        {
            enabled = true;
        }
    }
    InterlockedExchange(&g_keyLogEnabled, enabled ? 1 : 0);
    return enabled;
}

static DWORD GetKeyLogIntervalMs()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgKeyLogIntervalMs, -1, -1);
    if (cfg >= 0)
    {
        return static_cast<DWORD>(cfg);
    }

    LONG cached = InterlockedCompareExchange(&g_keyLogIntervalMs, -1, -1);
    if (cached >= 0)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD interval = 0;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kKeyLogIntervalEnvName, value, ARRAYSIZE(value));
    if (len > 0 && len < ARRAYSIZE(value))
    {
        DWORD parsed = _wtoi(value);
        if (parsed <= 5000)
        {
            interval = parsed;
        }
    }
    InterlockedExchange(&g_keyLogIntervalMs, static_cast<LONG>(interval));
    return interval;
}

static LONG GetKeyLogLevel()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgKeyLogLevel, -1, -1);
    if (cfg >= 0)
    {
        return cfg;
    }

    LONG cached = InterlockedCompareExchange(&g_keyLogLevel, -1, -1);
    if (cached >= 0)
    {
        return cached;
    }

    LONG level = 1;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kKeyLogLevelEnvName, value, ARRAYSIZE(value));
    if (len > 0 && len < ARRAYSIZE(value))
    {
        LONG parsed = _wtoi(value);
        if (parsed >= 1 && parsed <= 3)
        {
            level = parsed;
        }
    }
    InterlockedExchange(&g_keyLogLevel, level);
    return level;
}

static DWORD GetKeyUpTimeoutMs()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgKeyUpTimeoutMs, -1, -1);
    if (cfg >= 0)
    {
        return static_cast<DWORD>(cfg);
    }

    LONG cached = InterlockedCompareExchange(&g_keyUpTimeoutMs, -1, -1);
    if (cached >= 0)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD timeout = 300;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kKeyUpTimeoutEnvName, value, ARRAYSIZE(value));
    if (len > 0 && len < ARRAYSIZE(value))
    {
        DWORD parsed = _wtoi(value);
        if (parsed > 0 && parsed <= 5000)
        {
            timeout = parsed;
        }
    }
    InterlockedExchange(&g_keyUpTimeoutMs, static_cast<LONG>(timeout));
    return timeout;
}

static bool ShouldLogKeyEvent()
{
    if (!IsKeyLogEnabled())
    {
        return false;
    }

    DWORD interval = GetKeyLogIntervalMs();
    if (interval == 0)
    {
        return true;
    }

    ULONGLONG now = GetTickCount64();
    LONGLONG last = InterlockedCompareExchange64(&g_keyLogLastTick, 0, 0);
    if (now - static_cast<ULONGLONG>(last) >= interval)
    {
        InterlockedExchange64(&g_keyLogLastTick, static_cast<LONGLONG>(now));
        return true;
    }

    return false;
}

static void LogKeyWarn(const wchar_t* reason, int vKey, DWORD elapsedMs, KeyChannel channel)
{
    wchar_t buffer[256] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[KEYWARN] %s reason=%s vkey=0x%02X elapsed=%lu channel=%s",
        GetTimestamp().c_str(),
        reason ? reason : L"unknown",
        vKey,
        elapsedMs,
        KeyChannelToText(channel));
    WriteLogLine(buffer);
}

static void RecordKeyEvent(KeyChannel channel, int vKey, bool isDown, bool spoofed, UINT makeCode, UINT flags, uint32_t profileMode)
{
    if (vKey < 0 || vKey >= 256)
    {
        return;
    }
    if (!IsKeyLogEnabled())
    {
        return;
    }

    LONG level = GetKeyLogLevel();
    DWORD now = GetTickCount();

    if (level >= 2)
    {
        EnsureKeyStateLock();
        EnterCriticalSection(&g_keyStateLock);
        bool prevDown = g_keyDownState[vKey] != 0;
        g_keyLastChannel[vKey] = static_cast<BYTE>(channel);
        if (isDown)
        {
            g_keyDownState[vKey] = 1;
            g_keyDownTick[vKey] = now;
        }
        else
        {
            if (prevDown)
            {
                g_keyDownState[vKey] = 0;
                g_keyUpTick[vKey] = now;
            }
            else
            {
                DWORD elapsed = (now >= g_keyUpTick[vKey]) ? (now - g_keyUpTick[vKey]) : 0;
                LogKeyWarn(L"orphan_up", vKey, elapsed, channel);
            }
        }
        LeaveCriticalSection(&g_keyStateLock);
    }

    if (!ShouldLogKeyEvent())
    {
        return;
    }

    wchar_t buffer[320] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[KEY] %s ch=%s vkey=0x%02X down=%d mk=0x%02X fl=0x%X spoof=%d mode=%lu",
        GetTimestamp().c_str(),
        KeyChannelToText(channel),
        vKey,
        isDown ? 1 : 0,
        makeCode,
        flags,
        spoofed ? 1 : 0,
        profileMode);
    WriteLogLine(buffer);
}

static void LogKeyFix(const wchar_t* reason, int vKey, bool isDown)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }

    wchar_t buffer[256] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[KEYFIX] %s reason=%s vkey=0x%02X down=%d",
        GetTimestamp().c_str(),
        reason ? reason : L"align",
        vKey,
        isDown ? 1 : 0);
    WriteLogLine(buffer);
}

static void LogDirectionDecision(const wchar_t* action, int vKey, bool snapshotDown, bool rawDownBefore, bool win32DownBefore, const wchar_t* reason)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }
    if (!IsDirectionVKey(vKey))
    {
        return;
    }

    wchar_t buffer[320] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[DIR] %s action=%s vkey=0x%02X snapshot=%d raw_before=%d win32_before=%d reason=%s",
        GetTimestamp().c_str(),
        action ? action : L"unknown",
        vKey,
        snapshotDown ? 1 : 0,
        rawDownBefore ? 1 : 0,
        win32DownBefore ? 1 : 0,
        reason ? reason : L"none");
    WriteLogLine(buffer);
}

static void LogLogicalEdge(int vKey, bool desiredDown, bool pressedEdge, bool releasedEdge)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }

    wchar_t buffer[320] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[EDGE] %s vkey=0x%02X desired=%d pressed=%d released=%d",
        GetTimestamp().c_str(),
        vKey,
        desiredDown ? 1 : 0,
        pressedEdge ? 1 : 0,
        releasedEdge ? 1 : 0);
    WriteLogLine(buffer);
}

static void LogAdapterEmit(const wchar_t* adapter, int vKey, int emitAction, bool beforeDown, bool afterDown, const wchar_t* reason)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }

    const wchar_t* emitText = L"none";
    if (emitAction == 1)
    {
        emitText = L"down";
    }
    else if (emitAction == 2)
    {
        emitText = L"up";
    }

    wchar_t buffer[352] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[EMIT] %s adapter=%s vkey=0x%02X emit=%s before=%d after=%d reason=%s",
        GetTimestamp().c_str(),
        adapter ? adapter : L"unknown",
        vKey,
        emitText,
        beforeDown ? 1 : 0,
        afterDown ? 1 : 0,
        reason ? reason : L"none");
    WriteLogLine(buffer);
}

static void LogDirectionGroupDecision(const wchar_t* pairName, int winnerVKey, int loserVKey, const wchar_t* reason)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }

    wchar_t buffer[320] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[GROUP] %s pair=%s winner=0x%02X loser=0x%02X reason=%s",
        GetTimestamp().c_str(),
        pairName ? pairName : L"unknown",
        winnerVKey,
        loserVKey,
        reason ? reason : L"none");
    WriteLogLine(buffer);
}

static void LogRepeatSuppressed(const wchar_t* adapter, int vKey)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }

    wchar_t buffer[256] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[REPEAT] %s adapter=%s vkey=0x%02X suppressed=1",
        GetTimestamp().c_str(),
        adapter ? adapter : L"unknown",
        vKey);
    WriteLogLine(buffer);
}

static void LogPauseInterception(const wchar_t* adapter, int vKey, bool hadProjectedDown, const wchar_t* reason)
{
    if (!IsKeyLogEnabled() || GetKeyLogLevel() < 2)
    {
        return;
    }

    wchar_t buffer[352] = {0};
    StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"[PAUSE] %s adapter=%s vkey=0x%02X projected=%d reason=%s",
        GetTimestamp().c_str(),
        adapter ? adapter : L"unknown",
        vKey,
        hadProjectedDown ? 1 : 0,
        reason ? reason : L"none");
    WriteLogLine(buffer);
}

static void RecordWin32KeyEventIfNeeded(int vKey, SHORT result, bool spoofed, uint32_t profileMode)
{
    if (!IsKeyLogEnabled())
    {
        return;
    }
    if (GetKeyLogLevel() < 2)
    {
        return;
    }

    bool isDown = (result & 0x8000) != 0;
    BYTE downValue = isDown ? 1 : 0;
    BYTE prev = g_lastWin32State[vKey];
    if (prev == downValue)
    {
        return;
    }
    g_lastWin32State[vKey] = downValue;
    RecordKeyEvent(KeyChannel::Win32, vKey, isDown, spoofed, 0, 0, profileMode);
}

static void RecordDirectInputKeyEventIfNeeded(int vKey, bool isDown, bool spoofed, uint32_t profileMode)
{
    if (!IsKeyLogEnabled())
    {
        return;
    }
    if (GetKeyLogLevel() < 3)
    {
        return;
    }

    BYTE downValue = isDown ? 1 : 0;
    BYTE prev = g_lastDIState[vKey];
    if (prev == downValue)
    {
        return;
    }
    g_lastDIState[vKey] = downValue;
    RecordKeyEvent(KeyChannel::DirectInput, vKey, isDown, spoofed, 0, 0, profileMode);
}

static void CheckKeyUpTimeouts()
{
    if (!IsKeyLogEnabled())
    {
        return;
    }
    LONG level = GetKeyLogLevel();
    if (level < 2)
    {
        return;
    }

    DWORD timeout = GetKeyUpTimeoutMs();
    if (timeout == 0)
    {
        return;
    }

    DWORD now = GetTickCount();
    EnsureKeyStateLock();
    EnterCriticalSection(&g_keyStateLock);
    for (int vKey = 0; vKey < 256; vKey++)
    {
        if (g_keyDownState[vKey] == 0)
        {
            continue;
        }

        DWORD downTick = g_keyDownTick[vKey];
        if (now - downTick < timeout)
        {
            continue;
        }

        DWORD lastWarn = g_keyWarnTick[vKey];
        if (now - lastWarn < timeout)
        {
            continue;
        }

        g_keyWarnTick[vKey] = now;
        KeyChannel channel = static_cast<KeyChannel>(g_keyLastChannel[vKey]);
        LogKeyWarn(L"missing_keyup", vKey, now - downTick, channel);
    }
    LeaveCriticalSection(&g_keyStateLock);
}

static void CountStat(volatile LONG* counter)
{
    if (!IsStatsEnabled() && !IsDiagEnabled())
    {
        return;
    }
    InterlockedIncrement(counter);
}

static bool ReadSharedSnapshotLite(SharedSnapshotLite& snapshot)
{
    if (!EnsureSharedMemory() || !g_sharedState)
    {
        return false;
    }

    if (g_sharedState->version != kSharedVersion)
    {
        if (InterlockedCompareExchange(&g_sharedVersionLogged, 1, 0) == 0)
        {
            LogProtocolMismatch(L"version_mismatch", kSharedVersion, g_sharedState ? g_sharedState->version : 0);
        }
        return false;
    }

    const SharedKeyboardStateV2* snapshotState = g_sharedState;
    for (int i = 0; i < 3; i++)
    {
        uint32_t seq1 = snapshotState->seq;
        if ((seq1 & 1) != 0)
        {
            continue;
        }

        snapshot.seq = seq1;
        snapshot.flags = snapshotState->flags;
        snapshot.activePid = snapshotState->activePid;
        snapshot.profileId = snapshotState->profileId;
        snapshot.profileMode = snapshotState->profileMode;
        snapshot.lastTick = snapshotState->lastTick;

        uint32_t seq2 = snapshotState->seq;
        if (seq1 == seq2 && (seq2 & 1) == 0)
        {
            g_lastProfileId = snapshot.profileId;
            g_lastProfileMode = snapshot.profileMode;
            return true;
        }
    }

    return false;
}

static bool ReadSharedSnapshotLiteCached(SharedSnapshotLite& snapshot)
{
    DWORD cacheMs = GetSnapshotCacheMs();
    if (cacheMs == 0)
    {
        return ReadSharedSnapshotLite(snapshot);
    }

    thread_local SharedSnapshotLite cached = {};
    thread_local ULONGLONG cachedTick = 0;
    thread_local bool cachedValid = false;

    ULONGLONG now = GetTickCount64();
    if (cachedValid && now - cachedTick <= cacheMs)
    {
        snapshot = cached;
        return true;
    }

    if (!ReadSharedSnapshotLite(cached))
    {
        cachedValid = false;
        return false;
    }

    cachedTick = now;
    cachedValid = true;
    snapshot = cached;
    return true;
}

static bool ReadSharedSnapshotCached(SharedSnapshot& snapshot)
{
    DWORD cacheMs = GetSnapshotCacheMs();
    if (cacheMs == 0)
    {
        return ReadSharedSnapshot(snapshot);
    }

    thread_local SharedSnapshot cached = {};
    thread_local ULONGLONG cachedTick = 0;
    thread_local bool cachedValid = false;
    EnsureDirectionConvergenceState();

    ULONGLONG now = GetTickCount64();
    const ULONGLONG cacheAge = cachedValid ? (now - cachedTick) : 0;
    const bool shouldForceRefresh = cachedValid &&
        t_directionConvergenceState &&
        payload_core_convergence_should_refresh(t_directionConvergenceState, cacheAge, cacheMs) != 0;
    if (cachedValid && !shouldForceRefresh && cacheAge <= cacheMs)
    {
        snapshot = cached;
        return true;
    }

    SharedSnapshotLite lite = {};
    if (cachedValid && ReadSharedSnapshotLite(lite))
    {
        if (lite.seq == cached.seq && (lite.seq & 1) == 0)
        {
            cached.seq = lite.seq;
            cached.flags = lite.flags;
            cached.activePid = lite.activePid;
            cached.profileId = lite.profileId;
            cached.profileMode = lite.profileMode;
            cached.lastTick = lite.lastTick;
            cachedTick = now;
            snapshot = cached;
            return true;
        }
    }

    if (!ReadSharedSnapshot(cached))
    {
        cachedValid = false;
        return false;
    }

    SyncDirectionConvergenceState(t_directionConvergenceState, t_directionLastState, cached);
    memset(t_forceReleaseMask, 0, sizeof(t_forceReleaseMask));
    if (t_directionConvergenceState)
    {
        payload_core_convergence_take_force_release_mask(
            t_directionConvergenceState,
            t_forceReleaseMask,
            sizeof(t_forceReleaseMask));
    }
    cachedTick = now;
    cachedValid = true;
    snapshot = cached;
    return true;
}

/// <summary>
/// 读取伪造延迟配置（单位毫秒），用于注入后短时间禁止伪造。
/// </summary>
static DWORD GetSpoofDelayMs()
{
    LoadDebugConfig();
    LONG cfg = InterlockedCompareExchange(&g_cfgSpoofDelayMs, -1, -1);
    if (cfg >= 0)
    {
        return static_cast<DWORD>(cfg);
    }

    LONG cached = InterlockedCompareExchange(&g_spoofDelayMs, -1, -1);
    if (cached != -1)
    {
        return static_cast<DWORD>(cached);
    }

    DWORD delay = kDefaultSpoofDelayMs;
    wchar_t value[16] = {0};
    DWORD len = GetEnvironmentVariableW(kSpoofDelayEnvName, value, ARRAYSIZE(value));
    if (len > 0)
    {
        wchar_t* end = nullptr;
        unsigned long parsed = wcstoul(value, &end, 10);
        if (end != value)
        {
            if (parsed > 600000)
            {
                parsed = 600000;
            }
            delay = static_cast<DWORD>(parsed);
        }
    }

    InterlockedExchange(&g_spoofDelayMs, static_cast<LONG>(delay));
    return delay;
}

/// <summary>
/// 初始化伪造延迟时间戳并记录日志。
/// </summary>
static void InitializeSpoofDelay()
{
    if (g_injectTick == 0)
    {
        g_injectTick = GetTickCount64();
    }

    DWORD delay = GetSpoofDelayMs();
    if (delay == 0)
    {
        LogInfo(L"伪造延迟已关闭（DNFSYNC_SPOOF_DELAY_MS=0）");
        return;
    }

    if (InterlockedCompareExchange(&g_spoofDelayLogged, 1, 0) == 0)
    {
        wchar_t buffer[160] = {0};
        StringCchPrintfW(
            buffer,
            ARRAYSIZE(buffer),
            L"伪造延迟已启用：%lu ms（环境变量 %s）",
            delay,
            kSpoofDelayEnvName);
        LogInfo(buffer);
    }
}

/// <summary>
/// 判断伪造延迟是否仍然生效，生效期间必须完全停止伪造。
/// </summary>
static bool IsSpoofDelayActive()
{
    DWORD delay = GetSpoofDelayMs();
    if (delay == 0)
    {
        return false;
    }

    ULONGLONG start = g_injectTick;
    if (start == 0)
    {
        return false;
    }

    ULONGLONG now = GetTickCount64();
    if (now - start < delay)
    {
        return true;
    }

    if (InterlockedCompareExchange(&g_spoofDelayEndLogged, 1, 0) == 0)
    {
        LogInfo(L"伪造延迟结束，开始启用输入伪造");
    }

    return false;
}

struct WindowSearchContext
{
    DWORD pid;
    HWND best;
    HWND fallback;
};

static BOOL CALLBACK EnumWindowsFindSelf(HWND hwnd, LPARAM lparam)
{
    auto* ctx = reinterpret_cast<WindowSearchContext*>(lparam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid)
    {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd))
    {
        return TRUE;
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr)
    {
        return TRUE;
    }

    if (!ctx->fallback)
    {
        ctx->fallback = hwnd;
    }

    if (GetWindowTextLengthW(hwnd) > 0)
    {
        ctx->best = hwnd;
        return FALSE;
    }

    return TRUE;
}

static void UpdateSelfWindowCache()
{
    WindowSearchContext ctx = {};
    ctx.pid = GetCurrentProcessId();
    EnumWindows(EnumWindowsFindSelf, reinterpret_cast<LPARAM>(&ctx));

    HWND result = ctx.best ? ctx.best : ctx.fallback;
    if (result && IsWindow(result))
    {
        SetSelfWindowCache(result);
    }
}

static HWND GetSelfMainWindow()
{
    HWND cached = GetSelfWindowCache();
    if (cached && IsWindow(cached))
    {
        return cached;
    }

    return nullptr;
}

static bool IsWindowOwnedBySelf(HWND hwnd)
{
    if (!hwnd)
    {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

static bool ShouldSpoofFocus()
{
    if (IsSpoofDelayActive())
    {
        return false;
    }

    // 仅在后台且共享内存存活/未暂停时伪造焦点，避免影响前台与暂停态。
    SharedSnapshot snapshot = {};
    if (!ReadSharedSnapshotCached(snapshot))
    {
        return false;
    }

    PayloadPathDecisionInterop decision = {};
    if (!EvaluatePathDecision(snapshot, decision))
    {
        return false;
    }

    return decision.should_spoof_focus != 0;
}

static bool IsKeyboardRawInputHandle(HRAWINPUT hRawInput)
{
    if (!g_origGetRawInputData || !hRawInput)
    {
        return false;
    }

    RAWINPUTHEADER header = {};
    UINT size = sizeof(header);
    UINT result = g_origGetRawInputData(hRawInput, RID_HEADER, &header, &size, sizeof(RAWINPUTHEADER));
    if (result == 0 || size < sizeof(RAWINPUTHEADER))
    {
        return false;
    }

    return header.dwType == RIM_TYPEKEYBOARD;
}

static bool ShouldPromoteRawKeyboardMessage(HRAWINPUT hRawInput)
{
    if (!g_origGetRawInputData || !hRawInput)
    {
        return false;
    }

    RAWINPUT raw = {};
    UINT size = sizeof(raw);
    UINT result = g_origGetRawInputData(hRawInput, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
    if (result == static_cast<UINT>(-1) || size < sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD))
    {
        return false;
    }
    if (raw.header.dwType != RIM_TYPEKEYBOARD)
    {
        return false;
    }

    SharedSnapshot snapshot = {};
    if (!ReadSharedSnapshotCached(snapshot))
    {
        return false;
    }

    ApplyClearIfNeeded(snapshot);
    ApplyRawClearIfNeeded(snapshot);
    if (IsBypassProcess(snapshot))
    {
        return false;
    }

    PayloadPathDecisionInterop pathDecision = {};
    if (!EvaluatePathDecision(snapshot, pathDecision))
    {
        return false;
    }

    if (pathDecision.is_bypass_process != 0)
    {
        return false;
    }

    if (pathDecision.can_process_keys == 0)
    {
        return pathDecision.is_paused != 0 || pathDecision.should_clear != 0 || pathDecision.is_alive == 0;
    }

    if (pathDecision.should_use_mapping != 0)
    {
        return HasMappingRawTransition(snapshot, pathDecision.is_alive != 0, pathDecision.is_paused != 0);
    }

    int vKey = static_cast<int>(raw.data.keyboard.VKey);
    if (vKey < 0 || vKey >= 256)
    {
        return false;
    }

    PayloadLogicalKeyDecisionInterop logicalDecision = {};
    PayloadChannelEmitDecisionInterop emitDecision = {};
    if (!EvaluateChannelEmitDecision(
            snapshot,
            vKey,
            (g_lastRawKeyboardState[vKey] & 0x80) != 0,
            (raw.data.keyboard.Flags & RI_KEY_BREAK) == 0,
            logicalDecision,
            emitDecision))
    {
        return false;
    }

    if (emitDecision.suppress_repeat != 0)
    {
        LogRepeatSuppressed(L"RawMessage", vKey);
    }

    if (emitDecision.emit_action != 0)
    {
        return true;
    }

    if (IsDirectionVKey(vKey))
    {
        for (int candidate : {GetDirectionPairVKey(vKey), vKey, VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN})
        {
            if (!IsDirectionVKey(candidate))
            {
                continue;
            }

            PayloadLogicalKeyDecisionInterop directionLogical = {};
            PayloadChannelEmitDecisionInterop directionEmit = {};
            if (!EvaluateChannelEmitDecision(
                    snapshot,
                    candidate,
                    (g_lastRawKeyboardState[candidate] & 0x80) != 0,
                    candidate == vKey ? ((raw.data.keyboard.Flags & RI_KEY_BREAK) == 0) : false,
                    directionLogical,
                    directionEmit))
            {
                continue;
            }

            if (directionEmit.suppress_repeat != 0)
            {
                LogRepeatSuppressed(L"RawMessage", candidate);
            }
            if (directionEmit.emit_action != 0)
            {
                return true;
            }
        }
    }

    return false;
}

static void FixWmInputMessage(MSG* msg)
{
    if (!msg)
    {
        return;
    }

    if (msg->message != WM_INPUT)
    {
        return;
    }

    if (msg->wParam != RIM_INPUTSINK)
    {
        return;
    }

    // 只有存在真实逻辑 transition 时才将后台 RawInput 提升为前台语义，
    // 避免把系统自动重复的 down 流直接重放到从端。
    if (!ShouldSpoofFocus())
    {
        return;
    }

    HRAWINPUT hRawInput = reinterpret_cast<HRAWINPUT>(msg->lParam);
    if (!IsKeyboardRawInputHandle(hRawInput))
    {
        return;
    }

    if (!ShouldPromoteRawKeyboardMessage(hRawInput))
    {
        return;
    }

    msg->wParam = RIM_INPUT;
    CountStat(&g_countSpoofWmInput);
}

// ------------------------------
// Hook 回调
// ------------------------------

static SHORT WINAPI Hook_GetAsyncKeyState(int vKey)
{
    CountStat(&g_countGetAsyncKeyState);

    if (!g_origGetAsyncKeyState)
    {
        return 0;
    }

    SHORT original = g_origGetAsyncKeyState(vKey);
    if (vKey < 0 || vKey >= 256)
    {
        return original;
    }

    if (IsSpoofDelayActive())
    {
        return original;
    }

    SharedSnapshot snapshot = {};
    if (!ReadSharedSnapshotCached(snapshot))
    {
        return original;
    }

    ApplyClearIfNeeded(snapshot);

    if (IsBypassProcess(snapshot))
    {
        return original;
    }

    PayloadPathDecisionInterop pathDecision = {};
    if (EvaluatePathDecision(snapshot, pathDecision) &&
        pathDecision.is_bypass_process == 0 &&
        pathDecision.can_process_keys == 0)
    {
        RecordWin32KeyEventIfNeeded(vKey, 0, true, snapshot.profileMode);
        return 0;
    }

    PayloadLogicalKeyDecisionInterop logicalDecision = {};
    if (!EvaluateLogicalKeyDecision(snapshot, vKey, logicalDecision))
    {
        return original;
    }

    if (logicalDecision.target_marked == 0)
    {
        if (logicalDecision.should_block != 0)
        {
            RecordWin32KeyEventIfNeeded(vKey, 0, true, snapshot.profileMode);
            return 0;
        }

        return original;
    }

    if (logicalDecision.desired_down == 0)
    {
        RecordWin32KeyEventIfNeeded(vKey, 0, true, snapshot.profileMode);
        return 0;
    }

    SHORT result = 0;
    result |= static_cast<SHORT>(0x8000);

    uint32_t currentEdge = snapshot.edgeCounter[vKey];
    if (currentEdge != g_lastEdgeCounter[vKey])
    {
        g_lastEdgeCounter[vKey] = currentEdge;
        result |= 0x0001;
    }

    CountStat(&g_countSpoofAsync);
    RecordWin32KeyEventIfNeeded(vKey, result, true, snapshot.profileMode);
    return result;
}

static BOOL WINAPI Hook_GetKeyboardState(PBYTE lpKeyState)
{
    CountStat(&g_countGetKeyboardState);

    if (!g_origGetKeyboardState)
    {
        return FALSE;
    }

    BOOL ok = g_origGetKeyboardState(lpKeyState);
    if (!ok || !lpKeyState)
    {
        return ok;
    }

    if (IsSpoofDelayActive())
    {
        return ok;
    }

    SharedSnapshot snapshot = {};
    if (!ReadSharedSnapshotCached(snapshot))
    {
        return ok;
    }

    ApplyClearIfNeeded(snapshot);

    if (IsBypassProcess(snapshot))
    {
        return ok;
    }

    PayloadPathDecisionInterop pathDecision = {};
    if (EvaluatePathDecision(snapshot, pathDecision) &&
        pathDecision.is_bypass_process == 0 &&
        pathDecision.can_process_keys == 0)
    {
        memset(lpKeyState, 0, 256);
        CountStat(&g_countSpoofKeyboard);
        return ok;
    }

    bool spoofed = false;

    for (int i = 0; i < 256; i++)
    {
        PayloadLogicalKeyDecisionInterop logicalDecision = {};
        if (!EvaluateLogicalKeyDecision(snapshot, i, logicalDecision))
        {
            continue;
        }

        if (logicalDecision.target_marked != 0)
        {
            if (logicalDecision.desired_down == 0)
            {
                lpKeyState[i] &= static_cast<BYTE>(~0x81);
                spoofed = true;
                continue;
            }

            BYTE desired = snapshot.keyboardState[i];
            lpKeyState[i] = (lpKeyState[i] & static_cast<BYTE>(~0x81)) | (desired & 0x81);
            spoofed = true;
            continue;
        }

        if (logicalDecision.should_block != 0)
        {
            // 拦截键在同步生效时强制抬起，避免后台继续读到真实输入。
            lpKeyState[i] &= static_cast<BYTE>(~0x81);
            spoofed = true;
        }
    }

    if (spoofed)
    {
        CountStat(&g_countSpoofKeyboard);
    }

    return ok;
}

static BOOL WINAPI Hook_RegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT numDevices, UINT size)
{
    CountStat(&g_countRegisterRawInput);
    return g_origRegisterRawInputDevices ? g_origRegisterRawInputDevices(devices, numDevices, size) : FALSE;
}

static UINT WINAPI Hook_GetRawInputBuffer(PRAWINPUT data, PUINT size, UINT headerSize)
{
    CountStat(&g_countGetRawInputBuffer);
    if (!g_origGetRawInputBuffer)
    {
        return 0;
    }

    UINT count = g_origGetRawInputBuffer(data, size, headerSize);
    if (!data || !size || count == 0)
    {
        return count;
    }

    if (IsSpoofDelayActive())
    {
        return count;
    }

    InterlockedExchange(&g_seenRawInputBuffer, 1);

    SharedSnapshot snapshot = {};
    const bool hasSnapshot = ReadSharedSnapshotCached(snapshot);
    if (hasSnapshot)
    {
        ApplyClearIfNeeded(snapshot);
        ApplyRawClearIfNeeded(snapshot);
    }

    RAWINPUT* raw = data;
    for (UINT i = 0; i < count; i++)
    {
        if (raw->header.dwType == RIM_TYPEKEYBOARD &&
            raw->header.dwSize >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD))
        {
            bool spoofed = false;
            if (hasSnapshot && !IsBypassProcess(snapshot))
            {
                PayloadPathDecisionInterop pathDecision = {};
                if (!EvaluatePathDecision(snapshot, pathDecision))
                {
                    continue;
                }

                if (pathDecision.can_process_keys == 0)
                {
                    int vKey = static_cast<int>(raw->data.keyboard.VKey);
                    bool isDown = false;
                    const wchar_t* reason = nullptr;
                    if (TryPickSilentRawTransition(snapshot, vKey, &vKey, &isDown, &reason))
                    {
                        BuildRawKeyboardEvent(vKey, isDown, raw->data.keyboard);
                        spoofed = true;
                        LogAdapterEmit(L"RawInputBuffer", vKey, isDown ? 1 : 2, !isDown, isDown, reason);
                    }
                }
                else if (pathDecision.should_use_mapping != 0)
                {
                    // 映射模式下用目标键序列重写 RawInput，确保后台能收到映射后的按键事件。
                    int vKey = 0;
                    bool isDown = false;
                    const wchar_t* reason = nullptr;
                    if (TryPickMappingRawTransition(
                            snapshot,
                            pathDecision.is_alive != 0,
                            pathDecision.is_paused != 0,
                            &vKey,
                            &isDown,
                            &reason))
                    {
                        BuildRawKeyboardEvent(vKey, isDown, raw->data.keyboard);
                        spoofed = true;
                        LogAdapterEmit(L"RawInputBuffer", vKey, isDown ? 1 : 2, !isDown, isDown, reason);
                    }
                }
                else
                {
                    int vKey = static_cast<int>(raw->data.keyboard.VKey);
                    if (vKey >= 0 && vKey < 256)
                    {
                        int transitionVKey = 0;
                        bool transitionDown = false;
                        const wchar_t* reason = nullptr;
                        if (TryPickLogicalRawTransition(snapshot, vKey, &transitionVKey, &transitionDown, &reason))
                        {
                            BuildRawKeyboardEvent(transitionVKey, transitionDown, raw->data.keyboard);
                            spoofed = true;
                        }
                    }
                }
            }

            if (spoofed)
            {
                CountStat(&g_countSpoofRawInputBuffer);
            }

            if (IsKeyLogEnabled() && GetKeyLogLevel() >= 1)
            {
                const RAWKEYBOARD& kb = raw->data.keyboard;
                const bool isDown = (kb.Flags & RI_KEY_BREAK) == 0;
                const uint32_t mode = hasSnapshot ? snapshot.profileMode : 0;
                RecordKeyEvent(KeyChannel::RawInputBuffer, static_cast<int>(kb.VKey), isDown, spoofed, kb.MakeCode, kb.Flags, mode);
            }

            if (ShouldLogRawInput())
            {
                const RAWKEYBOARD& kb = raw->data.keyboard;
                wchar_t buffer[256] = {0};
                StringCchPrintfW(
                    buffer,
                    ARRAYSIZE(buffer),
                    L"[RAWB] %s RawInputBuffer 键盘: VKey=%u(0x%02X) MakeCode=%u(0x%02X) Flags=0x%X Spoof=%d",
                    GetTimestamp().c_str(),
                    kb.VKey,
                    kb.VKey,
                    kb.MakeCode,
                    kb.MakeCode,
                    kb.Flags,
                    spoofed ? 1 : 0);
                WriteLogLine(buffer);
            }
        }

        // 跳到下一个 RawInput 结构，防止异常尺寸导致死循环。
        if (raw->header.dwSize == 0)
        {
            break;
        }
        raw = reinterpret_cast<RAWINPUT*>(reinterpret_cast<BYTE*>(raw) + raw->header.dwSize);
    }

    return count;
}

static UINT WINAPI Hook_GetRawInputData(HRAWINPUT hRawInput, UINT command, LPVOID data, PUINT size, UINT headerSize)
{
    CountStat(&g_countGetRawInputData);
    if (!g_origGetRawInputData)
    {
        return 0;
    }

    UINT result = g_origGetRawInputData(hRawInput, command, data, size, headerSize);

    if (IsSpoofDelayActive())
    {
        return result;
    }

    // RawInput 键盘数据大小通常小于 sizeof(RAWINPUT)，必须以 header->dwSize 判定。
    const bool allowDataSpoof = InterlockedCompareExchange(&g_seenRawInputBuffer, 1, 1) == 0;
    if (result > 0 && command == RID_INPUT && data && size && *size >= sizeof(RAWINPUTHEADER))
    {
        auto* header = static_cast<RAWINPUTHEADER*>(data);
        if (header->dwType == RIM_TYPEKEYBOARD &&
            header->dwSize >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD) &&
            result >= header->dwSize)
        {
            auto* raw = static_cast<RAWINPUT*>(data);
            SharedSnapshot snapshot = {};
            bool spoofed = false;

            if (ReadSharedSnapshotCached(snapshot))
            {
                ApplyClearIfNeeded(snapshot);
                ApplyRawClearIfNeeded(snapshot);

                if (!IsBypassProcess(snapshot))
                {
                    PayloadPathDecisionInterop pathDecision = {};
                    if (!EvaluatePathDecision(snapshot, pathDecision))
                    {
                        goto SkipRawDataSpoof;
                    }

                    if (pathDecision.can_process_keys == 0)
                    {
                        int vKey = static_cast<int>(raw->data.keyboard.VKey);
                        bool isDown = false;
                        const wchar_t* reason = nullptr;
                        if (TryPickSilentRawTransition(snapshot, vKey, &vKey, &isDown, &reason))
                        {
                            BuildRawKeyboardEvent(vKey, isDown, raw->data.keyboard);
                            spoofed = true;
                            LogAdapterEmit(L"RawInputData", vKey, isDown ? 1 : 2, !isDown, isDown, reason);
                        }
                    }
                    else if (allowDataSpoof && pathDecision.should_use_mapping != 0)
                    {
                        // 映射模式下用目标键序列重写 RawInput，确保后台能收到映射后的按键事件。
                        int vKey = 0;
                        bool isDown = false;
                        const wchar_t* reason = nullptr;
                        if (TryPickMappingRawTransition(
                                snapshot,
                                pathDecision.is_alive != 0,
                                pathDecision.is_paused != 0,
                                &vKey,
                                &isDown,
                                &reason))
                        {
                            BuildRawKeyboardEvent(vKey, isDown, raw->data.keyboard);
                            spoofed = true;
                            LogAdapterEmit(L"RawInputData", vKey, isDown ? 1 : 2, !isDown, isDown, reason);
                        }
                    }
                    else
                    {
                        // 非映射模式：按 authoritative logical state 投影 transition，
                        // 不再跟随宿主原始 RawInput 重复 down 流。
                        int vKey = static_cast<int>(raw->data.keyboard.VKey);
                        if (vKey >= 0 && vKey < 256)
                        {
                            int transitionVKey = 0;
                            bool transitionDown = false;
                            const wchar_t* reason = nullptr;
                            if (TryPickLogicalRawTransition(snapshot, vKey, &transitionVKey, &transitionDown, &reason))
                            {
                                BuildRawKeyboardEvent(transitionVKey, transitionDown, raw->data.keyboard);
                                spoofed = true;
                            }
                        }
                    }
                }
            }
SkipRawDataSpoof:

            if (spoofed)
            {
                CountStat(&g_countSpoofRawInputData);
            }

            if (IsKeyLogEnabled() && GetKeyLogLevel() >= 1)
            {
                const RAWKEYBOARD& kb = raw->data.keyboard;
                const bool isDown = (kb.Flags & RI_KEY_BREAK) == 0;
                const uint32_t mode = snapshot.profileMode;
                RecordKeyEvent(KeyChannel::RawInputData, static_cast<int>(kb.VKey), isDown, spoofed, kb.MakeCode, kb.Flags, mode);
            }

            // 仅记录键盘 RawInput，便于判定按键是否只通过 RawInput 进入 DNF。
            if (ShouldLogRawInput())
            {
                const RAWKEYBOARD& kb = raw->data.keyboard;
                wchar_t buffer[256] = {0};
                StringCchPrintfW(
                    buffer,
                    ARRAYSIZE(buffer),
                    L"[RAW] %s RawInput 键盘: VKey=%u(0x%02X) MakeCode=%u(0x%02X) Flags=0x%X Spoof=%d",
                    GetTimestamp().c_str(),
                    kb.VKey,
                    kb.VKey,
                    kb.MakeCode,
                    kb.MakeCode,
                    kb.Flags,
                    spoofed ? 1 : 0);
                WriteLogLine(buffer);
            }
        }
    }

    return result;
}

static BOOL WINAPI Hook_GetMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max)
{
    CountStat(&g_countGetMessage);
    if (!g_origGetMessageW)
    {
        return FALSE;
    }

    BOOL result = g_origGetMessageW(msg, hwnd, min, max);
    if (result > 0 && msg && msg->message == WM_INPUT && msg->wParam == RIM_INPUTSINK)
    {
        FixWmInputMessage(msg);
    }

    return result;
}

static BOOL WINAPI Hook_GetMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max)
{
    CountStat(&g_countGetMessage);
    if (!g_origGetMessageA)
    {
        return FALSE;
    }

    BOOL result = g_origGetMessageA(msg, hwnd, min, max);
    if (result > 0 && msg && msg->message == WM_INPUT && msg->wParam == RIM_INPUTSINK)
    {
        FixWmInputMessage(msg);
    }

    return result;
}

static BOOL WINAPI Hook_PeekMessageW(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove)
{
    CountStat(&g_countPeekMessage);
    if (!g_origPeekMessageW)
    {
        return FALSE;
    }

    BOOL result = g_origPeekMessageW(msg, hwnd, min, max, remove);
    if (result && msg && msg->message == WM_INPUT && msg->wParam == RIM_INPUTSINK)
    {
        FixWmInputMessage(msg);
    }

    return result;
}

static BOOL WINAPI Hook_PeekMessageA(LPMSG msg, HWND hwnd, UINT min, UINT max, UINT remove)
{
    CountStat(&g_countPeekMessage);
    if (!g_origPeekMessageA)
    {
        return FALSE;
    }

    BOOL result = g_origPeekMessageA(msg, hwnd, min, max, remove);
    if (result && msg && msg->message == WM_INPUT && msg->wParam == RIM_INPUTSINK)
    {
        FixWmInputMessage(msg);
    }

    return result;
}

static HWND WINAPI Hook_GetForegroundWindow()
{
    CountStat(&g_countGetForegroundWindow);
    HWND original = g_origGetForegroundWindow ? g_origGetForegroundWindow() : nullptr;
    if (!ShouldSpoofFocus())
    {
        return original;
    }

    // 后台时伪造前台窗口，避免客户端因焦点限制丢弃输入。
    if (IsWindowOwnedBySelf(original))
    {
        return original;
    }

    HWND selfWindow = GetSelfMainWindow();
    if (!selfWindow)
    {
        return original;
    }

        CountStat(&g_countSpoofFocus);
    return selfWindow;
}

static HWND WINAPI Hook_GetActiveWindow()
{
    CountStat(&g_countGetActiveWindow);
    HWND original = g_origGetActiveWindow ? g_origGetActiveWindow() : nullptr;
    if (!ShouldSpoofFocus())
    {
        return original;
    }

    if (IsWindowOwnedBySelf(original))
    {
        return original;
    }

    HWND selfWindow = GetSelfMainWindow();
    if (!selfWindow)
    {
        return original;
    }

        CountStat(&g_countSpoofFocus);
    return selfWindow;
}

static HWND WINAPI Hook_GetFocus()
{
    CountStat(&g_countGetFocus);
    HWND original = g_origGetFocus ? g_origGetFocus() : nullptr;
    if (!ShouldSpoofFocus())
    {
        return original;
    }

    if (IsWindowOwnedBySelf(original))
    {
        return original;
    }

    HWND selfWindow = GetSelfMainWindow();
    if (!selfWindow)
    {
        return original;
    }

        CountStat(&g_countSpoofFocus);
    return selfWindow;
}

static HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(IDirectInputDevice8W* device, DWORD size, LPVOID data)
{
    CountStat(&g_countGetDeviceState);

    if (!g_origGetDeviceState)
    {
        return DIERR_GENERIC;
    }

    HRESULT hr = g_origGetDeviceState(device, size, data);
    bool forceOk = false;
    if (FAILED(hr))
    {
        CountStat(&g_countGetDeviceStateFailed);
        if (hr == DIERR_NOTACQUIRED)
        {
            CountStat(&g_countGetDeviceStateNotAcquired);
            forceOk = ShouldForceDeviceStateOk();
            if (!forceOk)
            {
                return hr;
            }
        }
        else
        {
            return hr;
        }
    }

    // DirectInput 键盘状态固定 256 字节，避免对非键盘设备误改。
    if (size < 256 || !data)
    {
        return hr;
    }
    auto* state = static_cast<BYTE*>(data);

    if (IsSpoofDelayActive())
    {
        return hr;
    }

    SharedSnapshot snapshot = {};
    if (!ReadSharedSnapshotCached(snapshot))
    {
        return hr;
    }

    ApplyClearIfNeeded(snapshot);
    PayloadPathDecisionInterop pathDecision = {};
    if (!EvaluatePathDecision(snapshot, pathDecision))
    {
        return hr;
    }

    if (pathDecision.is_bypass_process != 0)
    {
        return hr;
    }

    if (pathDecision.can_process_keys == 0)
    {
        memset(state, 0, 256);
        CountStat(&g_countSpoofDeviceState);
        if (forceOk || SUCCEEDED(hr))
        {
            return DI_OK;
        }
        return hr;
    }

    // DNF 走 DirectInput 轮询时需要覆盖 GetDeviceState，否则后台状态无法被读取到。
    EnsureVkeyToDikMap();
    bool spoofed = false;

    for (int vKey = 0; vKey < 256; vKey++)
    {
        int dik = g_vkeyToDik[vKey];
        if (dik < 0 || dik >= 256)
        {
            continue;
        }

        PayloadLogicalKeyDecisionInterop logicalDecision = {};
        if (!EvaluateLogicalKeyDecision(snapshot, vKey, logicalDecision))
        {
            continue;
        }

        if (logicalDecision.target_marked != 0)
        {
            if (logicalDecision.desired_down == 0)
            {
                state[dik] = 0;
                spoofed = true;
                RecordDirectInputKeyEventIfNeeded(vKey, false, true, snapshot.profileMode);
                continue;
            }

            state[dik] = (snapshot.keyboardState[vKey] & 0x80) ? 0x80 : 0x00;
            spoofed = true;
            RecordDirectInputKeyEventIfNeeded(vKey, (state[dik] & 0x80) != 0, true, snapshot.profileMode);
        }
        else if (logicalDecision.should_block != 0)
        {
            state[dik] = 0;
            spoofed = true;
            RecordDirectInputKeyEventIfNeeded(vKey, false, true, snapshot.profileMode);
        }
    }

    if (spoofed)
    {
        CountStat(&g_countSpoofDeviceState);
    }

    if (forceOk && spoofed)
    {
        if (InterlockedCompareExchange(&g_forceDeviceStateLogged, 1, 0) == 0)
        {
            LogInfo(L"GetDeviceState 返回 DIERR_NOTACQUIRED，已按配置强制返回 DI_OK");
        }
        return DI_OK;
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(
    IDirectInputDevice8W* device,
    DWORD objectDataSize,
    LPDIDEVICEOBJECTDATA data,
    LPDWORD entries,
    DWORD flags)
{
    CountStat(&g_countGetDeviceData);
    return g_origGetDeviceData ? g_origGetDeviceData(device, objectDataSize, data, entries, flags) : DIERR_GENERIC;
}

static HRESULT STDMETHODCALLTYPE Hook_Acquire(IDirectInputDevice8W* device)
{
    CountStat(&g_countAcquire);
    return g_origAcquire ? g_origAcquire(device) : DIERR_GENERIC;
}

static HRESULT STDMETHODCALLTYPE Hook_Unacquire(IDirectInputDevice8W* device)
{
    CountStat(&g_countUnacquire);
    return g_origUnacquire ? g_origUnacquire(device) : DIERR_GENERIC;
}

static HRESULT STDMETHODCALLTYPE Hook_Poll(IDirectInputDevice8W* device)
{
    CountStat(&g_countPoll);
    return g_origPoll ? g_origPoll(device) : DIERR_GENERIC;
}

static void LogGuid(const wchar_t* prefix, REFGUID guid)
{
    wchar_t guidText[64] = {0};
    if (StringFromGUID2(guid, guidText, ARRAYSIZE(guidText)) > 0)
    {
        LogInfo(std::wstring(prefix) + L" " + guidText);
    }
}

static void InstallDeviceHooks(IDirectInputDevice8W* device)
{
    if (!device)
    {
        return;
    }

    // vtbl 地址通常全局共享，首次 Hook 即可覆盖后续设备
    if (InterlockedCompareExchange(&g_deviceHooksHooked, 1, 0) != 0)
    {
        return;
    }

    IDirectInputDevice8WVtbl* vtbl = device->lpVtbl;
    if (!vtbl)
    {
        return;
    }

    MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(vtbl->GetDeviceState), Hook_GetDeviceState, reinterpret_cast<LPVOID*>(&g_origGetDeviceState));
    LogMinHookStatus(L"Hook GetDeviceState", status);
    if (status == MH_OK)
    {
        MH_EnableHook(reinterpret_cast<LPVOID>(vtbl->GetDeviceState));
    }

    status = MH_CreateHook(reinterpret_cast<LPVOID>(vtbl->GetDeviceData), Hook_GetDeviceData, reinterpret_cast<LPVOID*>(&g_origGetDeviceData));
    LogMinHookStatus(L"Hook GetDeviceData", status);
    if (status == MH_OK)
    {
        MH_EnableHook(reinterpret_cast<LPVOID>(vtbl->GetDeviceData));
    }

    status = MH_CreateHook(reinterpret_cast<LPVOID>(vtbl->Acquire), Hook_Acquire, reinterpret_cast<LPVOID*>(&g_origAcquire));
    LogMinHookStatus(L"Hook Acquire", status);
    if (status == MH_OK)
    {
        MH_EnableHook(reinterpret_cast<LPVOID>(vtbl->Acquire));
    }

    status = MH_CreateHook(reinterpret_cast<LPVOID>(vtbl->Unacquire), Hook_Unacquire, reinterpret_cast<LPVOID*>(&g_origUnacquire));
    LogMinHookStatus(L"Hook Unacquire", status);
    if (status == MH_OK)
    {
        MH_EnableHook(reinterpret_cast<LPVOID>(vtbl->Unacquire));
    }

    status = MH_CreateHook(reinterpret_cast<LPVOID>(vtbl->Poll), Hook_Poll, reinterpret_cast<LPVOID*>(&g_origPoll));
    LogMinHookStatus(L"Hook Poll", status);
    if (status == MH_OK)
    {
        MH_EnableHook(reinterpret_cast<LPVOID>(vtbl->Poll));
    }
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(
    IDirectInput8W* self,
    REFGUID rguid,
    LPDIRECTINPUTDEVICE8W* device,
    LPUNKNOWN unkOuter)
{
    CountStat(&g_countCreateDevice);
    LogGuid(L"CreateDevice GUID:", rguid);

    HRESULT hr = g_origCreateDevice ? g_origCreateDevice(self, rguid, device, unkOuter) : DIERR_GENERIC;
    if (SUCCEEDED(hr) && device && *device)
    {
        InstallDeviceHooks(*device);
    }
    return hr;
}

static HRESULT WINAPI Hook_DirectInput8Create(
    HINSTANCE hinst,
    DWORD version,
    REFIID riid,
    LPVOID* out,
    LPUNKNOWN unkOuter)
{
    CountStat(&g_countDirectInput8Create);

    HRESULT hr = g_origDirectInput8Create ? g_origDirectInput8Create(hinst, version, riid, out, unkOuter) : DIERR_GENERIC;
    if (SUCCEEDED(hr) && out && *out)
    {
        // 首次创建接口时再 Hook CreateDevice，避免提前拿不到 vtbl
        if (InterlockedCompareExchange(&g_createDeviceHooked, 1, 0) == 0)
        {
            auto* dinput = reinterpret_cast<IDirectInput8W*>(*out);
            if (dinput && dinput->lpVtbl)
            {
                MH_STATUS status = MH_CreateHook(
                    reinterpret_cast<LPVOID>(dinput->lpVtbl->CreateDevice),
                    Hook_CreateDevice,
                    reinterpret_cast<LPVOID*>(&g_origCreateDevice));
                LogMinHookStatus(L"Hook CreateDevice", status);
                if (status == MH_OK)
                {
                    MH_EnableHook(reinterpret_cast<LPVOID>(dinput->lpVtbl->CreateDevice));
                }
            }
        }
    }

    return hr;
}

// ------------------------------
// Hook 初始化
// ------------------------------

static void InstallUser32Hooks()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
    {
        LogError(L"user32.dll 未加载，无法安装 Win32 Hook");
        return;
    }

    auto* getAsync = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetAsyncKeyState"));
    if (getAsync)
    {
        MH_STATUS status = MH_CreateHook(getAsync, Hook_GetAsyncKeyState, reinterpret_cast<LPVOID*>(&g_origGetAsyncKeyState));
        LogMinHookStatus(L"Hook GetAsyncKeyState", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getAsync);
        }
    }

    auto* getKeyboard = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetKeyboardState"));
    if (getKeyboard)
    {
        MH_STATUS status = MH_CreateHook(getKeyboard, Hook_GetKeyboardState, reinterpret_cast<LPVOID*>(&g_origGetKeyboardState));
        LogMinHookStatus(L"Hook GetKeyboardState", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getKeyboard);
        }
    }

    auto* regRaw = reinterpret_cast<LPVOID>(GetProcAddress(user32, "RegisterRawInputDevices"));
    if (regRaw)
    {
        MH_STATUS status = MH_CreateHook(regRaw, Hook_RegisterRawInputDevices, reinterpret_cast<LPVOID*>(&g_origRegisterRawInputDevices));
        LogMinHookStatus(L"Hook RegisterRawInputDevices", status);
        if (status == MH_OK)
        {
            MH_EnableHook(regRaw);
        }
    }

    auto* rawData = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetRawInputData"));
    if (rawData)
    {
        MH_STATUS status = MH_CreateHook(rawData, Hook_GetRawInputData, reinterpret_cast<LPVOID*>(&g_origGetRawInputData));
        LogMinHookStatus(L"Hook GetRawInputData", status);
        if (status == MH_OK)
        {
            MH_EnableHook(rawData);
        }
    }

    auto* rawBuffer = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetRawInputBuffer"));
    if (rawBuffer)
    {
        MH_STATUS status = MH_CreateHook(rawBuffer, Hook_GetRawInputBuffer, reinterpret_cast<LPVOID*>(&g_origGetRawInputBuffer));
        LogMinHookStatus(L"Hook GetRawInputBuffer", status);
        if (status == MH_OK)
        {
            MH_EnableHook(rawBuffer);
        }
    }

    auto* getMessageW = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetMessageW"));
    if (getMessageW)
    {
        MH_STATUS status = MH_CreateHook(getMessageW, Hook_GetMessageW, reinterpret_cast<LPVOID*>(&g_origGetMessageW));
        LogMinHookStatus(L"Hook GetMessageW", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getMessageW);
        }
    }

    auto* getMessageA = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetMessageA"));
    if (getMessageA)
    {
        MH_STATUS status = MH_CreateHook(getMessageA, Hook_GetMessageA, reinterpret_cast<LPVOID*>(&g_origGetMessageA));
        LogMinHookStatus(L"Hook GetMessageA", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getMessageA);
        }
    }

    auto* peekMessageW = reinterpret_cast<LPVOID>(GetProcAddress(user32, "PeekMessageW"));
    if (peekMessageW)
    {
        MH_STATUS status = MH_CreateHook(peekMessageW, Hook_PeekMessageW, reinterpret_cast<LPVOID*>(&g_origPeekMessageW));
        LogMinHookStatus(L"Hook PeekMessageW", status);
        if (status == MH_OK)
        {
            MH_EnableHook(peekMessageW);
        }
    }

    auto* peekMessageA = reinterpret_cast<LPVOID>(GetProcAddress(user32, "PeekMessageA"));
    if (peekMessageA)
    {
        MH_STATUS status = MH_CreateHook(peekMessageA, Hook_PeekMessageA, reinterpret_cast<LPVOID*>(&g_origPeekMessageA));
        LogMinHookStatus(L"Hook PeekMessageA", status);
        if (status == MH_OK)
        {
            MH_EnableHook(peekMessageA);
        }
    }

    auto* getForeground = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetForegroundWindow"));
    if (getForeground)
    {
        MH_STATUS status = MH_CreateHook(getForeground, Hook_GetForegroundWindow, reinterpret_cast<LPVOID*>(&g_origGetForegroundWindow));
        LogMinHookStatus(L"Hook GetForegroundWindow", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getForeground);
        }
    }

    auto* getActive = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetActiveWindow"));
    if (getActive)
    {
        MH_STATUS status = MH_CreateHook(getActive, Hook_GetActiveWindow, reinterpret_cast<LPVOID*>(&g_origGetActiveWindow));
        LogMinHookStatus(L"Hook GetActiveWindow", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getActive);
        }
    }

    auto* getFocus = reinterpret_cast<LPVOID>(GetProcAddress(user32, "GetFocus"));
    if (getFocus)
    {
        MH_STATUS status = MH_CreateHook(getFocus, Hook_GetFocus, reinterpret_cast<LPVOID*>(&g_origGetFocus));
        LogMinHookStatus(L"Hook GetFocus", status);
        if (status == MH_OK)
        {
            MH_EnableHook(getFocus);
        }
    }
}

static void InstallDirectInputHook()
{
    HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
    if (!dinput)
    {
        // 确保 dinput8 已加载，方便取得导出函数地址
        dinput = LoadLibraryW(L"dinput8.dll");
    }

    if (!dinput)
    {
        LogError(L"dinput8.dll 未加载，无法 Hook DirectInput8Create");
        return;
    }

    auto* proc = reinterpret_cast<LPVOID>(GetProcAddress(dinput, "DirectInput8Create"));
    if (!proc)
    {
        LogError(L"未找到 DirectInput8Create 导出");
        return;
    }

    MH_STATUS status = MH_CreateHook(proc, Hook_DirectInput8Create, reinterpret_cast<LPVOID*>(&g_origDirectInput8Create));
    LogMinHookStatus(L"Hook DirectInput8Create", status);
    if (status == MH_OK)
    {
        MH_EnableHook(proc);
    }
}

static const wchar_t* ResolveObservedInputChannel(uint32_t channelKind)
{
    switch (channelKind)
    {
        case 1:
            return L"Win32";
        case 2:
            return L"RawInput";
        case 3:
            return L"DirectInput";
        default:
            return L"Unknown";
    }
}

static bool EvaluateInputPathObservation(
    LONG rawPromoted,
    LONG rawData,
    LONG rawBuffer,
    LONG diState,
    LONG diData,
    LONG win32Async,
    LONG win32Keyboard,
    PayloadInputPathObservationInterop& observation)
{
    memset(&observation, 0, sizeof(observation));
    return payload_core_observe_input_path(
               rawPromoted > 0 ? 1u : 0u,
               rawData > 0 ? static_cast<uint32_t>(rawData) : 0u,
               rawBuffer > 0 ? static_cast<uint32_t>(rawBuffer) : 0u,
               diState > 0 ? static_cast<uint32_t>(diState) : 0u,
               diData > 0 ? static_cast<uint32_t>(diData) : 0u,
               win32Async > 0 ? static_cast<uint32_t>(win32Async) : 0u,
               win32Keyboard > 0 ? static_cast<uint32_t>(win32Keyboard) : 0u,
               g_lastProfileId,
               g_lastProfileMode,
               &observation) != 0;
}

static bool EvaluateAdapterProjectedState(
    bool desiredDown,
    bool rawProjected,
    bool win32Projected,
    bool directInputProjected,
    PayloadAdapterProjectedStateInterop& state)
{
    memset(&state, 0, sizeof(state));
    return payload_core_evaluate_adapter_projected_state(
               desiredDown ? 1u : 0u,
               rawProjected ? 1u : 0u,
               win32Projected ? 1u : 0u,
               directInputProjected ? 1u : 0u,
               &state) != 0;
}

static void LogCountersOnce()
{
    const bool statsEnabled = IsStatsEnabled();
    const bool diagEnabled = IsDiagEnabled();
    if (!statsEnabled && !diagEnabled)
    {
        return;
    }

    static ULONGLONG lastStatsTick = 0;
    static ULONGLONG lastDiagTick = 0;
    ULONGLONG now = GetTickCount64();

    bool doStats = false;
    if (statsEnabled)
    {
        DWORD interval = GetStatsIntervalMs();
        if (interval == 0 || now - lastStatsTick >= interval)
        {
            doStats = true;
            lastStatsTick = now;
        }
    }

    bool doDiag = false;
    if (diagEnabled)
    {
        DWORD interval = GetDiagIntervalMs();
        if (interval == 0 || now - lastDiagTick >= interval)
        {
            doDiag = true;
            lastDiagTick = now;
        }
    }

    if (!doStats && !doDiag)
    {
        return;
    }

    // 输出统计/诊断，避免在高频回调里写日志造成干扰
    LONG getAsync = InterlockedExchange(&g_countGetAsyncKeyState, 0);
    LONG getKeyboard = InterlockedExchange(&g_countGetKeyboardState, 0);
    LONG diCreate = InterlockedExchange(&g_countDirectInput8Create, 0);
    LONG createDevice = InterlockedExchange(&g_countCreateDevice, 0);
    LONG getState = InterlockedExchange(&g_countGetDeviceState, 0);
    LONG getStateFailed = InterlockedExchange(&g_countGetDeviceStateFailed, 0);
    LONG getStateNotAcquired = InterlockedExchange(&g_countGetDeviceStateNotAcquired, 0);
    LONG getData = InterlockedExchange(&g_countGetDeviceData, 0);
    LONG acquire = InterlockedExchange(&g_countAcquire, 0);
    LONG poll = InterlockedExchange(&g_countPoll, 0);
    LONG unacquire = InterlockedExchange(&g_countUnacquire, 0);
    LONG rawRegister = InterlockedExchange(&g_countRegisterRawInput, 0);
    LONG rawData = InterlockedExchange(&g_countGetRawInputData, 0);
    LONG rawBuffer = InterlockedExchange(&g_countGetRawInputBuffer, 0);
    LONG spoofRawData = InterlockedExchange(&g_countSpoofRawInputData, 0);
    LONG spoofRawBuffer = InterlockedExchange(&g_countSpoofRawInputBuffer, 0);
    LONG getMessage = InterlockedExchange(&g_countGetMessage, 0);
    LONG peekMessage = InterlockedExchange(&g_countPeekMessage, 0);
    LONG spoofWmInput = InterlockedExchange(&g_countSpoofWmInput, 0);
    LONG getForeground = InterlockedExchange(&g_countGetForegroundWindow, 0);
    LONG getActive = InterlockedExchange(&g_countGetActiveWindow, 0);
    LONG getFocus = InterlockedExchange(&g_countGetFocus, 0);
    LONG spoofFocus = InterlockedExchange(&g_countSpoofFocus, 0);
    LONG spoofAsync = InterlockedExchange(&g_countSpoofAsync, 0);
    LONG spoofKeyboard = InterlockedExchange(&g_countSpoofKeyboard, 0);
    LONG spoofDeviceState = InterlockedExchange(&g_countSpoofDeviceState, 0);

    if (doStats)
    {
        wchar_t buffer[720] = {0};
        StringCchPrintfW(
            buffer,
            ARRAYSIZE(buffer),
            L"[STAT] %s Win32: GetAsyncKeyState=%ld GetKeyboardState=%ld SpoofAsync=%ld SpoofKeyboard=%ld | Focus: Foreground=%ld Active=%ld Focus=%ld Spoof=%ld | DirectInput: DirectInput8Create=%ld CreateDevice=%ld GetDeviceState=%ld Fail=%ld NotAcquired=%ld GetDeviceData=%ld Acquire=%ld Poll=%ld Unacquire=%ld SpoofDI=%ld | RawInput: Register=%ld GetRawInputData=%ld GetRawInputBuffer=%ld | Msg: GetMessage=%ld PeekMessage=%ld SpoofWmInput=%ld | Profile=%lu Mode=%lu",
            GetTimestamp().c_str(),
            getAsync,
            getKeyboard,
            spoofAsync,
            spoofKeyboard,
            getForeground,
            getActive,
            getFocus,
            spoofFocus,
            diCreate,
            createDevice,
            getState,
            getStateFailed,
            getStateNotAcquired,
            getData,
            acquire,
            poll,
            unacquire,
            spoofDeviceState,
            rawRegister,
            rawData,
            rawBuffer,
            getMessage,
            peekMessage,
            spoofWmInput,
            g_lastProfileId,
            g_lastProfileMode);

        WriteLogLine(buffer);
    }

    if (doDiag)
    {
        SharedSnapshotLite lite = {};
        const bool hasSnapshot = ReadSharedSnapshotLiteCached(lite);
        const bool alive = hasSnapshot && IsSnapshotAliveLite(lite);
        const bool paused = hasSnapshot && ((lite.flags & kFlagPaused) != 0);
        const uint32_t activePid = hasSnapshot ? lite.activePid : 0;
        const DWORD selfPid = GetCurrentProcessId();
        const wchar_t* role = L"Unknown";
        if (activePid != 0)
        {
            role = (activePid == selfPid) ? L"Master" : L"Slave";
        }
        PayloadInputPathObservationInterop observation = {};
        EvaluateInputPathObservation(
            spoofWmInput,
            rawData,
            rawBuffer,
            getState,
            getData,
            getAsync,
            getKeyboard,
            observation);
        const wchar_t* channel = ResolveObservedInputChannel(observation.channel_kind);

        wchar_t buffer[640] = {0};
        StringCchPrintfW(
            buffer,
            ARRAYSIZE(buffer),
            L"[DIAG] %s Role=%s ActivePid=%lu Alive=%d Paused=%d Channel=%s | Win32: Async=%ld Keyboard=%ld SpoofAsync=%ld SpoofKeyboard=%ld | RawInput: Data=%ld Buffer=%ld SpoofData=%ld SpoofBuffer=%ld | DirectInput: GetState=%ld GetData=%ld SpoofDI=%ld | Profile=%lu Mode=%lu",
            GetTimestamp().c_str(),
            role,
            activePid,
            alive ? 1 : 0,
            paused ? 1 : 0,
            channel,
            getAsync,
            getKeyboard,
            spoofAsync,
            spoofKeyboard,
            rawData,
            rawBuffer,
            spoofRawData,
            spoofRawBuffer,
            getState,
            getData,
            spoofDeviceState,
            g_lastProfileId,
            g_lastProfileMode);

        WriteLogLine(buffer);

        PayloadAdapterDriftSummaryInterop driftSummary = {};
        payload_core_summarize_adapter_drift(
            g_lastLogicalDesiredState,
            g_lastRawKeyboardState,
            g_lastWin32State,
            g_lastDIState,
            256,
            &driftSummary);

        wchar_t obs[512] = {0};
        StringCchPrintfW(
            obs,
            ARRAYSIZE(obs),
            L"[OBS] %s channel=%s mixed=%u raw_promoted=%u raw_active=%u di_active=%u win32_active=%u drift_raw=%u drift_win32=%u drift_di=%u raw_data=%ld raw_buffer=%ld di_state=%ld di_data=%ld win32_async=%ld win32_keyboard=%ld profile=%lu mode=%lu",
            GetTimestamp().c_str(),
            channel,
            observation.mixed_inputs,
            observation.raw_promoted,
            observation.raw_active,
            observation.direct_input_active,
            observation.win32_active,
            driftSummary.raw_drift_count,
            driftSummary.win32_drift_count,
            driftSummary.direct_input_drift_count,
            rawData,
            rawBuffer,
            getState,
            getData,
            getAsync,
            getKeyboard,
            g_lastProfileId,
            g_lastProfileMode);
        WriteLogLine(obs);
    }
}

static DWORD WINAPI WorkerThread(LPVOID)
{
    // 所有耗时与高风险操作都放在工作线程，避免 DllMain 触发 Loader Lock
    InitializeLogging();
    // 成功文件写入：避免在 DllMain 中做 I/O，降低 Loader Lock 风险
    WriteSuccessFile();
    InitializeSpoofDelay();

    LogInfo(L"工作线程启动，准备初始化 MinHook 与输入路径统计");

    MH_STATUS status = MH_Initialize();
    LogMinHookStatus(L"MinHook 初始化", status);
    if (status != MH_OK)
    {
        LogError(L"MinHook 初始化失败，终止 Hook 安装");
        return 0;
    }

    InstallUser32Hooks();
    InstallDirectInputHook();

    LogInfo(L"Hook 安装完成，开始统计调用频率");

    // 后台定期刷新窗口缓存，避免 Hook 回调中执行 EnumWindows
    UpdateSelfWindowCache();

    // 暂时禁用抹头逻辑，便于调试与稳定性验证。
#if 0
    // 抹头放在 Hook 初始化之后，避免影响需要解析 PE 的逻辑
    if (ErasePeHeader(g_module))
    {
        LogInfo(L"抹头完成（PE Header 已清零）");
    }
    else
    {
        LogError(L"抹头失败或被跳过");
    }
#endif

    while (InterlockedCompareExchange(&g_shouldStop, 0, 0) == 0)
    {
        Sleep(1000);
        UpdateSelfWindowCache();
        CheckKeyUpTimeouts();
        LogCountersOnce();
    }

    LogInfo(L"工作线程退出");
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(g_logFile);
    }
    ArchiveLogFile();
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
    return 0;
}

static void StartSyncInternal()
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&StartSyncInternal),
        &module))
    {
        g_module = module;
        // 记录注入时间，用于伪造延迟计时。
        g_injectTick = GetTickCount64();
        DisableThreadLibraryCalls(module);

        // 断链：降低被模块枚举发现的概率
        UnlinkFromPeb(module);
    }

    HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (thread)
    {
        CloseHandle(thread);
    }
}

namespace Sync {
    void Start()
    {
        StartSyncInternal();
    }

    void Stop()
    {
        InterlockedExchange(&g_shouldStop, 1);
        RemoveSuccessFile();
    }
}
