//! Toolkit-independent Miro configuration, including upstream multi-key bindings.
use std::{collections::BTreeMap, path::PathBuf};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Binding {
    pub keys: Vec<String>,
    pub mode: String,
    pub action: String,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub bindings: Vec<Binding>,
    pub mouse: Vec<Binding>,
    pub settings: BTreeMap<String, serde_json::Value>,
    pub errors: Vec<String>,
}
pub fn directory() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            home::home_dir()
                .unwrap_or_else(|| PathBuf::from("."))
                .join(".config")
        })
        .join("miro-pdf")
}
fn words(line: &str) -> Result<Vec<String>, &'static str> {
    let mut out = Vec::new();
    let mut word = String::new();
    let mut quoted = false;
    let mut escaped = false;
    for c in line.chars() {
        if escaped {
            word.push(c);
            escaped = false;
            continue;
        }
        match c {
            '\\' if quoted => escaped = true,
            '"' => quoted = !quoted,
            '#' if !quoted => break,
            c if c.is_whitespace() && !quoted => {
                if !word.is_empty() {
                    out.push(std::mem::take(&mut word));
                }
            }
            c => word.push(c),
        }
    }
    if quoted || escaped {
        return Err("Unterminated quoted binding");
    }
    if !word.is_empty() {
        out.push(word);
    }
    Ok(out)
}
fn normalize_key(key: &str, mouse: bool) -> Result<String, String> {
    let mut parts = key.split('+').collect::<Vec<_>>();
    let raw = parts.pop().unwrap_or("");
    let mut ctrl = false;
    let mut shift = false;
    let mut alt = false;
    for modifier in parts {
        match modifier {
            "Ctrl" | "Control" => ctrl = true,
            "Shift" => shift = true,
            "Alt" => alt = true,
            "Super" | "Meta" | "Logo" | "Cmd" => {
                return Err("Super chords belong to the compositor".into());
            }
            _ => return Err(format!("Unknown modifier: {modifier}")),
        }
    }
    let mut name = if mouse {
        match raw.strip_prefix("Mouse").unwrap_or(raw) {
            "Left" => "MouseLeft",
            "Middle" => "MouseMiddle",
            "Right" => "MouseRight",
            "Back" => "MouseBack",
            "Forward" => "MouseForward",
            "ScrollUp" => "ScrollUp",
            "ScrollDown" => "ScrollDown",
            _ => return Err(format!("Unknown mouse button: {raw}")),
        }
        .to_string()
    } else {
        match raw {
            "Esc" => "Escape",
            "Return" => "Enter",
            _ => raw,
        }
        .to_string()
    };
    if name.is_empty() {
        return Err("Empty key".into());
    }
    if !mouse && name.len() == 1 && name.as_bytes()[0].is_ascii_alphabetic() {
        if ctrl {
            name = name.to_ascii_lowercase();
        } else if shift {
            name = name.to_ascii_uppercase();
            shift = false;
        }
    }
    Ok(format!(
        "{}{}{}{}",
        if ctrl { "Ctrl+" } else { "" },
        if alt { "Alt+" } else { "" },
        if shift { "Shift+" } else { "" },
        name
    ))
}
const ACTIONS: &str = "MoveUp MoveDown MoveLeft MoveRight NextPage PreviousPage PageUp PageDown HalfPageUp HalfPageDown ZoomHome ZoomFit ZoomFitWidth ZoomIn ZoomOut NextTab PreviousTab ToggleDarkModePdf ToggleDarkModeUi TogglePageBorders ToggleSidebar ToggleLinkHitboxes OpenFileFinder CloseActiveTab CloseTab PrintPdf Exit JumpBack JumpForward ToggleFullscreen TogglePresentationMode ToggleOverviewMode OverviewSelect OpenSearch CloseSearch ToggleSearchMethod NextSearchResult PreviousSearchResult SinglePageLayout DoublePageLayout DoublePageTitlePageLayout RotatePageClockwise RotatePageCounterClockwise RotateAllPagesClockwise RotateAllPagesCounterClockwise Copy SelectAll AddBookmark Reload GoToPage SaveCopy Properties Help";
impl Config {
    pub fn empty() -> Self {
        Self {
            bindings: Vec::new(),
            mouse: Vec::new(),
            settings: BTreeMap::new(),
            errors: Vec::new(),
        }
    }
    pub fn parse(&mut self, source: &str) {
        for (i, line) in source.lines().enumerate() {
            let result = (|| -> Result<(), String> {
                let w = words(line).map_err(str::to_string)?;
                if w.is_empty() {
                    return Ok(());
                }
                match w[0].as_str() {
                    "Bind" | "MouseBind" => {
                        if !(w.len() == 3 || w.len() == 4) {
                            return Err("Expected binding, optional mode, and action".into());
                        }
                        let mode = if w.len() == 4 {
                            w[2].as_str()
                        } else {
                            "Normal"
                        };
                        if !["Normal", "Presentation", "Overview"].contains(&mode) {
                            return Err(format!("Unknown mode: {mode}"));
                        }
                        let action = w.last().unwrap();
                        let mouse = w[0] == "MouseBind";
                        if !ACTIONS.split_whitespace().any(|a| a == action)
                            && !(mouse && ["Panning", "Selection"].contains(&action.as_str()))
                        {
                            return Err(format!("Unknown action: {action}"));
                        }
                        let keys = w[1]
                            .split_whitespace()
                            .map(|key| normalize_key(key, mouse))
                            .collect::<Result<Vec<_>, _>>()?;
                        if keys.is_empty() {
                            return Err("Empty binding".into());
                        }
                        if keys.iter().any(|k| {
                            k.split('+')
                                .any(|m| ["Super", "Meta", "Logo", "Cmd"].contains(&m))
                        }) {
                            return Err("Super chords belong to the compositor".into());
                        }
                        let list = if mouse {
                            &mut self.mouse
                        } else {
                            &mut self.bindings
                        };
                        list.retain(|b| b.mode != mode || b.keys != keys);
                        list.push(Binding {
                            keys,
                            mode: mode.into(),
                            action: action.clone(),
                        });
                    }
                    "Set" => {
                        if w.len() != 3 {
                            return Err("Expected setting and value".into());
                        }
                        let v = match w[1].as_str() {
                            "Rpc" | "RpcAllowLan" | "DarkModePdf" | "DarkModeUi"
                            | "OpenSidebar" | "PageBorders" | "OpenFullscreen"
                            | "OpenPresentation" => match w[2].to_lowercase().as_str() {
                                "true" | "1" => serde_json::json!(true),
                                "false" | "0" => serde_json::json!(false),
                                _ => return Err("Expected True or False".into()),
                            },
                            "DefaultSearchMethod" => {
                                if !["PlainText", "Regex"].contains(&w[2].as_str()) {
                                    return Err(
                                        "Unknown search method; use PlainText or Regex".into()
                                    );
                                }
                                serde_json::json!(w[2])
                            }
                            "RpcPort" => {
                                let n = w[2].parse::<u16>().map_err(|_| "Invalid RPC port")?;
                                if n == 0 {
                                    return Err("Invalid RPC port".into());
                                }
                                serde_json::json!(n)
                            }
                            "TrackpadSensitivity" => {
                                let n = w[2].parse::<f64>().map_err(|_| "Invalid sensitivity")?;
                                if !n.is_finite() || n <= 0.0 || n > 100.0 {
                                    return Err("Invalid sensitivity".into());
                                }
                                serde_json::json!(n)
                            }
                            _ => return Err(format!("Unknown setting: {}", w[1])),
                        };
                        self.settings.insert(w[1].clone(), v);
                    }
                    _ => return Err(format!("Unknown command: {}", w[0])),
                }
                Ok(())
            })();
            if let Err(e) = result {
                self.errors.push(format!("Line {}: {e}", i + 1));
            }
        }
    }
    pub fn load(path: Option<PathBuf>) -> Self {
        let mut c = Self::default();
        let path = path.unwrap_or_else(|| directory().join("miro.conf"));
        match std::fs::read_to_string(&path) {
            Ok(s) => c.parse(&s),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => c.errors.push(format!("{}: {e}", path.display())),
        };
        c
    }
}
impl Default for Config {
    fn default() -> Self {
        let mut c = Self::empty();
        c.parse(include_str!("../assets/default.conf"));
        c
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn default_bindings_are_valid_and_leave_super_to_dome() {
        let c = Config::default();
        assert!(c.errors.is_empty(), "{:?}", c.errors);
        assert!(
            c.bindings
                .iter()
                .any(|b| b.keys == ["Ctrl+f"] && b.action == "OpenSearch")
        );
    }
    #[test]
    fn chords_modes_and_user_overrides() {
        let mut c = Config::default();
        c.parse("Bind \"Z Z\" Normal CloseTab\nBind Ctrl+f Normal PageDown\nBind Escape Presentation TogglePresentationMode");
        assert!(c.errors.is_empty());
        assert_eq!(
            c.bindings
                .iter()
                .filter(|b| b.keys == ["Ctrl+f"] && b.mode == "Normal")
                .count(),
            1
        );
        assert!(c.bindings.iter().any(|b| b.keys == ["Z", "Z"]));
    }
    #[test]
    fn legacy_mouse_aliases_settings_and_modifier_order() {
        let mut c = Config::empty();
        c.parse("MouseBind Shift+Left Normal Selection\nBind Shift+Ctrl+F Normal OpenSearch\nSet DefaultSearchMethod Regex\nSet OpenFullscreen 1\nSet OpenPresentation False");
        assert!(c.errors.is_empty(), "{:?}", c.errors);
        assert_eq!(c.mouse[0].keys, ["Shift+MouseLeft"]);
        assert_eq!(c.bindings[0].keys, ["Ctrl+Shift+f"]);
        assert_eq!(c.settings["DefaultSearchMethod"], "Regex");
        assert_eq!(c.settings["OpenFullscreen"], true);
    }
    #[test]
    fn malformed_values_do_not_block_later_bindings() {
        let mut c = Config::empty();
        c.parse("Set RpcPort 90000\nBind Super+x Normal Exit\nBind \"oops Normal Exit\nBind x Normal NextPage\nSet DarkModeUi False");
        assert_eq!(c.errors.len(), 3);
        assert_eq!(c.bindings[0].action, "NextPage");
        assert_eq!(c.settings["DarkModeUi"], false);
    }
}
