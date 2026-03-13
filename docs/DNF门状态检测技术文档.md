# DNF 门状态检测技术文档

> 用于检测当前房间怪物是否完全清除以及进入下一房间的完整技术方案

---

## 📌 目录

- [功能概述](#功能概述)
- [核心技术原理](#核心技术原理)
- [内存地址映射表](#内存地址映射表)
- [代码实现](#代码实现)
- [集成指南](#集成指南)
- [完整示例代码](#完整示例代码)
- [注意事项](#注意事项)

---

## 功能概述

本方案提供两个核心功能：

| 功能 | 描述 |
|------|------|
| **门状态检测** | 检测当前房间怪物是否已清除，门是否已打开 |
| **过图调用** | 通过游戏内部CALL直接进入下一房间 |

> **0725 版本约束（强制）**  
> 本文档中的基址/偏移仅用于说明字段语义，**禁止在代码中硬编码**。  
> 实际值必须通过外部配置（如 `config/offsets_0725.json`）或签名扫描结果在运行时注入。

---

## 核心技术原理

### 门状态检测原理

DNF游戏中，房间门的开关状态存储在特定内存地址中：

```
当房间内所有怪物被清除时，游戏内部会将门状态标记设为 0
此时玩家可以进入下一房间
```

**检测路径：**
```
人物基址(配置) → 地图偏移(配置) → 门状态偏移(配置) → 解密函数(配置)
```

### 过图原理

通过调用游戏内部函数（过图Call），传入方向参数触发房间切换：

```
房间编号 → 内部对象链 → 过图函数 + 方向参数
```

---

## 内存地址映射表

### 配置键位（替代硬编码）

| 配置键 | 示例值（0725需以实测为准） | 说明 |
|------|---------------------------|------|
| `player_base` | `0x00000000` | 角色对象基址 |
| `map_offset` | `0x00000000` | 地图对象偏移 |
| `door_state_offset` | `0x00000000` | 门状态字段偏移 |
| `room_info_base` | `0x00000000` | 房间信息基址 |
| `room_level1_offset` | `0x00000000` | 房间一级对象偏移 |
| `room_level2_offset` | `0x00000000` | 房间二级对象偏移 |
| `change_room_call` | `0x00000000` | 过图函数地址 |
| `decrypt_call` | `0x00000000` | 解密函数地址（或算法入口） |
| `room_time_base_offset` | `0x00000000` | 房间时间结构偏移 |
| `room_base_offset` | `0x00000000` | 房间结构根偏移 |
| `boss_x_offset` | `0x00000000` | BOSS 房间 X 偏移 |
| `boss_y_offset` | `0x00000000` | BOSS 房间 Y 偏移 |
| `cur_x_offset` | `0x00000000` | 当前房间 X 偏移 |
| `cur_y_offset` | `0x00000000` | 当前房间 Y 偏移 |

### 推荐配置文件示例

```json
{
  "version_tag": "0725",
  "offsets": {
    "player_base": "0x00000000",
    "map_offset": "0x00000000",
    "door_state_offset": "0x00000000",
    "room_info_base": "0x00000000",
    "room_level1_offset": "0x00000000",
    "room_level2_offset": "0x00000000",
    "change_room_call": "0x00000000",
    "decrypt_call": "0x00000000",
    "room_time_base_offset": "0x00000000",
    "room_base_offset": "0x00000000",
    "boss_x_offset": "0x00000000",
    "boss_y_offset": "0x00000000",
    "cur_x_offset": "0x00000000",
    "cur_y_offset": "0x00000000"
  }
}
```

---

## 代码实现

### 1. 基础工具函数

```cpp
#include <Windows.h>
#include <TlHelp32.h>

// 进程句柄（需要通过进程名或窗口获取）
HANDLE g_hProcess = NULL;

// 偏移配置（运行时加载，禁止硬编码）
struct DoorOffsets {
    DWORD player_base;
    DWORD map_offset;
    DWORD door_state_offset;
    DWORD room_info_base;
    DWORD room_level1_offset;
    DWORD room_level2_offset;
    DWORD change_room_call;
    DWORD decrypt_call;
};

DoorOffsets g_offsets = {};

// 读取内存
template<typename T>
T ReadMemory(DWORD address) {
    T value = T();
    ReadProcessMemory(g_hProcess, (LPCVOID)address, &value, sizeof(T), NULL);
    return value;
}

// 特化的指针读取
int ReadInt(DWORD address) {
    return ReadMemory<int>(address);
}

// 简单解密函数（根据实际游戏版本调整）
// 从配置文件加载偏移（示例）
bool LoadDoorOffsets(const wchar_t* configPath, DoorOffsets& outOffsets);

// 解密函数：通过配置提供的入口地址调用（示例签名，按实际调整）
int DecryptByGame(int encrypted) {
    if (g_offsets.decrypt_call == 0) {
        return encrypted;
    }
    using DecryptFn = int(__cdecl*)(int);
    DecryptFn fn = reinterpret_cast<DecryptFn>(g_offsets.decrypt_call);
    return fn(encrypted);
}
```

### 2. 门状态检测函数

```cpp
// 检测当前房间的门是否已打开（怪物是否已清除）
bool IsDoorOpen() {
    // 获取人物对象
    int playerBase = ReadInt(g_offsets.player_base);
    if (playerBase == 0) return false;

    // 获取地图对象
    int mapBase = ReadInt(playerBase + g_offsets.map_offset);
    if (mapBase == 0) return false;

    // 读取门状态（偏移来自配置）
    int doorStateEncrypted = ReadInt(mapBase + g_offsets.door_state_offset);
    int doorState = DecryptByGame(doorStateEncrypted);

    // 0表示门已打开
    return doorState == 0;
}
```

### 3. 过图调用函数

```cpp
// 方向枚举
enum class DoorDirection {
    LEFT = 0,   // 左
    RIGHT = 1,  // 右
    UP = 2,     // 上
    DOWN = 3    // 下
};

// 使用内联汇编调用过图函数
void ChangeRoom(DoorDirection direction) {
    if (g_offsets.room_info_base == 0 || g_offsets.change_room_call == 0) {
        return;
    }
    __asm {
        // 获取房间对象
        mov ecx, g_offsets.room_info_base
        mov ecx, [ecx]
        mov eax, g_offsets.room_level1_offset
        mov ecx, [ecx + eax]
        mov eax, g_offsets.room_level2_offset
        mov ecx, [ecx + eax]

        // 压入参数
        push 0xFF
        push 0xFF
        push 0x00
        push 0x00
        push 0x00
        push 0x00
        push 0x00
        push direction  // 方向参数

        // 调用过图函数
        mov eax, g_offsets.change_room_call
        call eax
    }
}
```

---

## 集成指南

### 步骤 1：初始化进程访问

```cpp
// 通过窗口标题获取DNF进程句柄
HANDLE GetDNFProcessHandle() {
    HWND hwnd = FindWindowW(L"DNF Client", NULL);
    if (hwnd == NULL) {
        return NULL;
    }

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
}

// 初始化
bool Initialize() {
    if (!LoadDoorOffsets(L"config\\offsets_0725.json", g_offsets)) {
        return false;
    }
    g_hProcess = GetDNFProcessHandle();
    return g_hProcess != NULL;
}
```

### 步骤 2：基本使用

```cpp
// 示例：自动过图循环
void AutoChangeRoom() {
    if (!Initialize()) {
        printf("无法获取DNF进程\n");
        return;
    }

    while (true) {
        if (IsDoorOpen()) {
            printf("门已打开，准备过图...\n");
            Sleep(500);
            ChangeRoom(DoorDirection::RIGHT);  // 向右过图
            Sleep(1000);
        }

        Sleep(100);
    }
}
```

### 步骤 3：获取进程句柄的其他方式

```cpp
// 通过进程名获取
HANDLE GetProcessByName(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &pe32)) {
        do {
            if (wcscmp(pe32.szExeFile, processName) == 0) {
                pid = pe32.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe32));
    }

    CloseHandle(snapshot);

    if (pid > 0) {
        return OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    }
    return NULL;
}
```

---

## 完整示例代码

```cpp
// DNFDoorDetector.h
#pragma once
#include <Windows.h>

class DNFDoorDetector {
public:
    // 偏移配置（由外部加载，禁止硬编码）
    struct Offsets {
        DWORD PlayerBase;
        DWORD MapOffset;
        DWORD DoorStateOffset;
        DWORD RoomInfoBase;
        DWORD RoomLevel1Offset;
        DWORD RoomLevel2Offset;
        DWORD ChangeRoomCall;
        DWORD DecryptCall;
        DWORD RoomTimeBaseOffset;
        DWORD RoomBaseOffset;
        DWORD BossXOffset;
        DWORD BossYOffset;
        DWORD CurXOffset;
        DWORD CurYOffset;
    };

    // 方向枚举
    enum Direction {
        DIR_LEFT = 0,
        DIR_RIGHT = 1,
        DIR_UP = 2,
        DIR_DOWN = 3
    };

    // 初始化（同时加载配置）
    bool Init(const wchar_t* configPath);

    // 检测门是否打开
    bool IsDoorOpen();

    // 过图
    void ChangeRoom(Direction dir);

    // 是否在副本中
    bool IsInDungeon();

private:
    HANDLE m_hProcess = NULL;
    Offsets m_offsets{};

    bool LoadOffsets(const wchar_t* configPath);
    int DecryptByGame(int value);

    template<typename T>
    T ReadMemory(DWORD addr);
};
```

```cpp
// DNFDoorDetector.cpp
#include "DNFDoorDetector.h"

bool DNFDoorDetector::Init(const wchar_t* configPath) {
    if (!LoadOffsets(configPath)) {
        return false;
    }
    // 获取DNF窗口
    HWND hwnd = FindWindowW(L"DNF Client", NULL);
    if (!hwnd) return false;

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    m_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    return m_hProcess != NULL;
}

bool DNFDoorDetector::IsDoorOpen() {
    if (!m_hProcess) return false;

    // 读取路径: 人物基址(配置) → 地图偏移(配置) → 门状态偏移(配置) → 解密
    int playerBase = ReadMemory<int>(m_offsets.PlayerBase);
    if (playerBase == 0) return false;

    int mapBase = ReadMemory<int>(playerBase + m_offsets.MapOffset);
    if (mapBase == 0) return false;

    int doorState = ReadMemory<int>(mapBase + m_offsets.DoorStateOffset);
    doorState = DecryptByGame(doorState);

    return doorState == 0;
}

void DNFDoorDetector::ChangeRoom(Direction dir) {
    if (m_offsets.RoomInfoBase == 0 || m_offsets.ChangeRoomCall == 0) {
        return;
    }

    __asm {
        mov ecx, m_offsets.RoomInfoBase
        mov ecx, [ecx]
        mov eax, m_offsets.RoomLevel1Offset
        mov ecx, [ecx + eax]
        mov eax, m_offsets.RoomLevel2Offset
        mov ecx, [ecx + eax]

        push 0xFF
        push 0xFF
        push 0x00
        push 0x00
        push 0x00
        push 0x00
        push 0x00
        push dir

        mov eax, m_offsets.ChangeRoomCall
        call eax
    }
}

bool DNFDoorDetector::IsInDungeon() {
    if (!m_hProcess) return false;

    int playerBase = ReadMemory<int>(m_offsets.PlayerBase);
    if (playerBase == 0) return false;

    int mapBase = ReadMemory<int>(playerBase + m_offsets.MapOffset);
    // 在副本中 mapBase != 0，在城镇中 mapBase == 0
    return mapBase != 0;
}

int DNFDoorDetector::DecryptByGame(int value) {
    if (m_offsets.DecryptCall == 0) {
        return value;
    }
    using DecryptFn = int(__cdecl*)(int);
    DecryptFn fn = reinterpret_cast<DecryptFn>(m_offsets.DecryptCall);
    return fn(value);
}

template<typename T>
T DNFDoorDetector::ReadMemory(DWORD addr) {
    T value = T();
    ReadProcessMemory(m_hProcess, (LPCVOID)addr, &value, sizeof(T), NULL);
    return value;
}
```

```cpp
// main.cpp - 使用示例
#include "DNFDoorDetector.h"
#include <iostream>

int main() {
    DNFDoorDetector detector;

    if (!detector.Init(L"config\\offsets_0725.json")) {
        std::cout << "无法连接到DNF进程，请确保游戏正在运行" << std::endl;
        return 1;
    }

    std::cout << "已连接到DNF，开始监控门状态..." << std::endl;
    std::cout << "按方向键手动过图，或按ESC退出" << std::endl;

    while (true) {
        if (GetAsyncKeyState(VK_ESCAPE)) {
            break;
        }

        // 检测门状态
        if (detector.IsInDungeon()) {
            bool doorOpen = detector.IsDoorOpen();

            if (doorOpen) {
                std::cout << "[门状态] 已开启 - 可以过图" << std::endl;

                // 快捷键过图
                if (GetAsyncKeyState(VK_LEFT)) {
                    detector.ChangeRoom(DNFDoorDetector::DIR_LEFT);
                    std::cout << "执行: 向左过图" << std::endl;
                    Sleep(500);
                }
                if (GetAsyncKeyState(VK_RIGHT)) {
                    detector.ChangeRoom(DNFDoorDetector::DIR_RIGHT);
                    std::cout << "执行: 向右过图" << std::endl;
                    Sleep(500);
                }
                if (GetAsyncKeyState(VK_UP)) {
                    detector.ChangeRoom(DNFDoorDetector::DIR_UP);
                    std::cout << "执行: 向上过图" << std::endl;
                    Sleep(500);
                }
                if (GetAsyncKeyState(VK_DOWN)) {
                    detector.ChangeRoom(DNFDoorDetector::DIR_DOWN);
                    std::cout << "执行: 向下过图" << std::endl;
                    Sleep(500);
                }
            } else {
                std::cout << "[门状态] 未开启 - 还有怪物" << std::endl;
            }
        } else {
            std::cout << "[状态] 在城镇中" << std::endl;
        }

        Sleep(500);
    }

    std::cout << "程序已退出" << std::endl;
    return 0;
}
```

---

## 注意事项

### ⚠️ 重要警告

1. **游戏版本差异**
   - 不同DNF版本的内存地址可能不同
   - 需要根据实际版本调整基址和偏移
   - 建议使用CE等工具自行扫描确认

2. **反作弊风险**
   - 此类代码属于游戏辅助工具
   - 可能被游戏反作弊系统检测
   - 请在合法范围内使用（如私人学习研究）

3. **权限要求**
   - 需要管理员权限运行
   - 需要关闭某些安全软件

4. **解密算法**
   - 示例中的解密是简化的
   - 实际游戏中加密可能更复杂
   - 需要根据版本调整解密逻辑

### 🔍 地址扫描方法

0725 版本请将扫描结果写入配置文件，不要改源码常量：

1. **打开游戏，进入副本**
2. **使用CE搜索特征值：**
   - 门关闭时：非0值
   - 门开启时：0
3. **找出基址和偏移链**
4. **更新 `config/offsets_0725.json` 对应键值**
5. **重启模块并进行版本标记校验**

### 📝 调试建议

```cpp
// 调试输出辅助函数
void DebugPrint(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // 输出到文件或调试器
    OutputDebugStringA(buffer);
}
```

---

## 扩展功能

### 检测当前是否在BOSS房间

```cpp
bool IsInBossRoom() {
    // 以下偏移均来自配置，不允许硬编码
    const DWORD offsetTimeBase = m_offsets.RoomTimeBaseOffset;
    const DWORD offsetBossX = m_offsets.BossXOffset;
    const DWORD offsetBossY = m_offsets.BossYOffset;
    const DWORD offsetCurX = m_offsets.CurXOffset;
    const DWORD offsetCurY = m_offsets.CurYOffset;
    const DWORD offsetRoomBase = m_offsets.RoomBaseOffset;

    int roomBase = ReadMemory<int>(m_offsets.RoomInfoBase);
    if (roomBase == 0) return false;

    int timeBase = ReadMemory<int>(roomBase + offsetTimeBase);
    if (timeBase == 0) return false;

    int roomStruct = ReadMemory<int>(timeBase + offsetRoomBase);
    if (roomStruct == 0) return false;

    int bossX = DecryptByGame(ReadMemory<int>(roomStruct + offsetBossX));
    int bossY = DecryptByGame(ReadMemory<int>(roomStruct + offsetBossY));
    int curX = ReadMemory<int>(roomStruct + offsetCurX);
    int curY = ReadMemory<int>(roomStruct + offsetCurY);

    return (curX == bossX && curY == bossY);
}
```

---

## 总结

本技术文档提供了完整的DNF门状态检测方案，包括：

- ✅ 门状态检测原理和实现
- ✅ 过图CALL调用方法
- ✅ 完整的C++代码示例
- ✅ 集成到其他项目的指南

**适用场景：**
- 自动刷图辅助
- 房间状态监控
- 游戏自动化工具开发

---

> 文档版本：v1.0
> 最后更新：2026-02-27
> 兼容DNF版本：需根据实际版本调整地址
