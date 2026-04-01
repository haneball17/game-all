use game_core_common::{ini::parse_ini_sections, path, value};
use game_core_protocols::{HELPER_STATUS_V5_SIZE, HELPER_STATUS_V5_VERSION};

pub const PROCESS_NAME_CAPACITY: usize = 64;
pub const PATH_TEXT_CAPACITY: usize = 1024;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InjectorConfig {
    pub process_name: String,
    pub dll_path: String,
    pub output_dir: String,
    pub scan_interval_ms: u32,
    pub inject_delay_ms: u32,
    pub window_wait_timeout_ms: u32,
    pub window_poll_interval_ms: u32,
    pub post_window_delay_ms: u32,
    pub max_retries: u32,
    pub retry_interval_ms: u32,
    pub success_timeout_ms: u32,
    pub success_interval_ms: u32,
    pub heartbeat_timeout_ms: u32,
    pub heartbeat_interval_ms: u32,
    pub watch_mode: bool,
    pub idle_exit_seconds: u32,
    pub max_concurrent_tasks: u32,
}

impl Default for InjectorConfig {
    fn default() -> Self {
        Self {
            process_name: "DNF.exe".to_string(),
            dll_path: "game-payload.dll".to_string(),
            output_dir: String::new(),
            scan_interval_ms: 1000,
            inject_delay_ms: 2000,
            window_wait_timeout_ms: 30000,
            window_poll_interval_ms: 500,
            post_window_delay_ms: 10000,
            max_retries: 3,
            retry_interval_ms: 1000,
            success_timeout_ms: 6000,
            success_interval_ms: 200,
            heartbeat_timeout_ms: 6000,
            heartbeat_interval_ms: 200,
            watch_mode: true,
            idle_exit_seconds: 600,
            max_concurrent_tasks: 3,
        }
    }
}

impl InjectorConfig {
    pub fn default_ini_text() -> String {
        [
            "[injector]",
            "; 目标进程名（不区分大小写，自动补 .exe）",
            "process_name=DNF.exe",
            "; DLL 路径（默认相对注入器输出目录）",
            "dll_path=game-payload.dll",
            "; 成功文件目录（为空则使用 DLL\\logs 目录）",
            "; output_dir=",
            "; 扫描进程间隔（毫秒）",
            "scan_interval_ms=1000",
            "; 等待窗口出现的超时（毫秒）",
            "window_wait_timeout_ms=30000",
            "; 窗口检测轮询间隔（毫秒）",
            "window_poll_interval_ms=500",
            "; 窗口出现后强制等待时间（毫秒）",
            "post_window_delay_ms=10000",
            "; 发现窗口后额外延迟（毫秒，可选）",
            "inject_delay_ms=0",
            "; 重试次数与间隔",
            "max_retries=3",
            "retry_interval_ms=1000",
            "; 成功文件检测",
            "success_timeout_ms=6000",
            "success_interval_ms=200",
            "; 共享内存心跳兜底",
            "heartbeat_timeout_ms=6000",
            "heartbeat_interval_ms=200",
            "; 常驻监听模式",
            "watch_mode=true",
            "; 无新目标进程出现后自动退出（秒，0 表示不退出）",
            "idle_exit_seconds=600",
            "; 并发注入任务上限（0 表示不限制）",
            "max_concurrent_tasks=3",
            "",
        ]
        .join("\r\n")
    }

    pub fn parse_ini(input: &str) -> Self {
        let mut config = Self::default();
        let sections = parse_ini_sections(input);
        let Some(injector) = sections.get("injector") else {
            return config;
        };

        if let Some(value) = injector.get("process_name") {
            config.process_name = path::ensure_process_name(value);
        }
        if let Some(value) = injector.get("dll_path") {
            config.dll_path = value.trim().to_string();
        }
        if let Some(value) = injector.get("output_dir") {
            config.output_dir = value.trim().to_string();
        }
        if let Some(value) = injector.get("scan_interval_ms") {
            config.scan_interval_ms = value::parse_u32_like(value, config.scan_interval_ms);
        }
        if let Some(value) = injector.get("inject_delay_ms") {
            config.inject_delay_ms = value::parse_u32_like(value, config.inject_delay_ms);
        }
        if let Some(value) = injector.get("window_wait_timeout_ms") {
            config.window_wait_timeout_ms =
                value::parse_u32_like(value, config.window_wait_timeout_ms);
        }
        if let Some(value) = injector.get("window_poll_interval_ms") {
            config.window_poll_interval_ms =
                value::parse_u32_like(value, config.window_poll_interval_ms);
        }
        if let Some(value) = injector.get("post_window_delay_ms") {
            config.post_window_delay_ms = value::parse_u32_like(value, config.post_window_delay_ms);
        }
        if let Some(value) = injector.get("max_retries") {
            config.max_retries = value::parse_u32_like(value, config.max_retries);
        }
        if let Some(value) = injector.get("retry_interval_ms") {
            config.retry_interval_ms = value::parse_u32_like(value, config.retry_interval_ms);
        }
        if let Some(value) = injector.get("success_timeout_ms") {
            config.success_timeout_ms = value::parse_u32_like(value, config.success_timeout_ms);
        }
        if let Some(value) = injector.get("success_interval_ms") {
            config.success_interval_ms = value::parse_u32_like(value, config.success_interval_ms);
        }
        if let Some(value) = injector.get("heartbeat_timeout_ms") {
            config.heartbeat_timeout_ms = value::parse_u32_like(value, config.heartbeat_timeout_ms);
        }
        if let Some(value) = injector.get("heartbeat_interval_ms") {
            config.heartbeat_interval_ms =
                value::parse_u32_like(value, config.heartbeat_interval_ms);
        }
        if let Some(value) = injector.get("watch_mode") {
            config.watch_mode = value::parse_bool_like(value, config.watch_mode);
        }
        if let Some(value) = injector.get("idle_exit_seconds") {
            config.idle_exit_seconds = value::parse_u32_like(value, config.idle_exit_seconds);
        }
        if let Some(value) = injector.get("max_concurrent_tasks") {
            config.max_concurrent_tasks = value::parse_u32_like(value, config.max_concurrent_tasks);
        }

        config
    }

