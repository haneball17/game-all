#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <cwctype>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#ifdef _DEBUG
#include <algorithm>
#endif

struct InjectorConfig {
    std::wstring process_name;
    std::wstring dll_path;
    std::wstring output_dir;
    DWORD scan_interval_ms = 1000;
    DWORD inject_delay_ms = 2000;
    int max_retries = 3;
    DWORD retry_interval_ms = 1000;
    DWORD success_timeout_ms = 6000;
    DWORD success_interval_ms = 200;
    DWORD heartbeat_timeout_ms = 6000;
    DWORD heartbeat_interval_ms = 200;
    bool watch_mode = true;
    DWORD idle_exit_seconds = 600;
};

#pragma pack(push, 1)
struct HelperStatusV2 {
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
    int32_t SummonEnabled;
    uint64_t SummonLastTick;
    int32_t FullscreenSkillEnabled;
    int32_t FullscreenSkillActive;
    uint32_t FullscreenSkillHotkey;
    int32_t HotkeyEnabled;
    wchar_t PlayerName[32];
};
#pragma pack(pop)

static_assert(sizeof(HelperStatusV2) == 136, "HelperStatusV2 size mismatch");

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
    std::string content =
        "[injector]\r\n"
        "; 目标进程名（不区分大小写，自动补 .exe）\r\n"
        "process_name=DNF.exe\r\n"
        "; DLL 路径（默认相对注入器输出目录）\r\n"
        "dll_path=game-payload.dll\r\n"
        "; 成功文件目录（为空则使用 DLL\\\\logs 目录）\r\n"
        "; output_dir=\r\n"
        "; 扫描进程间隔（毫秒）\r\n"
        "scan_interval_ms=1000\r\n"
        "; 发现进程后等待注入的延迟（毫秒）\r\n"
        "inject_delay_ms=2000\r\n"
        "; 重试次数与间隔\r\n"
        "max_retries=3\r\n"
        "retry_interval_ms=1000\r\n"
        "; 成功文件检测\r\n"
        "success_timeout_ms=6000\r\n"
        "success_interval_ms=200\r\n"
        "; 共享内存心跳兜底\r\n"
        "heartbeat_timeout_ms=6000\r\n"
        "heartbeat_interval_ms=200\r\n"
        "; 常驻监听模式\r\n"
        "watch_mode=true\r\n"
        "; 无新目标进程出现后自动退出（秒，0 表示不退出）\r\n"
        "idle_exit_seconds=600\r\n";
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
static std::wstring GetDebugLogPath() {
    std::wstring base_dir = GetExeDirectory();
    std::wstring log_dir = JoinPathSafe(base_dir, L"logs");
    CreateDirectoryW(log_dir.c_str(), nullptr);
    return JoinPathSafe(log_dir, L"injector_debug.log");
}

static void AppendDebugLog(const std::wstring& message) {
    static std::wstring log_path = GetDebugLogPath();
    HANDLE file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
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
#endif

static void Log(const std::wstring& message) {
    std::wcout << L"[Injector] " << message << std::endl;
#ifdef _DEBUG
    AppendDebugLog(message);
#endif
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

static bool WaitForSuccessFile(const std::wstring& path, DWORD timeout_ms, DWORD interval_ms, bool baseline_valid, const FILETIME& baseline) {
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start <= timeout_ms) {
        if (HasFileUpdated(path, baseline_valid, baseline)) {
            return true;
        }
        Sleep(interval_ms);
    }
    return false;
}

static bool TryReadHelperStatus(const std::wstring& mapping_name, HelperStatusV2* output) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str());
    if (!mapping) {
        return false;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(HelperStatusV2));
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    memcpy(output, view, sizeof(HelperStatusV2));
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return true;
}

