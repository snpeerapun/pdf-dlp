//! Sensitivity-label discovery compatible with Microsoft Purview / Information Protection (MSIP).
//! Sources: Windows local policy caches (best effort), already-labelled files (PDF / Office), manual.
use regex::bytes::Regex;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Label {
    pub id: String,          // MSIP GUID (lowercase) or "local:<slug>" for app-defined labels
    pub name: String,
    #[serde(default)] pub tooltip: String,
    #[serde(default)] pub color: String,  // "#rrggbb" or ""
    #[serde(default)] pub parent: String,
    #[serde(default)] pub order: i64,
    #[serde(default)] pub source: String, // "windows-cache:<path>" | "file:<path>" | "manual"
}

#[derive(Serialize, Default)]
pub struct Discovery { pub labels: Vec<Label>, pub scanned: Vec<String>, pub notes: Vec<String> }

/// Label stamped on a file in MSIP form (what Purview writes into PDFs / Office docs).
#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct FileLabel {
    pub id: String,
    pub name: String,
    pub enabled: bool,
    pub set_date: String,
    pub method: String,
    pub site_id: String,
    pub content_bits: String,
    pub action_id: String,
    pub owner: String,
}

/// Parse MSIP_Label_<guid>_<field> entries out of a flat metadata map.
pub fn file_label_from_meta(meta: &BTreeMap<String, String>) -> Option<FileLabel> {
    let re = regex::Regex::new(r"^MSIP_Label_([0-9a-fA-F-]{36})_(\w+)$").ok()?;
    let mut by_id: BTreeMap<String, FileLabel> = BTreeMap::new();
    for (k, v) in meta {
        if let Some(c) = re.captures(k) {
            let id = c[1].to_lowercase();
            let f = by_id.entry(id.clone()).or_insert_with(|| FileLabel { id: id.clone(), ..Default::default() });
            match c[2].to_ascii_lowercase().as_str() {
                "name" => f.name = v.clone(),
                "enabled" => f.enabled = v.eq_ignore_ascii_case("true"),
                "setdate" => f.set_date = v.clone(),
                "method" => f.method = v.clone(),
                "siteid" => f.site_id = v.clone(),
                "contentbits" => f.content_bits = v.clone(),
                "actionid" => f.action_id = v.clone(),
                "owner" => f.owner = v.clone(),
                _ => {}
            }
        }
    }
    // Prefer an enabled label; otherwise the first one.
    by_id.values().find(|f| f.enabled).cloned().or_else(|| by_id.values().next().cloned())
}

pub fn new_guid() -> String {
    use rand::RngExt as _;
    let mut b = [0u8; 16];
    rand::rng().fill(&mut b);
    b[6] = (b[6] & 0x0f) | 0x40; b[8] = (b[8] & 0x3f) | 0x80;
    format!("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15])
}

/// Build the MSIP metadata set for applying `label` (id must be a GUID for Purview compatibility).
pub fn msip_metadata(label: &Label, site_id: &str, content_bits: u32, owner: &str) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    if label.id.len() != 36 { return m; }
    let p = format!("MSIP_Label_{}_", label.id.to_lowercase());
    m.insert(format!("{p}Enabled"), "true".into());
    m.insert(format!("{p}Name"), label.name.clone());
    m.insert(format!("{p}SetDate"), chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string());
    m.insert(format!("{p}Method"), "Privileged".into());
    m.insert(format!("{p}ContentBits"), content_bits.to_string());
    m.insert(format!("{p}ActionId"), new_guid());
    if !site_id.is_empty() { m.insert(format!("{p}SiteId"), site_id.into()); }
    if !owner.is_empty() { m.insert(format!("{p}Owner"), owner.into()); }
    m
}

/// Keys that remove any previous MSIP label (value "" = delete) for a metadata map.
pub fn msip_clear_keys(meta: &BTreeMap<String, String>) -> BTreeMap<String, String> {
    meta.keys().filter(|k| k.starts_with("MSIP_Label_")).map(|k| (k.clone(), String::new())).collect()
}

// ---------- discovery from Windows caches ----------

fn candidate_dirs() -> Vec<PathBuf> {
    let mut v = vec![];
    if let Ok(l) = std::env::var("LOCALAPPDATA") {
        let l = PathBuf::from(l);
        v.push(l.join("Microsoft").join("Office").join("CLP"));
        v.push(l.join("Microsoft").join("MSIP"));
        v.push(l.join("Microsoft").join("Office").join("DLP"));
    }
    if let Ok(h) = std::env::var("HOME") {
        // Office for Mac keeps the same CLP cache under the group container.
        let h = PathBuf::from(h);
        v.push(h.join("Library/Group Containers/UBF8T346G9.Office/CLP"));
        v.push(h.join("Library/Containers/com.microsoft.Word/Data/Library/Application Support/Microsoft/Office/CLP"));
    }
    v
}

fn walk(dir: &Path, out: &mut Vec<PathBuf>, depth: u32) {
    if depth > 6 { return; }
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    for ent in rd.flatten() {
        let p = ent.path();
        if p.is_dir() { walk(&p, out, depth + 1); }
        else if let Ok(md) = ent.metadata() { if md.len() < 32 * 1024 * 1024 { out.push(p); } }
    }
}