    pub fn normalize_paths(&mut self, base_dir: &str) {
        self.process_name = path::ensure_process_name(&self.process_name);
        self.dll_path = path::normalize_relative_to_base(&self.dll_path, base_dir);
        if !self.output_dir.is_empty() {
            self.output_dir = path::normalize_relative_to_base(&self.output_dir, base_dir);
        }
    }

    pub fn helper_status_contract(&self) -> (u32, u32) {
        (HELPER_STATUS_V5_VERSION, HELPER_STATUS_V5_SIZE)
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorConfigView {
    pub scan_interval_ms: u32,
    pub inject_delay_ms: u32,
    pub window_wait_timeout_ms: u32,
    pub window_poll_interval_ms: u32,
    pub post_window_delay_ms: u32,
    pub max_retries: u32,
    pub retry_interval_ms: u32,
    pub success_timeout_ms: u32,
    pub success_interval_ms: u32,
    pub heartbeat_timeout_ms: u32,
    pub heartbeat_interval_ms: u32,
    pub watch_mode: u32,
    pub idle_exit_seconds: u32,
    pub max_concurrent_tasks: u32,
}

impl From<&InjectorConfig> for InjectorConfigView {
    fn from(value: &InjectorConfig) -> Self {
        Self {
            scan_interval_ms: value.scan_interval_ms,
            inject_delay_ms: value.inject_delay_ms,
            window_wait_timeout_ms: value.window_wait_timeout_ms,
            window_poll_interval_ms: value.window_poll_interval_ms,
            post_window_delay_ms: value.post_window_delay_ms,
            max_retries: value.max_retries,
            retry_interval_ms: value.retry_interval_ms,
            success_timeout_ms: value.success_timeout_ms,
            success_interval_ms: value.success_interval_ms,
            heartbeat_timeout_ms: value.heartbeat_timeout_ms,
            heartbeat_interval_ms: value.heartbeat_interval_ms,
            watch_mode: u32::from(value.watch_mode),
            idle_exit_seconds: value.idle_exit_seconds,
            max_concurrent_tasks: value.max_concurrent_tasks,
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InjectorConfigInterop {
    pub process_name: [u8; PROCESS_NAME_CAPACITY],
    pub dll_path: [u8; PATH_TEXT_CAPACITY],
    pub output_dir: [u8; PATH_TEXT_CAPACITY],
    pub view: InjectorConfigView,
}

impl InjectorConfigInterop {
    pub fn from_config(value: &InjectorConfig) -> Option<Self> {
        let mut interop = Self {
            process_name: [0; PROCESS_NAME_CAPACITY],
            dll_path: [0; PATH_TEXT_CAPACITY],
            output_dir: [0; PATH_TEXT_CAPACITY],
            view: InjectorConfigView::from(value),
        };
        copy_utf8_c_string(&mut interop.process_name, &value.process_name)?;
        copy_utf8_c_string(&mut interop.dll_path, &value.dll_path)?;
        copy_utf8_c_string(&mut interop.output_dir, &value.output_dir)?;
        Some(interop)
    }
}

fn copy_utf8_c_string<const N: usize>(dest: &mut [u8; N], value: &str) -> Option<()> {
    let bytes = value.as_bytes();
    if bytes.len() >= N {
        return None;
    }
    dest[..bytes.len()].copy_from_slice(bytes);
    dest[bytes.len()] = 0;
    Some(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_ini_contains_current_keys() {
        let text = InjectorConfig::default_ini_text();
        assert!(text.contains("[injector]"));
        assert!(text.contains("process_name=DNF.exe"));
        assert!(text.contains("watch_mode=true"));
    }

    #[test]
    fn parse_and_normalize_matches_cpp_defaults() {
        let input = "[injector]\nprocess_name=DNF\ndll_path=game-payload.dll\nwatch_mode=false\ninject_delay_ms=0\n";
        let mut config = InjectorConfig::parse_ini(input);
        config.normalize_paths("C:\\artifacts\\run");

        assert_eq!(config.process_name, "DNF.exe");
        assert_eq!(config.dll_path, "C:\\artifacts\\run\\game-payload.dll");
        assert!(!config.watch_mode);
        assert_eq!(config.inject_delay_ms, 0);
        assert_eq!(config.helper_status_contract(), (5, 152));
    }

    #[test]
    fn empty_ini_falls_back_to_defaults() {
        let config = InjectorConfig::parse_ini("");
        assert_eq!(config, InjectorConfig::default());
    }

    #[test]
    fn interop_view_contains_utf8_strings_and_values() {
        let config = InjectorConfig::parse_ini(
            "[injector]\nprocess_name=DNF\ndll_path=mods\\game-payload.dll\noutput_dir=logs\nmax_retries=9\n",
        );
        let interop = InjectorConfigInterop::from_config(&config).expect("interop");
        assert_eq!(interop.process_name[0..7], *b"DNF.exe");
        assert_eq!(interop.dll_path[0..21], *b"mods\\game-payload.dll");
        assert_eq!(interop.output_dir[0..4], *b"logs");
        assert_eq!(interop.view.max_retries, 9);
    }
}
