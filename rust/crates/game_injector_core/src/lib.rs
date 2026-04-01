//! Injector 核心纯 Rust 逻辑。
//!
//! 当前阶段先承接“配置模型 + 默认值 + 路径归一化 + 协议常量”，
//! 后续再把进程扫描/注入执行逐步迁入 Rust。

mod config;
pub mod ffi;

pub use config::{InjectorConfig, InjectorConfigInterop, InjectorConfigView};
