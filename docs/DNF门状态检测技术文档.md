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
人物基址 → 地图偏移 → +280 → 解密读取
```

### 过图原理

通过调用游戏内部函数（过图Call），传入方向参数触发房间切换：

```
房间编号 → 内部对象链 → 过图函数 + 方向参数
```

---

## 内存地址映射表

### 基础基址

| 名称 | 地址 (十进制) | 地址 (十六进制) | 说明 |
|------|--------------|----------------|------|
| 人物基址 | 81325736 | 0x4D8EEA8 | 角色对象基础地址 |
| 地图偏移 | 188 | 0xBC | 地图对象偏移量 |
| 房间编号 | 80315732 | 0x4C98554 | 当前房间信息基址 |
| 时间基址 | 2138152 | 0x20A028 | 房间结构时间基址 |
| 过图Call | 22239664 | 0x15359B0 | 过图函数地址 |
| 解密基址 | 81589368 | 0x4DCF478 | 数据解密函数基址 |

### 关键偏移

| 偏移位置 | 说明 | 数据类型 |
|---------|------|---------|
| 地图偏移 + 280 | 门状态标记 | 加密int |
| 时间基址 + 0x20A024 | 房间一级对象 | 指针 |
| + 0x8C | 房间二级对象 | 指针 |

---

## 代码实现

### 1. 基础工具函数

```cpp
#include <Windows.h>
#include <TlHelp32.h>

// 进程句柄（需要通过进程名或窗口获取）
HANDLE g_hProcess = NULL;

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
int Decrypt(int value) {
    // DNF使用简单的异或加密
    return value ^ 0x4DCF478;  // 解密基址作为密钥
}
```

### 2. 门状态检测函数

```cpp
// 检测当前房间的门是否已打开（怪物是否已清除）
bool IsDoorOpen() {
    const DWORD 人物基址 = 0x81325736;
    const DWORD 地图偏移 = 0xBC;

    // 获取人物对象
    int playerBase = ReadInt(人物基址);
    if (playerBase == 0) return false;

    // 获取地图对象
    int mapBase = ReadInt(playerBase + 地图偏移);
    if (mapBase == 0) return false;

    // 读取门状态（+280偏移）
    int doorStateEncrypted = ReadInt(mapBase + 280);
    int doorState = Decrypt(doorStateEncrypted);

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
    const DWORD 房间编号 = 0x80315732;
    const DWORD 过图Call = 0x22239664;

    __asm {
        // 获取房间对象
        mov ecx, 房间编号
        mov ecx, [ecx]
        mov ecx, [ecx + 0x20A024]
        mov ecx, [ecx + 0x8C]

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
        mov eax, 过图Call
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
    // 地址常量
    static const DWORD ADDR_PLAYER_BASE = 0x81325736;
    static const DWORD ADDR_MAP_OFFSET = 0xBC;
    static const DWORD ADDR_ROOM_INFO = 0x80315732;
    static const DWORD ADDR_CHANGE_ROOM_CALL = 0x22239664;
    static const DWORD ADDR_DECRYPT_BASE = 0x4DCF478;

    // 方向枚举
    enum Direction {
        DIR_LEFT = 0,
        DIR_RIGHT = 1,
        DIR_UP = 2,
        DIR_DOWN = 3
    };

    // 初始化
    bool Init();

    // 检测门是否打开
    bool IsDoorOpen();

    // 过图
    void ChangeRoom(Direction dir);

    // 是否在副本中
    bool IsInDungeon();

private:
    HANDLE m_hProcess;
    int Decrypt(int value);

    template<typename T>
    T ReadMemory(DWORD addr);
};
```

```cpp
// DNFDoorDetector.cpp
#include "DNFDoorDetector.h"

bool DNFDoorDetector::Init() {
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

    // 读取路径: 人物基址 → +地图偏移 → +280 → 解密
    int playerBase = ReadMemory<int>(ADDR_PLAYER_BASE);
    if (playerBase == 0) return false;

    int mapBase = ReadMemory<int>(playerBase + ADDR_MAP_OFFSET);
    if (mapBase == 0) return false;

    int doorState = ReadMemory<int>(mapBase + 280);
    doorState = Decrypt(doorState);

    return doorState == 0;
}

void DNFDoorDetector::ChangeRoom(Direction dir) {
    const DWORD offset1 = 0x20A024;
    const DWORD offset2 = 0x8C;

    __asm {
        mov ecx, ADDR_ROOM_INFO
        mov ecx, [ecx]
        mov ecx, [ecx + offset1]
        mov ecx, [ecx + offset2]

        push 0xFF
        push 0xFF
        push 0x00
        push 0x00
        push 0x00
        push 0x00
        push 0x00
        push dir

        mov eax, ADDR_CHANGE_ROOM_CALL
        call eax
    }
}

bool DNFDoorDetector::IsInDungeon() {
    if (!m_hProcess) return false;

    int playerBase = ReadMemory<int>(ADDR_PLAYER_BASE);
    if (playerBase == 0) return false;

    int mapBase = ReadMemory<int>(playerBase + ADDR_MAP_OFFSET);
    // 在副本中 mapBase != 0，在城镇中 mapBase == 0
    return mapBase != 0;
}

int DNFDoorDetector::Decrypt(int value) {
    return value ^ ADDR_DECRYPT_BASE;
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

    if (!detector.Init()) {
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

如果地址失效，可用以下方法重新扫描：

1. **打开游戏，进入副本**
2. **使用CE搜索特征值：**
   - 门关闭时：非0值
   - 门开启时：0
3. **找出基址和偏移链**
4. **更新代码中的常量**

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
    const DWORD ADDR_TIME_BASE = 0x20A028;
    const DWORD OFFSET_BOSS_X = 0xB70;  // BOSS房间X坐标偏移
    const DWORD OFFSET_BOSS_Y = 0xB78;  // BOSS房间Y坐标偏移
    const DWORD OFFSET_CUR_X = 0xAC4;   // 当前房间X坐标偏移
    const DWORD OFFSET_CUR_Y = 0xACC;   // 当前房间Y坐标偏移
    const DWORD OFFSET_ROOM_BASE = 0xCC;

    int roomBase = ReadMemory<int>(ADDR_ROOM_INFO);
    if (roomBase == 0) return false;

    int timeBase = ReadMemory<int>(roomBase + ADDR_TIME_BASE);
    if (timeBase == 0) return false;

    int roomStruct = ReadMemory<int>(timeBase + OFFSET_ROOM_BASE);
    if (roomStruct == 0) return false;

    int bossX = Decrypt(ReadMemory<int>(roomStruct + OFFSET_BOSS_X));
    int bossY = Decrypt(ReadMemory<int>(roomStruct + OFFSET_BOSS_Y));
    int curX = ReadMemory<int>(roomStruct + OFFSET_CUR_X);
    int curY = ReadMemory<int>(roomStruct + OFFSET_CUR_Y);

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
