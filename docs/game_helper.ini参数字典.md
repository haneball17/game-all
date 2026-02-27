# game_helper.ini 参数字典

## 1. 文档目的

本文档用于维护 `HelperMod` 的 `game_helper.ini` 配置项定义，作为长期版本迭代的字段基线。

代码来源：
- `Payload/modules/helper/HelperMod.cpp`
- `config-templates/game_helper.ini`

## 2. 配置文件定位与加载顺序

固定文件名：`game_helper.ini`

加载顺序（命中即停止）：
1. `模块目录\\config\\game_helper.ini`
2. `可执行目录\\config\\game_helper.ini`
3. `模块目录\\game_helper.ini`（兼容旧路径）
4. `可执行目录\\game_helper.ini`（兼容旧路径）

说明：
- 在 `config` 路径模式下，如果文件不存在，会自动创建默认配置文件。
- 支持热重载（文件变更通知 + 时间戳回退检测）。

## 3. 通用解析规则

1. 布尔值（`ReadIniBool`）
- true 值：`1/true/yes/on`
- false 值：`0/false/no/off`
- 其他值：回退默认值

2. 无符号整数（`ReadIniUInt32`）
- 支持十进制与十六进制（`0x` 前缀）
- 解析失败时回退默认值

3. 浮点数（`ReadIniFloat`）
- 解析失败时回退默认值

4. 热重载生效说明
- “热重载=是”表示修改后无需重注入即可通过热重载生效。
- “热重载=否（启动期）”表示仅在模块初始化阶段生效，修改后需重注入。

## 4. 参数字典

### [startup]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `startup_delay_ms` | `uint32` | `0` | 否（启动期） | 初始化后延迟（毫秒）再继续启动后续线程。 |
| `safe_mode` | `bool` | `false` | 否（启动期） | 安全模式。开启后直接返回，不启动主要功能逻辑。 |

### [patch]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `apply_fullscreen_attack_patch` | `bool` | `false` | 否（当前保留） | 当前版本仅记录日志字段 `apply_fullscreen_attack_patch_ignored`，实际不执行该开关逻辑。 |
| `fullscreen_attack_poll_interval_ms` | `uint32` | `1000` | 是 | 全屏攻击守护轮询间隔（毫秒）。配置为 `0` 时运行时会按 `1ms` 处理。 |

### [stealth]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `wipe_pe_header` | `bool` | `false` | 否（启动期） | 是否抹除 PE 头。仅当 `payload.ini` 里 `EnableStealth=true` 且 `safe_mode=false` 时才会真正执行。 |

### [feature]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `disable_input_thread` | `bool` | `false` | 否（启动期） | 禁用输入轮询线程创建。 |
| `disable_attract_thread` | `bool` | `false` | 否（当前保留） | 当前版本仅输出 `attract_thread config_disabled` 日志，不影响主逻辑线程创建。 |
| `enable_summon_doll` | `bool` | `true` | 是 | 召唤人偶总开关。 |
| `summon_monster_id` | `uint32` | `25301` | 是 | 召唤怪物 ID。 |
| `summon_level` | `uint32` | `70` | 是 | 召唤等级。 |
| `summon_cooldown_ms` | `uint32` | `0` | 是 | 召唤冷却（毫秒）。 |
| `enable_fullscreen_skill` | `bool` | `true` | 是 | 全屏技能功能总开关。 |
| `player_name_encoding` | `string` | `auto` | 是 | 角色名解码模式：`auto/utf16/unicode/utf8/ansi/gbk/cp936`。 |

### [fullscreen]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `skill_code` | `uint32` | `20022` | 是 | 全屏技能代码。 |
| `skill_damage` | `uint32` | `13333` | 是 | 全屏技能伤害参数。 |
| `skill_interval` | `uint32` | `1000` | 是 | 全屏技能触发间隔（毫秒）。激活状态下配置为 `0` 时按 `1ms` 处理。 |
| `hotkey_vk` | `uint32` | `36` | 是 | 全屏技能热键（VK 码）。若 `[hotkey].toggle_fullscreen_skill` 存在，将以后者为准。 |

### [damage]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `enable_damage_hook` | `bool` | `false` | 是 | 倍攻 Hook 开关。 |
| `damage_multiplier` | `uint32` | `10` | 是 | 倍攻倍率。运行时会钳制为 `[1, 1000]`。 |
| `invincible_enabled` | `bool` | `false` | 是 | 怪物零伤（无敌）开关。 |

