# HelperMod 配置驱动回归清单

## 1. 环境前置

- 目标构建：`x86 Release`
- 目标配置文件：`artifacts/run/config/game_helper.ini`
- 建议先确认：
  - `[debug] enable_runtime_address_log=false`（默认降噪）
  - 仅排障时改为 `true`

## 2. 注入与启动基础检查

1. 执行注入。
2. 检查日志文件：`artifacts/run/logs/helper/helper_*.log.jsonl`
3. 验证事件：
   - `worker_thread`
   - `config_loaded`
   - `background_worker_thread`
   - `shared_memory`
   - `control_memory`

验收标准：
- 上述事件均出现且无 `create_failed` 类错误。

## 3. 核心功能回归

### 3.1 自动透明

1. 热键：`F2`（默认）
2. 期望：
   - 日志出现 `auto_transparent enabled/disabled`
   - 功能开启后角色透明状态行为正常

### 3.2 全屏攻击

1. 热键：`F3`（默认）
2. 期望：
   - 日志出现 `fullscreen_attack target_enabled/target_disabled`
   - `default_off` 启动时可正常写回关闭态

### 3.3 召唤人偶

1. 热键：`F12`（默认）
2. 期望：
   - 日志出现 `summon_doll triggered`
   - 无崩溃、无卡死

### 3.4 全屏技能

1. 热键：`Home`（默认）
2. 期望：
   - 日志出现 `fullscreen_skill activated/deactivated`
   - 技能按间隔触发，无明显异常

### 3.5 共享内存心跳

1. GUI 或调试工具读取 `HelperStatusV5`。
2. 期望：
   - `last_tick_ms` 持续更新
   - 功能位与实际操作一致

## 4. 配置驱动专项检查

1. 修改 `[address]/[offset]` 任意字段为无效值后注入，验证：
   - 行为异常可复现
   - 恢复正确值后恢复正常
2. 打开 `enable_runtime_address_log=true`，验证：
   - 出现 `runtime_address` 与 `runtime_offset` 事件
   - 日志值与 INI 值一致
3. 关闭 `enable_runtime_address_log=false`，验证：
   - `runtime_*` 事件不再输出

## 5. 本轮执行记录（2026-02-27）

- 当前代码改造已完成：
  - `runtime_address/runtime_offset` 支持开关，默认关闭。
  - 基线参数已落地到 `artifacts/run/config/game_helper.ini`。
- 当前环境限制：
  - 本仓库执行环境无法直接完成 Windows 实机注入验证，需在目标机按本清单执行并回填结果。

