#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <cwctype>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <limits>
#include "game_injector_core_ffi.h"

#ifdef _DEBUG
#include <algorithm>
#endif

struct InjectorConfig {
    std::wstring process_name;
    std::wstring dll_path;
    std::wstring output_dir;
    DWORD scan_interval_ms = 1000;
    DWORD inject_delay_ms = 2000;
    DWORD window_wait_timeout_ms = 30000;
    DWORD window_poll_interval_ms = 500;
    DWORD post_window_delay_ms = 10000;
    int max_retries = 3;
    DWORD retry_interval_ms = 1000;
    DWORD success_timeout_ms = 6000;
    DWORD success_interval_ms = 200;
    DWORD heartbeat_timeout_ms = 6000;
    DWORD heartbeat_interval_ms = 200;
    DWORD inject_backend = 1;
    DWORD success_observer_mode = 1;
    bool watch_mode = true;
    DWORD idle_exit_seconds = 600;
    DWORD max_concurrent_tasks = 3;
};

#pragma pack(push, 1)
struct HelperStatusV5 {
    uint32_t Version;
    uint32_t Size;
    uint64_t LastTickMs;
    uint32_t Pid;
    int32_t ProcessAlive;
    int32_t AutoTransparentEnabled;
    int32_t FullscreenAttackTarget;
    int32_t FullscreenAttackPatchOn;
    int32_t AttractMode;
    int32_t AttractPositive;
    int32_t GatherItemsEnabled;
    int32_t DamageEnabled;
    int32_t DamageMultiplier;
    int32_t InvincibleEnabled;
    int32_t SummonEnabled;
    uint64_t SummonLastTick;
    int32_t FullscreenSkillEnabled;
    int32_t FullscreenSkillActive;
    uint32_t FullscreenSkillHotkey;
    int32_t HotkeyEnabled;
    wchar_t PlayerName[32];
};
#pragma pack(pop)

static_assert(sizeof(HelperStatusV5) == 152, "HelperStatusV5 size mismatch");

static InjectorConfig BuildDefaultInjectorConfig() {
    InjectorConfigView defaults = injector_core_default_view();
    InjectorConfig config;
    config.process_name = L"DNF.exe";
    config.dll_path = L"game-payload.dll";
    config.output_dir = L"";
    config.scan_interval_ms = defaults.scan_interval_ms;
    config.inject_delay_ms = defaults.inject_delay_ms;
    config.window_wait_timeout_ms = defaults.window_wait_timeout_ms;
    config.window_poll_interval_ms = defaults.window_poll_interval_ms;
    config.post_window_delay_ms = defaults.post_window_delay_ms;
    config.max_retries = static_cast<int>(defaults.max_retries);
    config.retry_interval_ms = defaults.retry_interval_ms;
    config.success_timeout_ms = defaults.success_timeout_ms;
    config.success_interval_ms = defaults.success_interval_ms;
    config.heartbeat_timeout_ms = defaults.heartbeat_timeout_ms;
    config.heartbeat_interval_ms = defaults.heartbeat_interval_ms;
    config.inject_backend = defaults.inject_backend;
    config.success_observer_mode = defaults.success_observer_mode;
    config.watch_mode = defaults.watch_mode != 0;
    config.idle_exit_seconds = defaults.idle_exit_seconds;
    config.max_concurrent_tasks = defaults.max_concurrent_tasks;
    return config;
}

static std::string GetRustDefaultInjectorConfigUtf8() {
    const char* text = reinterpret_cast<const char*>(injector_core_default_ini_text_utf8());
    size_t length = injector_core_default_ini_text_utf8_len();
    if (!text || length == 0) {
        return {};
    }
    return std::string(text, text + length);
}

static bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>* output) {
    if (!output) {
        return false;
    }
    output->clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(64 * 1024)) {
        CloseHandle(file);
        return false;
    }
    output->resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = output->empty() ? TRUE : ReadFile(file, output->data(), static_cast<DWORD>(output->size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        output->clear();
        return false;
    }
    output->resize(read);
    return true;
}

static std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return L"";
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &wide[0], length);
    return wide;
}

static std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &utf8[0], length, nullptr, nullptr);
    return utf8;
}

static std::string ReadCStringFromBuffer(const char* buffer, size_t capacity) {
    if (!buffer || capacity == 0) {
        return {};
    }
    size_t length = 0;
    while (length < capacity && buffer[length] != '\0') {
        ++length;
    }
    return std::string(buffer, buffer + length);
}

static bool CopyUtf8CString(char* dest, size_t capacity, const std::string& value) {
    if (!dest || capacity == 0 || value.size() >= capacity) {
        return false;
    }
    memset(dest, 0, capacity);
    memcpy(dest, value.data(), value.size());
    return true;
}

static std::wstring GetExeDirectory() {
    wchar_t buffer[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }
    wchar_t* last_slash = wcsrchr(buffer, L'\\');
    if (!last_slash) {
        last_slash = wcsrchr(buffer, L'/');
    }
    if (!last_slash) {
        return L"";
    }
    *(last_slash + 1) = L'\0';
    return buffer;
}

static bool IsAbsolutePath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        return true;
    }
    if (!path.empty() && (path[0] == L'\\' || path[0] == L'/')) {
        return true;
    }
    return false;
}

static std::wstring JoinPathSafe(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    std::wstring result = left;
    wchar_t last = result[result.size() - 1];
    if (last != L'\\' && last != L'/') {
        result.push_back(L'\\');
    }
    result.append(right);
    return result;
}

static std::wstring NormalizePath(const std::wstring& path, const std::wstring& base_dir) {
    if (path.empty()) {
        return path;
    }
    std::wstring candidate = path;
    if (!IsAbsolutePath(candidate)) {
        candidate = JoinPathSafe(base_dir, candidate);
    }
    wchar_t full_path[MAX_PATH] = {0};
    DWORD length = GetFullPathNameW(candidate.c_str(), MAX_PATH, full_path, nullptr);
    if (length == 0 || length >= MAX_PATH) {
        return candidate;
    }
    return full_path;
}

