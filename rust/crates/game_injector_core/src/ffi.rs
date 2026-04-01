use crate::{InjectorConfig, InjectorConfigInterop, InjectorConfigView};
use game_core_protocols::{HELPER_STATUS_V5_SIZE, HELPER_STATUS_V5_VERSION};
use std::sync::OnceLock;

static DEFAULT_INI_TEXT_UTF8: OnceLock<Box<[u8]>> = OnceLock::new();

fn default_ini_text_utf8() -> &'static [u8] {
    DEFAULT_INI_TEXT_UTF8.get_or_init(|| {
        InjectorConfig::default_ini_text()
            .into_bytes()
            .into_boxed_slice()
    })
}

/// 第一阶段仅暴露稳定、低风险的 C ABI：
/// - 默认配置数值视图
/// - Helper 协议版本/尺寸
///
/// 后续再扩展到真正的配置文件读取与执行流程。
#[unsafe(no_mangle)]
pub extern "C" fn injector_core_default_view() -> InjectorConfigView {
    InjectorConfigView::from(&InjectorConfig::default())
}

#[unsafe(no_mangle)]
/// 解析 UTF-8 编码的 injector.ini 文本，输出第一阶段稳定的互操作配置视图。
///
/// # Safety
/// - `text_ptr` 必须指向长度为 `text_len` 的可读 UTF-8（或 UTF-8 兼容）字节缓冲区；
/// - `out_config` 必须指向一块可写的 `InjectorConfigInterop` 内存；
/// - 调用方必须保证这两个指针在本函数返回前始终有效。
pub unsafe extern "C" fn injector_core_parse_ini_text_utf8(
    text_ptr: *const u8,
    text_len: usize,
    out_config: *mut InjectorConfigInterop,
) -> u32 {
    if text_ptr.is_null() || out_config.is_null() {
        return 0;
    }

    // SAFETY: 调用方承诺 `text_ptr` 指向长度为 `text_len` 的只读缓冲区；
    // 这里仅在函数作用域内按字节切片读取，不越界、不持久化借用。
    let text = unsafe { std::slice::from_raw_parts(text_ptr, text_len) };
    let text = String::from_utf8_lossy(text);
    let config = InjectorConfig::parse_ini(&text);
    let Some(interop) = InjectorConfigInterop::from_config(&config) else {
        return 0;
    };
    // SAFETY: 调用方承诺 `out_config` 指向可写的 `InjectorConfigInterop` 存储；
    // 这里执行一次按值写入，不会重复释放或越界访问。
    unsafe {
        out_config.write(interop);
    }
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_default_ini_text_utf8() -> *const u8 {
    default_ini_text_utf8().as_ptr()
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_default_ini_text_utf8_len() -> usize {
    default_ini_text_utf8().len()
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_helper_status_version() -> u32 {
    HELPER_STATUS_V5_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn injector_core_helper_status_size() -> u32 {
    HELPER_STATUS_V5_SIZE
}
