//! 仅做与当前项目一致的 Windows 路径字符串处理，
//! 不依赖宿主系统文件系统语义，便于在 Linux CI 上做单元测试。

pub fn is_windows_absolute_path(path: &str) -> bool {
    let bytes = path.as_bytes();
    if bytes.len() >= 2 && bytes[1] == b':' {
        return true;
    }
    matches!(bytes.first(), Some(b'\\' | b'/'))
}

pub fn join_windows_path(left: &str, right: &str) -> String {
    if left.is_empty() {
        return right.to_string();
    }
    if right.is_empty() {
        return left.to_string();
    }

    let mut result = left.to_string();
    if !result.ends_with('\\') && !result.ends_with('/') {
        result.push('\\');
    }
    result.push_str(right);
    result
}

pub fn normalize_relative_to_base(path: &str, base_dir: &str) -> String {
    if path.trim().is_empty() {
        return path.to_string();
    }
    if is_windows_absolute_path(path) {
        return path.to_string();
    }
    join_windows_path(base_dir, path)
}

pub fn ensure_process_name(process_name: &str) -> String {
    let trimmed = process_name.trim();
    if trimmed.is_empty() {
        return "DNF.exe".to_string();
    }
    if trimmed.to_ascii_lowercase().ends_with(".exe") {
        trimmed.to_string()
    } else {
        format!("{trimmed}.exe")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn absolute_path_detection_matches_windows_style() {
        assert!(is_windows_absolute_path("C:\\a\\b"));
        assert!(is_windows_absolute_path("\\\\server\\share"));
        assert!(!is_windows_absolute_path("payload.dll"));
    }

    #[test]
    fn join_and_normalize_paths() {
        assert_eq!(
            join_windows_path("C:\\run", "game-payload.dll"),
            "C:\\run\\game-payload.dll"
        );
        assert_eq!(
            normalize_relative_to_base("game-payload.dll", "C:\\run"),
            "C:\\run\\game-payload.dll"
        );
        assert_eq!(
            normalize_relative_to_base("D:\\bin\\game-payload.dll", "C:\\run"),
            "D:\\bin\\game-payload.dll"
        );
    }

    #[test]
    fn process_name_is_normalized() {
        assert_eq!(ensure_process_name("DNF"), "DNF.exe");
        assert_eq!(ensure_process_name("dnf.EXE"), "dnf.EXE");
        assert_eq!(ensure_process_name(""), "DNF.exe");
    }
}