static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool BuildInjectorConfigInterop(const InjectorConfig& config, InjectorConfigInterop* out) {
    if (!out) {
        return false;
    }
    ZeroMemory(out, sizeof(*out));

    std::string process_name = WideToUtf8(config.process_name);
    std::string dll_path = WideToUtf8(config.dll_path);
    std::string output_dir = WideToUtf8(config.output_dir);
    if (!CopyUtf8CString(out->process_name, sizeof(out->process_name), process_name)) {
        return false;
    }
    if (!CopyUtf8CString(out->dll_path, sizeof(out->dll_path), dll_path)) {
        return false;
    }
    if (!CopyUtf8CString(out->output_dir, sizeof(out->output_dir), output_dir)) {
        return false;
    }

    out->view.scan_interval_ms = config.scan_interval_ms;
    out->view.inject_delay_ms = config.inject_delay_ms;
    out->view.window_wait_timeout_ms = config.window_wait_timeout_ms;
    out->view.window_poll_interval_ms = config.window_poll_interval_ms;
    out->view.post_window_delay_ms = config.post_window_delay_ms;
    out->view.max_retries = static_cast<uint32_t>(config.max_retries);
    out->view.retry_interval_ms = config.retry_interval_ms;
    out->view.success_timeout_ms = config.success_timeout_ms;
    out->view.success_interval_ms = config.success_interval_ms;
    out->view.heartbeat_timeout_ms = config.heartbeat_timeout_ms;
    out->view.heartbeat_interval_ms = config.heartbeat_interval_ms;
    out->view.inject_backend = config.inject_backend;
    out->view.success_observer_mode = config.success_observer_mode;
    out->view.watch_mode = config.watch_mode ? 1u : 0u;
    out->view.idle_exit_seconds = config.idle_exit_seconds;
    out->view.max_concurrent_tasks = config.max_concurrent_tasks;
    return true;
}

static bool EnsureDirectoryExists(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    if (CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }
    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return false;
}

static bool WriteTextFileUtf8(const std::wstring& path, const std::string& content) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    // UTF-8 BOM
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    WriteFile(file, bom, sizeof(bom), &written, nullptr);
    if (!content.empty()) {
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    }
    CloseHandle(file);
    return true;
}

static std::wstring GetFileDirectory(const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L"";
    }
    return path.substr(0, pos + 1);
}

static std::wstring GetBaseName(const std::wstring& path) {
    if (path.empty()) {
        return L"";
    }
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

static std::wstring GetBaseNameWithoutExtension(const std::wstring& path) {
    std::wstring base = GetBaseName(path);
    size_t dot = base.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return base;
    }
    return base.substr(0, dot);
}

static std::wstring GetConfigDirectory(const std::wstring& base_dir) {
    return JoinPathSafe(base_dir, L"config");
}

static std::wstring GetInjectorConfigPath(const std::wstring& base_dir) {
    std::wstring config_dir = GetConfigDirectory(base_dir);
    if (EnsureDirectoryExists(config_dir)) {
        return JoinPathSafe(config_dir, L"injector.ini");
    }
    return JoinPathSafe(base_dir, L"injector.ini");
}

static void EnsureDefaultInjectorConfig(const std::wstring& config_path) {
    if (FileExists(config_path)) {
        return;
    }
    std::string content = GetRustDefaultInjectorConfigUtf8();
    if (content.empty()) {
        content =
            "[injector]\r\n"
            "; 目标进程名（不区分大小写，自动补 .exe）\r\n"
            "process_name=DNF.exe\r\n"
            "; DLL 路径（默认相对注入器输出目录）\r\n"
            "dll_path=game-payload.dll\r\n"
            "; 成功文件目录（为空则使用 DLL\\\\logs 目录）\r\n"
            "; output_dir=\r\n"
            "; 扫描进程间隔（毫秒）\r\n"
            "scan_interval_ms=1000\r\n"
            "; 等待窗口出现的超时（毫秒）\r\n"
            "window_wait_timeout_ms=30000\r\n"
            "; 窗口检测轮询间隔（毫秒）\r\n"
            "window_poll_interval_ms=500\r\n"
            "; 窗口出现后强制等待时间（毫秒）\r\n"
            "post_window_delay_ms=10000\r\n"
            "; 发现窗口后额外延迟（毫秒，可选）\r\n"
            "inject_delay_ms=0\r\n"
            "; 重试次数与间隔\r\n"
            "max_retries=3\r\n"
            "retry_interval_ms=1000\r\n"
            "; 成功文件检测\r\n"
            "success_timeout_ms=6000\r\n"
            "success_interval_ms=200\r\n"
            "; 共享内存心跳兜底\r\n"
            "heartbeat_timeout_ms=6000\r\n"
            "heartbeat_interval_ms=200\r\n"
            "; 注入后端：apc / fallback（当前默认 apc）\r\n"
            "inject_backend=apc\r\n"
            "; successfile 观察方式：notify / poll\r\n"
            "success_observer_mode=notify\r\n"
            "; 常驻监听模式\r\n"
            "watch_mode=true\r\n"
            "; 无新目标进程出现后自动退出（秒，0 表示不退出）\r\n"
            "idle_exit_seconds=600\r\n"
            "; 并发注入任务上限（0 表示不限制）\r\n"
            "max_concurrent_tasks=3\r\n";
    }
    WriteTextFileUtf8(config_path, content);
}

static std::wstring ReadIniStringValue(const std::wstring& path, const wchar_t* key, const wchar_t* default_value) {
    wchar_t buffer[512] = {0};
    DWORD read = GetPrivateProfileStringW(L"injector", key, default_value, buffer,
        static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());
    return std::wstring(buffer, buffer + read);
}

