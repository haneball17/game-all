/// 解析与当前 C++ 逻辑兼容的布尔值。
pub fn parse_bool_like(input: &str, default_value: bool) -> bool {
    let normalized = input.trim().to_ascii_lowercase();
    match normalized.as_str() {
        "1" | "true" | "yes" | "on" => true,
        "0" | "false" | "no" | "off" => false,
        _ => default_value,
    }
}

pub fn parse_u32_like(input: &str, default_value: u32) -> u32 {
    input.trim().parse::<u32>().unwrap_or(default_value)
}

pub fn trim_bom(input: &str) -> &str {
    input.strip_prefix('\u{feff}').unwrap_or(input)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_bool_variants() {
        assert!(parse_bool_like("true", false));
        assert!(parse_bool_like("YES", false));
        assert!(!parse_bool_like("off", true));
        assert!(parse_bool_like("unknown", true));
    }

    #[test]
    fn trim_utf8_bom() {
        assert_eq!(trim_bom("\u{feff}[injector]"), "[injector]");
        assert_eq!(trim_bom("plain"), "plain");
    }
}
