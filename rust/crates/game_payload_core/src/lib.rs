//! Payload 核心纯 Rust 状态机。
//!
//! 当前阶段先把最容易测试、最容易出错的“同步状态收敛”抽出来，
//! 作为未来从 C++ 迁移到 Rust 的第一批核心逻辑。

pub mod diagnostics;
pub mod ffi;
pub mod runtime;
pub mod sync;