static DWORD ReadIniUInt32(const std::wstring& path, const wchar_t* key, DWORD default_value) {
    std::wstring value = ReadIniStringValue(path, key, L"");
    if (value.empty()) {
        return default_value;
    }
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return default_value;
    }
    return static_cast<DWORD>(parsed);
}

static bool ReadIniBool(const std::wstring& path, const wchar_t* key, bool default_value) {
    std::wstring value = ReadIniStringValue(path, key, default_value ? L"true" : L"false");
    if (value.empty()) {
        return default_value;
    }
    if (_wcsicmp(value.c_str(), L"1") == 0 || _wcsicmp(value.c_str(), L"true") == 0 ||
        _wcsicmp(value.c_str(), L"yes") == 0 || _wcsicmp(value.c_str(), L"on") == 0) {
        return true;
    }
    if (_wcsicmp(value.c_str(), L"0") == 0 || _wcsicmp(value.c_str(), L"false") == 0 ||
        _wcsicmp(value.c_str(), L"no") == 0 || _wcsicmp(value.c_str(), L"off") == 0) {
        return false;
    }
    return default_value;
}

struct WindowCheckCtx {
    DWORD target_pid = 0;
    bool found = false;
};

static BOOL CALLBACK EnumWindowCheckProc(HWND hwnd, LPARAM lparam) {
    auto* ctx = reinterpret_cast<WindowCheckCtx*>(lparam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->target_pid) {
        return TRUE;
    }
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    ctx->found = true;
    return FALSE;
}

static bool WaitForProcessWindow(DWORD pid, DWORD timeout_ms, DWORD poll_interval_ms) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start <= timeout_ms) {
        WindowCheckCtx ctx;
        ctx.target_pid = pid;
        EnumWindows(EnumWindowCheckProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.found) {
            return true;
        }
        Sleep(poll_interval_ms);
    }
    return false;
}

static bool TryWaitForInputIdleHint(DWORD pid, DWORD timeout_ms) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }
    DWORD wait = WaitForInputIdle(process, timeout_ms);
    CloseHandle(process);
    return wait == 0;
}

static InjectorWindowProbeResultInterop ProbeProcessWindowReady(DWORD pid, DWORD timeout_ms, DWORD poll_interval_ms) {
    InjectorWindowProbeResultInterop result = {};
    DWORD idle_timeout = timeout_ms > 5000 ? 5000 : timeout_ms;
    bool usedHint = false;
    if (idle_timeout > 0) {
        usedHint = TryWaitForInputIdleHint(pid, idle_timeout);
    }
    result.used_hint = usedHint ? 1u : 0u;
    result.ok = WaitForProcessWindow(pid, timeout_ms, poll_interval_ms) ? 1u : 0u;
    result.timed_out = result.ok == 0 ? 1u : 0u;
    result.error_code = result.ok != 0 ? 0u : WAIT_TIMEOUT;
    return result;
}

static std::wstring NormalizeProcessName(const std::wstring& name) {
    if (name.empty()) {
        return name;
    }
    std::wstring normalized = name;
    if (normalized.size() >= 4) {
        std::wstring tail = normalized.substr(normalized.size() - 4);
        for (auto& ch : tail) {
            ch = static_cast<wchar_t>(towlower(ch));
        }
        if (tail == L".exe") {
            return normalized;
        }
    }
    return normalized + L".exe";
}

static bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
#ifdef _DEBUG
    std::wstring l = left;
    std::wstring r = right;
    std::transform(l.begin(), l.end(), l.begin(), towlower);
    std::transform(r.begin(), r.end(), r.begin(), towlower);
    return l == r;
#else
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
#endif
}

#ifdef _DEBUG
static std::wstring g_debug_log_path;
static std::wstring TrimWide(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && iswspace(value[start])) {
        start++;
    }
    size_t end = value.size();
    while (end > start && iswspace(value[end - 1])) {
        end--;
    }
    return value.substr(start, end - start);
}

static std::wstring GetSessionIdFromLogs(const std::wstring& base_dir) {
    std::wstring logs_dir = JoinPathSafe(base_dir, L"logs");
    std::wstring session_path = JoinPathSafe(logs_dir, L"session.current");
    HANDLE file = CreateFileW(session_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return L"";
    }
    char buffer[64] = {0};
    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) {
        return L"";
    }
    buffer[read] = '\0';
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, nullptr, 0);
    if (wlen <= 1) {
        return L"";
    }
    std::wstring wide(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buffer, -1, &wide[0], wlen - 1);
    return TrimWide(wide);
}

static std::wstring BuildSessionIdNow() {
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t buffer[32] = {0};
    swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return std::wstring(buffer);
}

static std::wstring GetDebugLogPath() {
    std::wstring base_dir = GetExeDirectory();
    std::wstring log_dir = JoinPathSafe(JoinPathSafe(base_dir, L"logs"), L"injector");
    CreateDirectoryW(log_dir.c_str(), nullptr);
    std::wstring session_id = GetSessionIdFromLogs(base_dir);
    if (session_id.empty()) {
        session_id = BuildSessionIdNow();
    }
    wchar_t file_name[128] = {0};
    swprintf_s(file_name, L"injector_%s_%lu.log", session_id.c_str(), GetCurrentProcessId());
    return JoinPathSafe(log_dir, file_name);
}

