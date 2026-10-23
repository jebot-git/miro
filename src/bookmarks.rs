//! Compatible bookmark/recent-file persistence; application settings stay in miro.conf.
use std::{
    fs,
    io::{Read, Write},
    hash::Hasher,
    path::{Path, PathBuf},
};
use anyhow::{Result, Context};
use serde_json::{Value, json};
use twox_hash::XxHash64;
use crate::config::directory;

pub fn load(name: &str) -> Value {
    fs::read(directory().join(name))
        .ok()
        .and_then(|b| serde_json::from_slice(&b).ok())
        .unwrap_or_else(|| json!({}))
}
pub fn save(name: &str, value: &Value) -> Result<()> {
    let dir = directory();
    fs::create_dir_all(&dir)?;
    let path = dir.join(name);
    if path.exists() {
        let existing = fs::read(&path)?;
        serde_json::from_slice::<Value>(&existing).context(
            "Existing preferences contain invalid JSON; repair or back up the file before saving",
        )?;
    }
    let nonce = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)?
        .as_nanos();
    let temp = dir.join(format!(".{name}.{}.{nonce}.tmp", std::process::id()));
    let mut options = fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(&temp)?;
    file.write_all(&serde_json::to_vec_pretty(value)?)?;
    file.sync_all()?;
    fs::rename(temp, path).context("Save Miro preferences")
}
pub fn request(input: &Value) -> Result<Value> {
    let op = input["op"].as_str().unwrap_or("");
    match op {
        "load" => Ok(
            json!({"bookmarks":load("bookmarks.json"),"recent":load("recent_files.json"),"preferences":load("fltk-state.json")}),
        ),
        "preferences" => {
            save("fltk-state.json", &input["value"])?;
            Ok(json!({}))
        }
        "recent" => {
            let path = input["path"].as_str().unwrap_or("");
            let mut recent = load("recent_files.json")["files"]
                .as_array()
                .cloned()
                .unwrap_or_default();
            recent.retain(|v| v.as_str() != Some(path));
            recent.insert(0, json!(path));
            recent.truncate(12);
            let value = json!({"files":recent});
            save("recent_files.json", &value)?;
            Ok(value)
        }
        "bookmark_add" | "bookmark_delete" => {
            let path = PathBuf::from(input["path"].as_str().unwrap_or(""));
            let path = path.canonicalize().unwrap_or(path);
            let path_string = path.to_string_lossy().to_string();
            let mut store = load("bookmarks.json");
            if !store["sets"].is_array() {
                store = json!({"sets":[]});
            }
            let sets = store["sets"].as_array_mut().unwrap();
            let index=sets.iter().position(|s|s["path"]==path_string).unwrap_or_else(||{
                sets.push(json!({"path":path_string,"file_hash":hash_file(&path).unwrap_or(0),"marks":[]}));sets.len()-1
            });
            let marks = sets[index]["marks"]
                .as_array_mut()
                .context("Invalid bookmark store")?;
            if op == "bookmark_add" {
                let name = input["name"].as_str().unwrap_or("Bookmark");
                marks.push(json!({"name":name,"page":input["page"].as_u64().unwrap_or(0)}));
            } else {
                let index = input["index"].as_u64().unwrap_or(u64::MAX) as usize;
                if index < marks.len() {
                    marks.remove(index);
                }
            }
            save("bookmarks.json", &store)?;
            Ok(store)
        }
        _ => anyhow::bail!("Unknown state operation"),
    }
}
fn hash_file(path: &Path) -> Result<u64> {
    let mut hash = XxHash64::with_seed(1337);
    let mut file = fs::File::open(path)?;
    let mut chunk = [0u8; 65536];
    loop {
        let n = file.read(&mut chunk)?;
        if n == 0 {
            break;
        }
        hash.write(&chunk[..n]);
    }
    Ok(hash.finish())
}