static bool HasHelperHeartbeat(DWORD pid, DWORD timeout_ms) {
    const wchar_t* prefixes[] = {L"Local\\GameHelperStatus_", L"Global\\GameHelperStatus_"};
    for (int i = 0; i < 2; ++i) {
        wchar_t mapping_name[64] = {0};
        swprintf_s(mapping_name, L"%s%lu", prefixes[i], pid);
        HelperStatusV2 status = {};
        if (!TryReadHelperStatus(mapping_name, &status)) {
            continue;
        }
        if (status.Version != 3 || status.Size != sizeof(HelperStatusV2)) {
            continue;
        }
        ULONGLONG now = GetTickCount64();
        ULONGLONG delta = now >= status.LastTickMs ? now - status.LastTickMs : 0;
        if (status.ProcessAlive != 0 && delta <= timeout_ms) {
            return true;
        }
    }
    return false;
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

struct ProcessState {
    ULONGLONG last_seen = 0;
    bool injected = false;
};

static bool TryInjectProcess(DWORD pid, const InjectorConfig& config) {
    std::wstring success_path = BuildSuccessFilePath(config.dll_path, config.output_dir, pid);
    for (int attempt = 1; attempt <= config.max_retries; ++attempt) {
        FILETIME baseline = {};
        bool baseline_valid = false;
        if (!success_path.empty()) {
            baseline_valid = GetFileWriteTime(success_path, &baseline);
        }

        Log(L"执行 APC 注入 (PID " + std::to_wstring(pid) + L", 尝试 " + std::to_wstring(attempt) + L"/" + std::to_wstring(config.max_retries) + L")");
        if (!PerformApcInjection(pid, config.dll_path)) {
            Log(L"APC 注入排队失败");
        } else {
            bool injected = false;
            if (!success_path.empty()) {
                Log(L"等待成功文件: " + success_path);
                if (WaitForSuccessFile(success_path, config.success_timeout_ms, config.success_interval_ms, baseline_valid, baseline)) {
                    injected = true;
                    Log(L"成功文件已更新，注入成功");
                }
            }
            if (!injected) {
                Log(L"成功文件未确认，尝试共享内存心跳兜底");
                ULONGLONG start = GetTickCount64();
                while (GetTickCount64() - start <= config.heartbeat_timeout_ms) {
                    if (HasHelperHeartbeat(pid, config.heartbeat_timeout_ms)) {
                        injected = true;
                        Log(L"共享内存心跳正常，注入成功");
                        break;
                    }
                    Sleep(config.heartbeat_interval_ms);
                }
            }
            if (injected) {
                return true;
            }
        }

        if (attempt < config.max_retries) {
            Sleep(config.retry_interval_ms);
        }
    }
    return false;
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

static InjectorConfig LoadInjectorConfig(const std::wstring& config_path, const std::wstring& exe_dir) {
    InjectorConfig config;
    config.process_name = ReadIniStringValue(config_path, L"process_name", L"DNF.exe");
    std::wstring dll_path = ReadIniStringValue(config_path, L"dll_path", L"..\\payload\\game-payload.dll");
    config.dll_path = NormalizePath(dll_path, exe_dir);

    std::wstring output_dir = ReadIniStringValue(config_path, L"output_dir", L"");
    if (!output_dir.empty()) {
        config.output_dir = NormalizePath(output_dir, exe_dir);
    }

    config.scan_interval_ms = ReadIniUInt32(config_path, L"scan_interval_ms", config.scan_interval_ms);
    config.inject_delay_ms = ReadIniUInt32(config_path, L"inject_delay_ms", config.inject_delay_ms);
    config.max_retries = static_cast<int>(ReadIniUInt32(config_path, L"max_retries", config.max_retries));
    config.retry_interval_ms = ReadIniUInt32(config_path, L"retry_interval_ms", config.retry_interval_ms);
    config.success_timeout_ms = ReadIniUInt32(config_path, L"success_timeout_ms", config.success_timeout_ms);
    config.success_interval_ms = ReadIniUInt32(config_path, L"success_interval_ms", config.success_interval_ms);
    config.heartbeat_timeout_ms = ReadIniUInt32(config_path, L"heartbeat_timeout_ms", config.heartbeat_timeout_ms);
    config.heartbeat_interval_ms = ReadIniUInt32(config_path, L"heartbeat_interval_ms", config.heartbeat_interval_ms);
    config.watch_mode = ReadIniBool(config_path, L"watch_mode", config.watch_mode);
    config.idle_exit_seconds = ReadIniUInt32(config_path, L"idle_exit_seconds", config.idle_exit_seconds);
    return config;
}

int wmain() {
    std::wstring exe_dir = GetExeDirectory();
    std::wstring config_path = GetInjectorConfigPath(exe_dir);
    EnsureDefaultInjectorConfig(config_path);
    InjectorConfig config = LoadInjectorConfig(config_path, exe_dir);

    Log(L"Injector 启动");
    Log(L"配置文件: " + config_path);
    Log(L"进程名: " + config.process_name);
    Log(L"DLL 路径: " + config.dll_path);

    DWORD attr = GetFileAttributesW(config.dll_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        Log(L"DLL 路径无效，请检查 injector.ini");
        return 1;
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
        if (config.inject_delay_ms > 0) {
            Sleep(config.inject_delay_ms);
        }
        bool injected = TryInjectProcess(pid, config);
        if (!injected) {
            Log(L"注入失败，请检查日志与配置");
            return 2;
        }
        Log(L"注入完成");
        return 0;
    }

    Log(L"进入常驻监听模式");
    std::unordered_map<DWORD, ProcessState> states;
    ULONGLONG last_new_tick = GetTickCount64();

    for (;;) {
        ULONGLONG now = GetTickCount64();
        std::vector<DWORD> pids = ListProcessIds(config.process_name);
        std::unordered_set<DWORD> current(pids.begin(), pids.end());

        // 清理已退出进程
        for (auto it = states.begin(); it != states.end(); ) {
            if (current.find(it->first) == current.end()) {
                Log(L"进程退出: PID " + std::to_wstring(it->first));
                DeleteSuccessFileForPid(it->first, config);
                it = states.erase(it);
                continue;
            }
            it->second.last_seen = now;
            ++it;
        }

        // 处理新进程
        for (DWORD pid : pids) {
            if (states.find(pid) != states.end()) {
                continue;
            }
            ProcessState state;
            state.last_seen = now;
            states.emplace(pid, state);
            last_new_tick = now;

            Log(L"发现新进程: PID " + std::to_wstring(pid));
            if (config.inject_delay_ms > 0) {
                Sleep(config.inject_delay_ms);
            }
            bool injected = TryInjectProcess(pid, config);
            states[pid].injected = injected;
            if (injected) {
                Log(L"注入完成: PID " + std::to_wstring(pid));
            } else {
                Log(L"注入失败: PID " + std::to_wstring(pid));
            }
        }

        if (config.idle_exit_seconds > 0) {
            ULONGLONG idle_ms = static_cast<ULONGLONG>(config.idle_exit_seconds) * 1000ULL;
            if (GetTickCount64() - last_new_tick >= idle_ms) {
                Log(L"超过 idle_exit_seconds 无新进程出现，退出注入器");
                break;
            }
        }
        Sleep(config.scan_interval_ms);
    }

    return 0;
}