static void AppendDebugLog(const std::wstring& message) {
    if (g_debug_log_path.empty()) {
        g_debug_log_path = GetDebugLogPath();
    }
    HANDLE file = CreateFileW(g_debug_log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t prefix[64] = {0};
    swprintf_s(prefix, L"%04u-%02u-%02u %02u:%02u:%02u.%03u ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    std::wstring line = prefix + message + L"\r\n";
    int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
        std::vector<char> utf8(size - 1);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), size - 1, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

static void ArchiveDebugLogIfNeeded() {
    if (g_debug_log_path.empty()) {
        return;
    }
    std::wstring base_dir = GetExeDirectory();
    std::wstring session_id = GetSessionIdFromLogs(base_dir);
    if (session_id.empty()) {
        return;
    }
    std::wstring session_dir = JoinPathSafe(JoinPathSafe(base_dir, L"logs"), L"session_" + session_id);
    CreateDirectoryW(session_dir.c_str(), nullptr);
    std::wstring file_name = GetBaseName(g_debug_log_path);
    if (file_name.empty()) {
        return;
    }
    std::wstring dest = JoinPathSafe(session_dir, file_name);
    CopyFileW(g_debug_log_path.c_str(), dest.c_str(), FALSE);
}
#endif

static void Log(const std::wstring& message) {
    std::wcout << L"[Injector] " << message << std::endl;
#ifdef _DEBUG
    AppendDebugLog(message);
#endif
}

static bool ValidateRustContracts(std::wstring* error_message) {
    uint32_t rust_helper_status_size = injector_core_helper_status_size();
    uint32_t rust_helper_status_version = injector_core_helper_status_version();
    if (rust_helper_status_size != sizeof(HelperStatusV5)) {
        if (error_message) {
            *error_message = L"Rust HelperStatus 合约尺寸不一致：Rust="
                + std::to_wstring(rust_helper_status_size)
                + L", C++=" + std::to_wstring(sizeof(HelperStatusV5));
        }
        return false;
    }
    if (rust_helper_status_version != 5) {
        if (error_message) {
            *error_message = L"Rust HelperStatus 合约版本不一致：Rust="
                + std::to_wstring(rust_helper_status_version)
                + L", 预期=5";
        }
        return false;
    }
    return true;
}

static bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    TOKEN_PRIVILEGES tp = {};
    LUID luid = {};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return ok && GetLastError() == ERROR_SUCCESS;
}

static DWORD FindProcessId(const std::wstring& process_name) {
    std::wstring normalized = NormalizeProcessName(process_name);
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (EqualsInsensitive(entry.szExeFile, normalized)) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

static std::vector<DWORD> ListProcessIds(const std::wstring& process_name) {
    std::wstring normalized = NormalizeProcessName(process_name);
    std::vector<DWORD> result;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (EqualsInsensitive(entry.szExeFile, normalized)) {
                result.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

static std::wstring BuildSuccessFilePath(const std::wstring& dll_path, const std::wstring& output_dir, DWORD pid) {
	std::wstring dir = output_dir;
	if (dir.empty()) {
		dir = GetFileDirectory(dll_path);
		if (!dir.empty()) {
			dir = JoinPathSafe(dir, L"logs");
		}
	}
	if (dir.empty()) {
		return L"";
	}
    std::wstring base_name = GetBaseNameWithoutExtension(dll_path);
    if (base_name.empty()) {
        return L"";
    }
    wchar_t file_name[128] = {0};
    if (swprintf_s(file_name, L"successfile_%s_%lu.txt", base_name.c_str(), pid) <= 0) {
        return L"";
    }
    return JoinPathSafe(dir, file_name);
}

static std::wstring BuildLegacySuccessFilePath(const std::wstring& dll_path, DWORD pid) {
    std::wstring dir = GetFileDirectory(dll_path);
    if (dir.empty()) {
        return L"";
    }
    std::wstring base_name = GetBaseNameWithoutExtension(dll_path);
    if (base_name.empty()) {
        return L"";
    }
    wchar_t file_name[128] = {0};
    if (swprintf_s(file_name, L"successfile_%s_%lu.txt", base_name.c_str(), pid) <= 0) {
        return L"";
    }
    return JoinPathSafe(dir, file_name);
}

static bool GetFileWriteTime(const std::wstring& path, FILETIME* output) {
    if (!output || path.empty()) {
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    FILETIME created = {}, accessed = {}, written = {};
    BOOL ok = GetFileTime(file, &created, &accessed, &written);
    CloseHandle(file);
    if (!ok) {
        return false;
    }
    *output = written;
    return true;
}

static bool HasFileUpdated(const std::wstring& path, bool baseline_valid, const FILETIME& baseline) {
    FILETIME current = {};
    if (!GetFileWriteTime(path, &current)) {
        return false;
    }
    if (!baseline_valid) {
        return true;
    }
    return CompareFileTime(&current, &baseline) == 1;
}

static bool WaitForSuccessFilePoll(const std::wstring& path, DWORD timeout_ms, DWORD interval_ms, bool baseline_valid, const FILETIME& baseline) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start <= timeout_ms) {
        if (HasFileUpdated(path, baseline_valid, baseline)) {
            return true;
        }
        Sleep(interval_ms);
    }
    return false;
}

static bool WaitForSuccessFileNotify(const std::wstring& path, DWORD timeout_ms, DWORD interval_ms, bool baseline_valid, const FILETIME& baseline) {
    std::wstring directory = GetFileDirectory(path);
    if (directory.empty()) {
        return WaitForSuccessFilePoll(path, timeout_ms, interval_ms, baseline_valid, baseline);
    }

    HANDLE change = FindFirstChangeNotificationW(
        directory.c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (change == INVALID_HANDLE_VALUE) {
        return WaitForSuccessFilePoll(path, timeout_ms, interval_ms, baseline_valid, baseline);
    }

    ULONGLONG start = GetTickCount64();
    bool updated = false;
    while (GetTickCount64() - start <= timeout_ms) {
        if (HasFileUpdated(path, baseline_valid, baseline)) {
            updated = true;
            break;
        }

        DWORD remaining = timeout_ms - static_cast<DWORD>(GetTickCount64() - start);
        DWORD wait = WaitForSingleObject(change, remaining);
        if (wait == WAIT_OBJECT_0) {
            if (HasFileUpdated(path, baseline_valid, baseline)) {
                updated = true;
                break;
            }
            if (!FindNextChangeNotification(change)) {
                break;
            }
            continue;
        }
        if (wait == WAIT_TIMEOUT) {
            break;
        }
        break;
    }

    FindCloseChangeNotification(change);
    if (updated) {
        return true;
    }
    return WaitForSuccessFilePoll(path, interval_ms, interval_ms, baseline_valid, baseline);
}

static InjectorSuccessObservationResultInterop ObserveSuccessFileChange(const std::wstring& path, const InjectorConfig& config, bool baseline_valid, const FILETIME& baseline) {
    InjectorSuccessObservationResultInterop result = {};
    if (config.success_observer_mode == 2) {
        result.used_fallback = 1;
        result.observed = WaitForSuccessFilePoll(path, config.success_timeout_ms, config.success_interval_ms, baseline_valid, baseline) ? 1u : 0u;
        result.timed_out = result.observed == 0 ? 1u : 0u;
        result.error_code = result.observed != 0 ? 0u : WAIT_TIMEOUT;
        return result;
    }
    result.observed = WaitForSuccessFileNotify(path, config.success_timeout_ms, config.success_interval_ms, baseline_valid, baseline) ? 1u : 0u;
    result.timed_out = result.observed == 0 ? 1u : 0u;
    result.error_code = result.observed != 0 ? 0u : WAIT_TIMEOUT;
    return result;
}

static bool TryReadHelperStatus(const std::wstring& mapping_name, HelperStatusV5* output) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
    if (!mapping) {
        return false;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(HelperStatusV5));
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    memcpy(output, view, sizeof(HelperStatusV5));
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return true;
}

static InjectorHeartbeatObservationResultInterop ObserveHelperHeartbeat(DWORD pid, DWORD timeout_ms) {
    InjectorHeartbeatObservationResultInterop result = {};
    const wchar_t* prefixes[] = {L"Local\\GameHelperStatus_", L"Global\\GameHelperStatus_"};
    for (int i = 0; i < 2; ++i) {
        wchar_t mapping_name[64] = {0};
        swprintf_s(mapping_name, L"%s%lu", prefixes[i], pid);
        HelperStatusV5 status = {};
        if (!TryReadHelperStatus(mapping_name, &status)) {
            continue;
        }
        result.mapping_found = 1;
        InjectorHelperHeartbeatDecisionInterop decision = injector_core_evaluate_helper_heartbeat(
            status.Version,
            status.Size,
            status.ProcessAlive != 0 ? 1u : 0u,
            status.LastTickMs,
            GetTickCount64(),
            timeout_ms);
        result.contract_ok = decision.contract_ok;
        result.observed = decision.heartbeat_ok;
        result.timed_out = decision.heartbeat_ok == 0 ? 1u : 0u;
        result.error_code = decision.heartbeat_ok != 0 ? 0u : WAIT_TIMEOUT;
        return result;
    }
    result.error_code = ERROR_FILE_NOT_FOUND;
    return result;
}

static bool PerformApcInjection(DWORD pid, const std::wstring& dll_path) {
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        return false;
    }

    size_t bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        CloseHandle(process);
        return false;
    }

    if (!WriteProcessMemory(process, remote_path, dll_path.c_str(), bytes, nullptr)) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC load_library = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
    if (!load_library) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    int queued = 0;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                HANDLE thread = OpenThread(THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
                if (thread) {
                    if (QueueUserAPC(reinterpret_cast<PAPCFUNC>(load_library), thread,
                        reinterpret_cast<ULONG_PTR>(remote_path)) != 0) {
                        ++queued;
                    }
                    CloseHandle(thread);
                }
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    CloseHandle(process);
    return queued > 0;
}

static InjectorBackendExecutionResultInterop PerformInjectionWithBackend(DWORD pid, const InjectorConfig& config) {
    InjectorBackendExecutionResultInterop result = {};
    result.configured_backend = config.inject_backend;
    switch (config.inject_backend) {
    case 2:
        result.effective_backend = 1;
        result.downgraded = 1;
        result.started = PerformApcInjection(pid, config.dll_path) ? 1u : 0u;
        result.error_code = result.started != 0 ? 0u : ERROR_GEN_FAILURE;
        return result;
    case 1:
    default:
        result.effective_backend = 1;
        result.started = PerformApcInjection(pid, config.dll_path) ? 1u : 0u;
        result.error_code = result.started != 0 ? 0u : ERROR_GEN_FAILURE;
        return result;
    }
}

static std::wstring DescribeInjectionBackend(const InjectorConfig& config) {
    switch (config.inject_backend) {
    case 2:
        return L"fallback";
    case 1:
    default:
        return L"apc";
    }
}

static std::wstring DescribeEffectiveInjectionBackend(const InjectorBackendExecutionResultInterop& result) {
    switch (result.effective_backend) {
    case 2:
        return L"fallback";
    case 1:
    default:
        return L"apc";
    }
}

static bool TryInjectProcess(DWORD pid, const InjectorConfig& config) {
    std::wstring success_path = BuildSuccessFilePath(config.dll_path, config.output_dir, pid);
    InjectionRetryRuntime* retry_runtime = injector_core_retry_runtime_create(
        static_cast<uint32_t>(config.max_retries),
        config.retry_interval_ms);
    if (!retry_runtime) {
        return false;
    }

    bool injected = false;
    while (injector_core_retry_runtime_can_attempt(retry_runtime) != 0) {
        uint32_t attempt = injector_core_retry_runtime_current_attempt(retry_runtime);
        FILETIME baseline = {};
        bool baseline_valid = false;
        if (!success_path.empty()) {
            baseline_valid = GetFileWriteTime(success_path, &baseline);
        }

        InjectorBackendExecutionResultInterop backend = PerformInjectionWithBackend(pid, config);
        Log(L"执行 " + DescribeInjectionBackend(config) + L" 注入 (PID " + std::to_wstring(pid) + L", 尝试 " + std::to_wstring(attempt) + L"/" + std::to_wstring(config.max_retries) + L", effective=" + DescribeEffectiveInjectionBackend(backend) + L")");
        if (backend.downgraded != 0) {
            Log(L"注入后端已自动降级为 apc");
        }
        if (backend.started == 0) {
            Log(L"注入后端执行失败");
        }

        InjectorSuccessObservationResultInterop success = {};
        InjectorHeartbeatObservationResultInterop heartbeat = {};
        if (backend.started != 0) {
            if (!success_path.empty()) {
                Log(L"等待成功文件: " + success_path);
                success = ObserveSuccessFileChange(success_path, config, baseline_valid, baseline);
            }

            if (success.observed == 0) {
                Log(L"成功文件未确认，尝试共享内存心跳兜底");
                ULONGLONG start = GetTickCount64();
                while (GetTickCount64() - start <= config.heartbeat_timeout_ms) {
                    heartbeat = ObserveHelperHeartbeat(pid, config.heartbeat_timeout_ms);
                    if (heartbeat.observed != 0) {
                        break;
                    }
                    Sleep(config.heartbeat_interval_ms);
                }
                if (heartbeat.observed == 0) {
                    heartbeat.timed_out = 1;
                    if (heartbeat.error_code == 0) {
                        heartbeat.error_code = WAIT_TIMEOUT;
                    }
                }
            }
        }

        InjectorRetryDecisionInterop decision = {};
        injector_core_retry_runtime_finish_attempt_with_results(
            retry_runtime,
            &backend,
            &success,
            &heartbeat,
            &decision);
        if (decision.succeeded != 0) {
            if (decision.success_source == 1) {
                Log(L"成功文件已更新，注入成功");
            } else if (decision.success_source == 2) {
                Log(L"共享内存心跳正常，注入成功");
            }
        } else if (decision.backend_started != 0 && decision.used_heartbeat_fallback != 0 && heartbeat.observed == 0) {
            Log(L"successfile 与 heartbeat 均未确认成功");
        }
        if (decision.succeeded != 0) {
            injected = true;
            break;
        }
        if (decision.should_retry != 0 && decision.retry_delay_ms > 0) {
            Sleep(decision.retry_delay_ms);
        }
    }

    injector_core_retry_runtime_destroy(retry_runtime);
    return injected;
}

static void DeleteSuccessFileForPid(DWORD pid, const InjectorConfig& config) {
    std::wstring path = BuildSuccessFilePath(config.dll_path, config.output_dir, pid);
    if (path.empty()) {
        return;
    }
    if (DeleteFileW(path.c_str())) {
        Log(L"已清理成功文件: " + path);
    }

    // 兼容旧路径：successfile 位于 DLL 根目录时也要清理。
    std::wstring legacy_path = BuildLegacySuccessFilePath(config.dll_path, pid);
    if (!legacy_path.empty() && legacy_path != path) {
        if (DeleteFileW(legacy_path.c_str())) {
            Log(L"已清理 legacy 成功文件: " + legacy_path);
            return;
        }
    }

    DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND) {
        Log(L"清理成功文件失败: " + path + L" (error=" + std::to_wstring(error) + L")");
    }
}

static InjectorConfig LoadInjectorConfig(
    const std::wstring& config_path,
    const std::wstring& exe_dir,
    InjectorConfigInterop* out_interop) {
    InjectorConfig config = BuildDefaultInjectorConfig();
    std::vector<unsigned char> file_bytes;
    if (ReadFileBytes(config_path, &file_bytes) && !file_bytes.empty()) {
        InjectorConfigInterop interop = {};
        std::string exe_dir_utf8 = WideToUtf8(exe_dir);
        if (!exe_dir_utf8.empty() &&
            injector_core_load_config_utf8(
                file_bytes.data(),
                file_bytes.size(),
                reinterpret_cast<const uint8_t*>(exe_dir_utf8.data()),
                exe_dir_utf8.size(),
                &interop) != 0) {
            config.process_name = Utf8ToWide(ReadCStringFromBuffer(interop.process_name, sizeof(interop.process_name)));
            config.dll_path = Utf8ToWide(ReadCStringFromBuffer(interop.dll_path, sizeof(interop.dll_path)));
            config.output_dir = Utf8ToWide(ReadCStringFromBuffer(interop.output_dir, sizeof(interop.output_dir)));
            config.scan_interval_ms = interop.view.scan_interval_ms;
            config.inject_delay_ms = interop.view.inject_delay_ms;
            config.window_wait_timeout_ms = interop.view.window_wait_timeout_ms;
            config.window_poll_interval_ms = interop.view.window_poll_interval_ms;
            config.post_window_delay_ms = interop.view.post_window_delay_ms;
            config.max_retries = static_cast<int>(interop.view.max_retries);
            config.retry_interval_ms = interop.view.retry_interval_ms;
            config.success_timeout_ms = interop.view.success_timeout_ms;
            config.success_interval_ms = interop.view.success_interval_ms;
            config.heartbeat_timeout_ms = interop.view.heartbeat_timeout_ms;
            config.heartbeat_interval_ms = interop.view.heartbeat_interval_ms;
            config.inject_backend = interop.view.inject_backend;
            config.success_observer_mode = interop.view.success_observer_mode;
            config.watch_mode = interop.view.watch_mode != 0;
            config.idle_exit_seconds = interop.view.idle_exit_seconds;
            config.max_concurrent_tasks = interop.view.max_concurrent_tasks;
            if (out_interop) {
                *out_interop = interop;
            }
            return config;
        }
    }

    // Rust 解析失败时，保留当前 C++ 回退路径，避免配置读取中断。
    config.process_name = ReadIniStringValue(config_path, L"process_name", config.process_name.c_str());
    std::wstring dll_path = ReadIniStringValue(config_path, L"dll_path", config.dll_path.c_str());
    config.dll_path = NormalizePath(dll_path, exe_dir);

    std::wstring output_dir = ReadIniStringValue(config_path, L"output_dir", config.output_dir.c_str());
    if (!output_dir.empty()) {
        config.output_dir = NormalizePath(output_dir, exe_dir);
    }

    config.scan_interval_ms = ReadIniUInt32(config_path, L"scan_interval_ms", config.scan_interval_ms);
    config.inject_delay_ms = ReadIniUInt32(config_path, L"inject_delay_ms", config.inject_delay_ms);
    config.window_wait_timeout_ms = ReadIniUInt32(config_path, L"window_wait_timeout_ms", config.window_wait_timeout_ms);
    config.window_poll_interval_ms = ReadIniUInt32(config_path, L"window_poll_interval_ms", config.window_poll_interval_ms);
    config.post_window_delay_ms = ReadIniUInt32(config_path, L"post_window_delay_ms", config.post_window_delay_ms);
    config.max_retries = static_cast<int>(ReadIniUInt32(config_path, L"max_retries", config.max_retries));
    config.retry_interval_ms = ReadIniUInt32(config_path, L"retry_interval_ms", config.retry_interval_ms);
    config.success_timeout_ms = ReadIniUInt32(config_path, L"success_timeout_ms", config.success_timeout_ms);
    config.success_interval_ms = ReadIniUInt32(config_path, L"success_interval_ms", config.success_interval_ms);
    config.heartbeat_timeout_ms = ReadIniUInt32(config_path, L"heartbeat_timeout_ms", config.heartbeat_timeout_ms);
    config.heartbeat_interval_ms = ReadIniUInt32(config_path, L"heartbeat_interval_ms", config.heartbeat_interval_ms);
    std::wstring inject_backend = ReadIniStringValue(config_path, L"inject_backend", L"");
    if (!inject_backend.empty() && _wcsicmp(inject_backend.c_str(), L"fallback") == 0) {
        config.inject_backend = 2;
    }
    std::wstring success_observer_mode = ReadIniStringValue(config_path, L"success_observer_mode", L"");
    if (!success_observer_mode.empty() && _wcsicmp(success_observer_mode.c_str(), L"poll") == 0) {
        config.success_observer_mode = 2;
    }
    config.watch_mode = ReadIniBool(config_path, L"watch_mode", config.watch_mode);
    config.idle_exit_seconds = ReadIniUInt32(config_path, L"idle_exit_seconds", config.idle_exit_seconds);
    config.max_concurrent_tasks = ReadIniUInt32(config_path, L"max_concurrent_tasks", config.max_concurrent_tasks);
    if (out_interop) {
        BuildInjectorConfigInterop(config, out_interop);
    }
    return config;
}

struct InjectTaskParams {
    DWORD pid = 0;
    InjectorConfig config;
};

static DWORD WINAPI InjectTaskThread(LPVOID param) {
    std::unique_ptr<InjectTaskParams> payload(reinterpret_cast<InjectTaskParams*>(param));
    DWORD pid = payload->pid;
    InjectorConfig config = payload->config;

    Log(L"等待目标进程窗口初始化... (PID " + std::to_wstring(pid) + L")");
    InjectorWindowProbeResultInterop window_probe = ProbeProcessWindowReady(
        pid,
        config.window_wait_timeout_ms,
        config.window_poll_interval_ms);
    if (window_probe.ok == 0) {
        Log(L"超时：目标进程未创建窗口，跳过注入 (PID " + std::to_wstring(pid) + L")");
        return 0;
    }

    if (config.post_window_delay_ms > 0) {
        Log(L"窗口已就绪，等待 " + std::to_wstring(config.post_window_delay_ms) + L"ms 后注入 (PID " + std::to_wstring(pid) + L")");
        Sleep(config.post_window_delay_ms);
    }

    if (config.inject_delay_ms > 0) {
        Sleep(config.inject_delay_ms);
    }

    bool injected = TryInjectProcess(pid, config);
    if (injected) {
        Log(L"注入完成: PID " + std::to_wstring(pid));
    } else {
        Log(L"注入失败: PID " + std::to_wstring(pid));
    }
    return injected ? 1u : 0u;
}

static void CleanupFinishedTasks(
    std::unordered_map<DWORD, HANDLE>& threads,
    InjectorWatchRuntime* runtime) {
    for (auto it = threads.begin(); it != threads.end(); ) {
        if (!it->second) {
            it = threads.erase(it);
            continue;
        }
        DWORD wait = WaitForSingleObject(it->second, 0);
        if (wait == WAIT_OBJECT_0) {
            DWORD exit_code = 0;
            GetExitCodeThread(it->second, &exit_code);
            injector_core_watch_runtime_mark_finished(runtime, it->first, exit_code == 1 ? 1u : 0u);
            CloseHandle(it->second);
            it = threads.erase(it);
            continue;
        }
        ++it;
    }
}

static void CollectRuntimeRemovals(
    InjectorWatchRuntime* runtime,
    const InjectorConfig& config) {
    size_t task_count = injector_core_watch_runtime_task_count(runtime);
    if (task_count == 0) {
        return;
    }
    std::vector<uint32_t> removable(task_count);
    size_t count = injector_core_watch_runtime_collect_removals(
        runtime,
        removable.data(),
        removable.size());
    for (size_t i = 0; i < count; ++i) {
        DWORD pid = removable[i];
        Log(L"进程退出: PID " + std::to_wstring(pid));
        DeleteSuccessFileForPid(pid, config);
    }
}

static void TryStartPendingTasks(
    std::unordered_map<DWORD, HANDLE>& threads,
    InjectorWatchRuntime* runtime,
    const InjectorConfig& config,
    size_t start_budget) {
    if (start_budget == 0) {
        return;
    }
    std::vector<uint32_t> pending(start_budget);
    size_t count = injector_core_watch_runtime_collect_pending(
        runtime,
        start_budget,
        pending.data(),
        pending.size());
    for (size_t i = 0; i < count; ++i) {
        DWORD pid = pending[i];
        std::unique_ptr<InjectTaskParams> payload(new InjectTaskParams());
        payload->pid = pid;
        payload->config = config;
        HANDLE thread = CreateThread(nullptr, 0, InjectTaskThread, payload.release(), 0, nullptr);
        if (!thread) {
            Log(L"创建注入线程失败: PID " + std::to_wstring(pid));
            continue;
        }
        threads[pid] = thread;
        injector_core_watch_runtime_mark_started(runtime, pid);
        Log(L"启动注入任务: PID " + std::to_wstring(pid));
    }
}

int wmain() {
    int exit_code = 0;
    std::wstring exe_dir = GetExeDirectory();
    std::wstring config_path = GetInjectorConfigPath(exe_dir);
    InjectorConfig config;
    InjectorConfigInterop config_interop = {};
    InjectorWatchRuntime* watch_runtime = nullptr;
    std::unordered_map<DWORD, HANDLE> threads;

    std::wstring contract_error;
    if (!ValidateRustContracts(&contract_error)) {
        Log(L"Rust 合约校验失败: " + contract_error);
        exit_code = 4;
        goto Exit;
    }

    EnsureDefaultInjectorConfig(config_path);
    config = LoadInjectorConfig(config_path, exe_dir, &config_interop);

    Log(L"Injector 启动");
    Log(L"配置文件: " + config_path);
    Log(L"Rust HelperStatus 合约: Version="
        + std::to_wstring(injector_core_helper_status_version())
        + L", Size=" + std::to_wstring(injector_core_helper_status_size()));
    Log(L"进程名: " + config.process_name);
    Log(L"DLL 路径: " + config.dll_path);
    Log(L"注入后端配置: " + DescribeInjectionBackend(config));
    Log(L"successfile 观察方式: " + std::wstring(config.success_observer_mode == 2 ? L"poll" : L"notify"));
    Log(L"窗口等待超时: " + std::to_wstring(config.window_wait_timeout_ms) + L"ms");
    Log(L"窗口检测间隔: " + std::to_wstring(config.window_poll_interval_ms) + L"ms");
    Log(L"窗口后等待: " + std::to_wstring(config.post_window_delay_ms) + L"ms");

    DWORD attr = GetFileAttributesW(config.dll_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        Log(L"DLL 路径无效，请检查 injector.ini");
        exit_code = 1;
        goto Exit;
    }

    if (!EnableDebugPrivilege()) {
        Log(L"警告: 无法启用 SeDebugPrivilege");
    }

    if (!config.watch_mode) {
        DWORD pid = 0;
        Log(L"等待目标进程...");
        while ((pid = FindProcessId(config.process_name)) == 0) {
            Sleep(config.scan_interval_ms);
        }
        Log(L"发现 PID: " + std::to_wstring(pid));
        Log(L"等待目标进程窗口初始化...");
        InjectorWindowProbeResultInterop window_probe = ProbeProcessWindowReady(
            pid,
            config.window_wait_timeout_ms,
            config.window_poll_interval_ms);
        if (window_probe.ok == 0) {
            Log(L"超时：目标进程未创建窗口，终止注入");
            exit_code = 3;
            goto Exit;
        }
        if (config.post_window_delay_ms > 0) {
            Sleep(config.post_window_delay_ms);
        }
        if (config.inject_delay_ms > 0) {
            Sleep(config.inject_delay_ms);
        }
        bool injected = TryInjectProcess(pid, config);
        if (!injected) {
            Log(L"注入失败，请检查日志与配置");
            exit_code = 2;
            goto Exit;
        }
        Log(L"注入完成");
        exit_code = 0;
        goto Exit;
    }

    Log(L"进入常驻监听模式");
    watch_runtime = injector_core_watch_runtime_create(&config_interop, GetTickCount64());
    if (!watch_runtime) {
        Log(L"Rust watch runtime 创建失败");
        exit_code = 5;
        goto Exit;
    }

    for (;;) {
        ULONGLONG now = GetTickCount64();
        CleanupFinishedTasks(threads, watch_runtime);
        std::vector<DWORD> pids = ListProcessIds(config.process_name);
        injector_core_watch_runtime_observe_processes(
            watch_runtime,
            now,
            reinterpret_cast<const uint32_t*>(pids.data()),
            pids.size());
        CollectRuntimeRemovals(watch_runtime, config);

        size_t active = threads.size();
        size_t limit = config.max_concurrent_tasks == 0
            ? pids.size()
            : static_cast<size_t>(config.max_concurrent_tasks);
        size_t start_budget = limit > active ? (limit - active) : 0;
        TryStartPendingTasks(threads, watch_runtime, config, start_budget);

        if (injector_core_watch_runtime_should_exit_idle(watch_runtime, now) != 0) {
            Log(L"超过 idle_exit_seconds 无新进程出现，退出注入器");
            break;
        }
        Sleep(config.scan_interval_ms);
    }

Exit:
    if (watch_runtime) {
        injector_core_watch_runtime_destroy(watch_runtime);
        watch_runtime = nullptr;
    }
    for (auto& pair : threads) {
        if (pair.second) {
            CloseHandle(pair.second);
        }
    }
#ifdef _DEBUG
    ArchiveDebugLogIfNeeded();
#endif
    return exit_code;
}