fn extract_labels_from_bytes(data: &[u8], source: &str) -> Vec<Label> {
    let mut out: BTreeMap<String, Label> = BTreeMap::new();
    let guid = r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";
    // JSON: {"id":"<guid>", ... "name":"..."} in either order, within one object.
    let pats = [
        format!(r#""id"\s*:\s*"({guid})"[^{{}}]{{0,600}}?"name"\s*:\s*"([^"]{{1,120}})""#),
        format!(r#""name"\s*:\s*"([^"]{{1,120}})"[^{{}}]{{0,600}}?"id"\s*:\s*"({guid})""#),
        // XML attributes
        format!(r#"<[Ll]abel[^>]{{0,400}}?[Ii]d="({guid})"[^>]{{0,400}}?[Nn]ame="([^"]{{1,120}})""#),
        format!(r#"<[Ll]abel[^>]{{0,400}}?[Nn]ame="([^"]{{1,120}})"[^>]{{0,400}}?[Ii]d="({guid})""#),
    ];
    for (i, p) in pats.iter().enumerate() {
        let Ok(re) = Regex::new(p) else { continue };
        for c in re.captures_iter(data) {
            let (id, name) = if i % 2 == 0 { (&c[1], &c[2]) } else { (&c[2], &c[1]) };
            let id = String::from_utf8_lossy(id).to_lowercase();
            let name = String::from_utf8_lossy(name).to_string();
            if name.contains('\\') || name.trim().is_empty() { continue; }
            out.entry(id.clone()).or_insert(Label { id, name, tooltip: String::new(), color: String::new(), parent: String::new(), order: 0, source: source.to_string() });
        }
    }
    // Optional color / tooltip next to the id
    if let Ok(re) = Regex::new(&format!(r#""id"\s*:\s*"({guid})"[^{{}}]{{0,900}}?"color"\s*:\s*"(#?[0-9a-fA-F]{{6}})""#)) {
        for c in re.captures_iter(data) {
            let id = String::from_utf8_lossy(&c[1]).to_lowercase();
            if let Some(l) = out.get_mut(&id) { let col = String::from_utf8_lossy(&c[2]).to_string(); l.color = if col.starts_with('#') { col } else { format!("#{col}") }; }
        }
    }
    if let Ok(re) = Regex::new(&format!(r#""id"\s*:\s*"({guid})"[^{{}}]{{0,900}}?"tooltip"\s*:\s*"([^"]{{1,300}})""#)) {
        for c in re.captures_iter(data) {
            let id = String::from_utf8_lossy(&c[1]).to_lowercase();
            if let Some(l) = out.get_mut(&id) { l.tooltip = String::from_utf8_lossy(&c[2]).to_string(); }
        }
    }
    let mut v: Vec<Label> = out.into_values().collect();
    for (i, l) in v.iter_mut().enumerate() { l.order = i as i64; }
    v
}

pub fn discover_local() -> Discovery {
    let mut d = Discovery::default();
    let mut files = vec![];
    for dir in candidate_dirs() {
        if dir.exists() { d.scanned.push(dir.display().to_string()); walk(&dir, &mut files, 0); }
    }
    if d.scanned.is_empty() {
        d.notes.push("ไม่พบโฟลเดอร์แคช Microsoft Information Protection บนเครื่องนี้ (ต้องมี Office/Purview client ที่ล็อกอินองค์กรแล้ว)".into());
        return d;
    }
    let mut seen = BTreeMap::new();
    for f in files {
        let Ok(data) = std::fs::read(&f) else { continue };
        // zip-based caches: scan inner entries too
        let mut blobs: Vec<(Vec<u8>, String)> = vec![];
        if data.starts_with(b"PK\x03\x04") {
            if let Ok(mut z) = zip::ZipArchive::new(std::io::Cursor::new(&data)) {
                for i in 0..z.len() {
                    if let Ok(mut e) = z.by_index(i) {
                        let mut b = vec![]; use std::io::Read; if e.read_to_end(&mut b).is_ok() { blobs.push((b, format!("{}!{}", f.display(), e.name()))); }
                    }
                }
            }
        } else { blobs.push((data, f.display().to_string())); }
        for (b, src) in blobs {
            for l in extract_labels_from_bytes(&b, &format!("windows-cache:{src}")) {
                seen.entry(l.id.clone()).or_insert(l);
            }
        }
    }
    d.labels = seen.into_values().collect();
    if d.labels.is_empty() { d.notes.push("สแกนแคชแล้วแต่ไม่พบนิยาม label ที่อ่านได้ (อาจถูกเข้ารหัสด้วย DPAPI) — ใช้วิธี 'เรียนรู้จากไฟล์ที่ติด label' แทน".into()); }
    d
}

/// Learn labels from an already-labelled file: PDF (Info dict) or Office OOXML (docProps/custom.xml).
pub fn labels_from_file(path: &Path, pdf_meta: Option<&BTreeMap<String, String>>) -> Result<Vec<Label>, String> {
    let src = format!("file:{}", path.display());
    if let Some(meta) = pdf_meta {
        return Ok(file_label_from_meta(meta).map(|f| vec![Label { id: f.id, name: f.name, tooltip: String::new(), color: String::new(), parent: String::new(), order: 0, source: src }]).unwrap_or_default());
    }
    let data = std::fs::read(path).map_err(|e| e.to_string())?;
    let mut meta = BTreeMap::new();
    if data.starts_with(b"PK\x03\x04") {
        let mut z = zip::ZipArchive::new(std::io::Cursor::new(&data)).map_err(|e| e.to_string())?;
        if let Ok(mut e) = z.by_name("docProps/custom.xml") {
            let mut s = String::new(); use std::io::Read; e.read_to_string(&mut s).map_err(|x| x.to_string())?;
            let re = regex::Regex::new(r#"name="(MSIP_Label_[^"]+)"[^>]*>\s*<vt:[a-z0-9]+>([^<]*)<"#).unwrap();
            for c in re.captures_iter(&s) { meta.insert(c[1].to_string(), c[2].to_string()); }
        }
    }
    Ok(file_label_from_meta(&meta).map(|f| vec![Label { id: f.id, name: f.name, tooltip: String::new(), color: String::new(), parent: String::new(), order: 0, source: src }]).unwrap_or_default())
}
