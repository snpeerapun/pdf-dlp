//! PDF DLP core: PDF operations (lopdf), MSIP-compatible labels, Windows shell integration.
//! Shared by the Tauri app (src-tauri) and the C ABI (core-ffi) used by the Qt/C++ app.
pub mod docx;
pub mod labels;
pub mod pdfops;
pub mod winshell;

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Serialize)]
pub struct InfoOut { #[serde(flatten)] pub info: pdfops::PdfInfo, pub msip: Option<labels::FileLabel> }
#[derive(Serialize)]
pub struct ApplyOut { pub path: String, pub pages: u32 }
#[derive(Deserialize)]
pub struct MergeInput { pub path: String, #[serde(default)] pub password: String, #[serde(default)] pub pages: Vec<u32> }

pub fn sanitize_name(s: &str) -> String {
    let s: String = s.chars().map(|c| if c.is_alphanumeric() || c == '-' || c == '_' || c == '.' || c == ' ' || ('\u{0E00}'..='\u{0E7F}').contains(&c) { c } else { '_' }).collect();
    let s = s.trim().trim_end_matches(".pdf").trim_end_matches(".PDF").to_string();
    if s.is_empty() { "document".into() } else { s }
}

pub fn temp_out(stem: &str) -> PathBuf {
    let dir = std::env::temp_dir().join("pdfdlp");
    let _ = std::fs::create_dir_all(&dir);
    let tag = chrono::Local::now().format("%Y%m%d-%H%M%S%.3f");
    dir.join(format!("{}-{}.pdf", sanitize_name(stem), tag))
}

pub fn info(path: &str, password: &str) -> Result<InfoOut, String> {
    let info = pdfops::info(Path::new(path), password)?;
    let msip = labels::file_label_from_meta(&info.metadata);
    Ok(InfoOut { info, msip })
}

pub fn apply(path: &str, password: &str, ops: &[pdfops::Op], out: Option<&str>) -> Result<ApplyOut, String> {
    let p = Path::new(path);
    let mut doc = pdfops::load(p, password)?;
    pdfops::apply(&mut doc, ops)?;
    let dest = out.map(PathBuf::from).unwrap_or_else(|| temp_out(p.file_stem().and_then(|s| s.to_str()).unwrap_or("document")));
    pdfops::save(&mut doc, &dest)?;
    Ok(ApplyOut { path: dest.display().to_string(), pages: doc.get_pages().len() as u32 })
}

pub fn merge(inputs: Vec<MergeInput>, out: Option<&str>) -> Result<ApplyOut, String> {
    let v: Vec<(PathBuf, String, Vec<u32>)> = inputs.into_iter().map(|i| (PathBuf::from(i.path), i.password, i.pages)).collect();
    let mut doc = pdfops::merge(&v)?;
    let dest = out.map(PathBuf::from).unwrap_or_else(|| temp_out("merged"));
    pdfops::save(&mut doc, &dest)?;
    Ok(ApplyOut { path: dest.display().to_string(), pages: doc.get_pages().len() as u32 })
}

/// mode "ranges" (spec "1-3,4-6"), "each", "every" (n pages per file)
pub fn split(path: &str, password: &str, mode: &str, spec: &str, every: u32, out_dir: &str) -> Result<Vec<String>, String> {
    let p = Path::new(path);
    let doc = pdfops::load(p, password)?;
    let total = doc.get_pages().len() as u32;
    let stem = sanitize_name(p.file_stem().and_then(|s| s.to_str()).unwrap_or("document"));
    let groups: Vec<Vec<u32>> = match mode {
        "each" => (1..=total).map(|i| vec![i]).collect(),
        "every" => { let n = every.max(1); (1..=total).collect::<Vec<u32>>().chunks(n as usize).map(|c| c.to_vec()).collect() }
        _ => spec.split(',').map(str::trim).filter(|s| !s.is_empty()).map(|s| pdfops::parse_ranges(s, total)).collect::<Result<Vec<_>, _>>()?,
    };
    if groups.is_empty() { return Err("ไม่มีช่วงหน้าให้ตัด".into()); }
    let mut files = vec![];
    let width = groups.len().to_string().len().max(2);
    for (i, g) in groups.iter().enumerate() {
        let mut d = pdfops::load(p, password)?;
        pdfops::apply(&mut d, &[pdfops::Op::Extract { pages: g.clone() }])?;
        let name = if g.len() == 1 { format!("{stem}-p{:0w$}.pdf", g[0], w = width) } else { format!("{stem}-part{:0w$}-p{}-{}.pdf", i + 1, g[0], g[g.len() - 1], w = width) };
        let dest = Path::new(out_dir).join(name);
        pdfops::save(&mut d, &dest)?;
        files.push(dest.display().to_string());
    }
    Ok(files)
}

pub fn images_to_pdf(paths: Vec<String>, fit_a4: bool, out: Option<&str>) -> Result<ApplyOut, String> {
    let v: Vec<PathBuf> = paths.into_iter().map(PathBuf::from).collect();
    let mut doc = pdfops::images_to_pdf(&v, fit_a4)?;
    let dest = out.map(PathBuf::from).unwrap_or_else(|| temp_out("images"));
    pdfops::save(&mut doc, &dest)?;
    Ok(ApplyOut { path: dest.display().to_string(), pages: doc.get_pages().len() as u32 })
}

pub fn labels_from_file(path: &str, password: &str) -> Result<Vec<labels::Label>, String> {
    let p = Path::new(path);
    if path.to_lowercase().ends_with(".pdf") {
        let info = pdfops::info(p, password)?;
        labels::labels_from_file(p, Some(&info.metadata))
    } else { labels::labels_from_file(p, None) }
}