### [hotkey]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `toggle_transparent` | `uint32` | `113` | 是 | 自动透明切换热键（默认 F2）。 |
| `toggle_fullscreen_attack` | `uint32` | `114` | 是 | 全屏攻击切换热键（默认 F3）。 |
| `summon_doll` | `uint32` | `123` | 是 | 召唤热键（默认 F12）。 |
| `attract_mode1` | `uint32` | `55` | 是 | 吸怪模式1热键（默认 `7`）。 |
| `attract_mode2` | `uint32` | `56` | 是 | 吸怪模式2热键（默认 `8`）。 |
| `attract_mode3` | `uint32` | `57` | 是 | 吸怪模式3热键（默认 `9`）。 |
| `attract_mode4` | `uint32` | `48` | 是 | 吸怪模式4热键（默认 `0`）。 |
| `toggle_attract_direction` | `uint32` | `189` | 是 | 吸怪方向切换热键（默认 `-`）。 |
| `toggle_gather_items` | `uint32` | `220` | 是 | 聚物切换热键（默认 `\\`）。 |
| `toggle_fullscreen_skill` | `uint32` | `36` | 是 | 全屏技能热键（VK）。会覆盖 `[fullscreen].hotkey_vk`。 |

### [attract]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `monster_x_offset_mode1` | `float` | `0.0` | 是 | 吸怪模式1 X 偏移。 |
| `monster_x_offset_mode2` | `float` | `80.0` | 是 | 吸怪模式2 X 偏移。 |
| `monster_x_offset_mode3` | `float` | `150.0` | 是 | 吸怪模式3 X 偏移。 |
| `monster_x_offset_mode4` | `float` | `250.0` | 是 | 吸怪模式4 X 偏移。 |

补充：
- 若配置为负值，运行时会自动转为绝对值（取正）。

### [address]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `player_base` | `uint32` | `0x01AC790C` | 是 | 角色基址。 |
| `fullscreen_attack_patch` | `uint32` | `0x00825282` | 是 | 全屏攻击补丁地址。 |
| `transparent_call` | `uint32` | `0x011499E0` | 是 | 透明调用地址。 |
| `summon_call_param` | `uint32` | `0x0119FEF0` | 是 | 召唤调用参数地址。 |
| `summon_function_offset` | `uint32` | `0x00000354` | 是 | 召唤函数偏移。 |
| `summon_position_param` | `uint32` | `0x000008AE` | 是 | 召唤位置参数。 |
| `fullscreen_skill_call` | `uint32` | `0x00879320` | 是 | 全屏技能调用地址。 |
| `damage_hook` | `uint32` | `0x0087B8E3` | 是 | 倍攻 Hook 地址。 |

### [offset]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `map` | `uint32` | `0x000000B8` | 是 | 地图指针偏移。 |
| `map_start` | `uint32` | `0x000000B0` | 是 | 对象数组起始偏移。 |
| `map_end` | `uint32` | `0x000000B4` | 是 | 对象数组结束偏移。 |
| `type` | `uint32` | `0x00000094` | 是 | 对象类型偏移。 |
| `position_x` | `uint32` | `0x0000018C` | 是 | 角色 X 坐标偏移。 |
| `position_y` | `uint32` | `0x00000190` | 是 | 角色 Y 坐标偏移。 |
| `player_name` | `uint32` | `0x00000258` | 是 | 角色名指针偏移。 |
| `player_name_second` | `uint32` | `0x00000000` | 是 | 角色名二级偏移。 |
| `object_position_base` | `uint32` | `0x000000A8` | 是 | 对象坐标基址偏移。 |
| `object_position_x` | `uint32` | `0x0000000C` | 是 | 对象 X 坐标偏移。 |
| `object_position_y` | `uint32` | `0x00000010` | 是 | 对象 Y 坐标偏移。 |
| `faction` | `uint32` | `0x00000644` | 是 | 阵营偏移。 |
| `fullscreen_skill_type` | `uint32` | `0x00000090` | 是 | 全屏技能对象类型偏移。 |
| `fullscreen_skill_pos_x` | `uint32` | `0x00003CE4` | 是 | 全屏技能对象 X 偏移。 |
| `fullscreen_skill_pos_y` | `uint32` | `0x00003CE8` | 是 | 全屏技能对象 Y 偏移。 |

### [output]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `output_dir` | `string` | 空 | 否（启动期） | success 文件输出目录。若目录无效会回退默认目录并记录 `output_dir_invalid` 日志。 |

### [gui_log]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `log_level` | `string` | `INFO` | 否（当前保留） | 当前 `HelperMod` 未读取该字段。 |
| `log_path` | `string` | `auto` | 否（当前保留） | 当前 `HelperMod` 未读取该字段。 |
| `console_output` | `bool` | `false` | 否（当前保留） | 当前 `HelperMod` 未读取该字段。 |

### [debug]

| 键 | 类型 | 默认值 | 热重载 | 说明 |
|---|---|---:|---|---|
| `enable_runtime_address_log` | `bool` | `false` | 是 | 为 `true` 时，每次应用配置输出 `runtime_address` / `runtime_offset`；默认关闭以减少日志噪声。 |

## 5. 建议维护流程

1. 先改 `config-templates/game_helper.ini`，再同步代码默认值（如有需要）。
2. 新增字段时同步更新：
- `HelperConfig`
- `GetDefaultHelperConfig()`
- `WriteDefaultConfigFile()`
- `LoadHelperConfig()`
- 本文档
3. 版本切换时，先在测试环境打开 `enable_runtime_address_log=true` 对比实际生效值，再恢复为 `false`。

