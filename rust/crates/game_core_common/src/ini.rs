use std::collections::BTreeMap;

use crate::value::trim_bom;

/// 极简 INI 解析器，目标是兼容当前项目已有的简单配置格式。
pub fn parse_ini_sections(input: &str) -> BTreeMap<String, BTreeMap<String, String>> {
    let mut sections: BTreeMap<String, BTreeMap<String, String>> = BTreeMap::new();
    let mut current_section = String::new();

    for raw_line in trim_bom(input).lines() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }

        if line.starts_with('[') && line.ends_with(']') && line.len() >= 2 {
            current_section = line[1..line.len() - 1].trim().to_ascii_lowercase();
            sections.entry(current_section.clone()).or_default();
            continue;
        }

        let Some((raw_key, raw_value)) = line.split_once('=') else {
            continue;
        };
        let key = raw_key.trim().to_ascii_lowercase();
        let value = raw_value.trim().to_string();
        sections
            .entry(current_section.clone())
            .or_default()
            .insert(key, value);
    }

    sections
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_ini_by_section() {
        let parsed = parse_ini_sections(
            "\u{feff}[Injector]\nprocess_name=DNF.exe\n; comment\nwatch_mode=true\n",
        );
        let injector = parsed.get("injector").expect("missing injector section");
        assert_eq!(
            injector.get("process_name").map(String::as_str),
            Some("DNF.exe")
        );
        assert_eq!(injector.get("watch_mode").map(String::as_str), Some("true"));
    }
}
