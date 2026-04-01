# Rust Core Workspace

该目录承载 `game-all` 的原生核心 Rust 重构。

## 当前状态
- 已落地协议镜像、通用基础设施、Injector 核心配置层、Payload 同步状态机原型。
- 目前仍是 **可回退的并行落地**，尚未替换现有 C++ 运行入口。

## crate 说明
- `game_core_protocols`：共享协议镜像与布局测试
- `game_core_common`：INI/路径/兼容值解析
- `game_injector_core`：Injector 配置模型与最小 C ABI
- `game_payload_core`：Sync 方向键释放收敛与缓存策略

## 本地验证
```bash
cd rust
cargo test
cargo clippy --all-targets --all-features -- -D warnings
```

## 后续接入方向
1. 先把 `game_injector_core` 静态库接到现有 `Injector` 工程。
2. 再将 `Payload` 的共享内存读写、状态机和诊断逻辑逐步迁入 Rust。
