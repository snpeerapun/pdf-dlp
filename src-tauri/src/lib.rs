pub mod labels;
pub mod pdfops;
mod winshell;

use pdfops::Op;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use tauri::{Emitter, Manager};

#[derive(Serialize, Clone, Debug)]
pub struct OpenRequest { pub action: String, pub paths: Vec<String> }

#[derive(Default)]
pub struct Pending(pub Mutex<Vec<OpenRequest>>);

fn parse_args(args: &[String]) -> Option<OpenRequest> {
    let mut action = "open".to_string();
    let mut paths = vec![];
    let mut it = args.iter();
    while let Some(a) = it.next() {
        if a == "--action" { if let Some(v) = it.next() { action = v.clone(); } }
        else if a.starts_with("--") { continue; }
        else if a.to_lowercase().ends_with(".pdf") && Path::new(a).exists() { paths.push(a.clone()); }
    }
    if paths.is_empty() { None } else { Some(OpenRequest { action, paths }) }
}

fn temp_out(stem: &str) -> PathBuf {
    let dir = std::env::temp_dir().join("pdfdlp");
    let _ = std::fs::create_dir_all(&dir);
    let tag = chrono::Local::now().format("%Y%m%d-%H%M%S%.3f");
    dir.join(format!("{}-{}.pdf", sanitize(stem), tag))
}

fn sanitize(s: &str) -> String {
    let s: String = s.chars().map(|c| if c.is_alphanumeric() || c == '-' || c == '_' || c == '.' || c == ' ' || ('\u{0E00}'..='\u{0E7F}').contains(&c) { c } else { '_' }).collect();
    let s = s.trim().trim_end_matches(".pdf").trim_end_matches(".PDF").to_string();
    if s.is_empty() { "document".into() } else { s }
}

#[tauri::command]
fn read_file(path: String) -> Result<tauri::ipc::Response, String> {
    std::fs::read(&path).map(tauri::ipc::Response::new).map_err(|e| format!("อ่านไฟล์ไม่ได้: {e}"))
}

#[tauri::command]
fn write_file_b64(path: String, data_base64: String) -> Result<(), String> {
    use base64::Engine;
    let s = data_base64.split(',').next_back().unwrap_or("");
    let bytes = base64::engine::general_purpose::STANDARD.decode(s.trim()).map_err(|e| e.to_string())?;
    if let Some(p) = Path::new(&path).parent() { std::fs::create_dir_all(p).map_err(|e| e.to_string())?; }
    std::fs::write(&path, bytes).map_err(|e| format!("เขียนไฟล์ไม่ได้: {e}"))
}

#[tauri::command]
fn copy_file(src: String, dest: String) -> Result<(), String> {
    if let Some(p) = Path::new(&dest).parent() { std::fs::create_dir_all(p).map_err(|e| e.to_string())?; }
    std::fs::copy(&src, &dest).map(|_| ()).map_err(|e| format!("บันทึกไฟล์ไม่ได้: {e}"))
}

#[derive(Serialize)]
struct InfoOut { #[serde(flatten)] info: pdfops::PdfInfo, msip: Option<labels::FileLabel> }

#[tauri::command]
fn pdf_info(path: String, password: Option<String>) -> Result<InfoOut, String> {
    let info = pdfops::info(Path::new(&path), password.as_deref().unwrap_or(""))?;
    let msip = labels::file_label_from_meta(&info.metadata);
    Ok(InfoOut { info, msip })
}

#[derive(Serialize)]
struct ApplyOut { path: String, pages: u32 }

/// Apply ops to `path`, write to a temp file (or `out` if given). Original file is never modified.
#[tauri::command]
fn apply_ops(path: String, password: Option<String>, ops: Vec<Op>, out: Option<String>) -> Result<ApplyOut, String> {
    let p = Path::new(&path);
    let mut doc = pdfops::load(p, password.as_deref().unwrap_or(""))?;
    pdfops::apply(&mut doc, &ops)?;
    let dest = out.map(PathBuf::from).unwrap_or_else(|| temp_out(p.file_stem().and_then(|s| s.to_str()).unwrap_or("document")));
    pdfops::save(&mut doc, &dest)?;
    Ok(ApplyOut { path: dest.display().to_string(), pages: doc.get_pages().len() as u32 })
}

#[derive(Deserialize)]
struct MergeInput { path: String, #[serde(default)] password: String, #[serde(default)] pages: Vec<u32> }

#[tauri::command]
fn merge_pdfs(inputs: Vec<MergeInput>, out: Option<String>) -> Result<ApplyOut, String> {
    let v: Vec<(PathBuf, String, Vec<u32>)> = inputs.into_iter().map(|i| (PathBuf::from(i.path), i.password, i.pages)).collect();
    let mut doc = pdfops::merge(&v)?;
    let dest = out.map(PathBuf::from).unwrap_or_else(|| temp_out("merged"));
    pdfops::save(&mut doc, &dest)?;
    Ok(ApplyOut { path: dest.display().to_string(), pages: doc.get_pages().len() as u32 })
}

#[derive(Serialize)]
struct SplitOut { files: Vec<String> }

/// mode "ranges": spec like "1-3,4-6" (each comma group -> one file); "each": one file per page; "every": chunks of n pages.
#[tauri::command]
fn split_pdf(path: String, password: Option<String>, mode: String, spec: Option<String>, every: Option<u32>, out_dir: String) -> Result<SplitOut, String> {
    let p = Path::new(&path);
    let pw = password.unwrap_or_default();
    let doc = pdfops::load(p, &pw)?;
    let total = doc.get_pages().len() as u32;
    let stem = sanitize(p.file_stem().and_then(|s| s.to_str()).unwrap_or("document"));
    let groups: Vec<Vec<u32>> = match mode.as_str() {
        "each" => (1..=total).map(|i| vec![i]).collect(),
        "every" => { let n = every.unwrap_or(1).max(1); (1..=total).collect::<Vec<u32>>().chunks(n as usize).map(|c| c.to_vec()).collect() }
        _ => spec.unwrap_or_default().split(',').map(str::trim).filter(|s| !s.is_empty()).map(|s| pdfops::parse_ranges(s, total)).collect::<Result<Vec<_>, _>>()?,
    };
    if groups.is_empty() { return Err("ไม่มีช่วงหน้าให้ตัด".into()); }
    let mut files = vec![];
    let width = groups.len().to_string().len().max(2);
    for (i, g) in groups.iter().enumerate() {
        let mut d = pdfops::load(p, &pw)?;
        pdfops::apply(&mut d, &[Op::Extract { pages: g.clone() }])?;
        let name = if g.len() == 1 { format!("{stem}-p{:0w$}.pdf", g[0], w = width) } else { format!("{stem}-part{:0w$}-p{}-{}.pdf", i + 1, g[0], g[g.len() - 1], w = width) };
        let dest = Path::new(&out_dir).join(name);
        pdfops::save(&mut d, &dest)?;
        files.push(dest.display().to_string());
    }
    Ok(SplitOut { files })
}

#[tauri::command]
fn images_to_pdf(paths: Vec<String>, fit_a4: bool, out: Option<String>) -> Result<ApplyOut, String> {
    let v: Vec<PathBuf> = paths.into_iter().map(PathBuf::from).collect();
    let mut doc = pdfops::images_to_pdf(&v, fit_a4)?;
    let dest = out.map(PathBuf::from).unwrap_or_else(|| temp_out("images"));
    pdfops::save(&mut doc, &dest)?;
    Ok(ApplyOut { path: dest.display().to_string(), pages: doc.get_pages().len() as u32 })
}

#[tauri::command]
fn file_size(path: String) -> Result<u64, String> { std::fs::metadata(&path).map(|m| m.len()).map_err(|e| e.to_string()) }

#[tauri::command]
fn parse_page_ranges(spec: String, total: u32) -> Result<Vec<u32>, String> { pdfops::parse_ranges(&spec, total) }

// ---------- labels ----------
#[tauri::command]
fn discover_labels() -> labels::Discovery { labels::discover_local() }

#[tauri::command]
fn labels_from_file(path: String, password: Option<String>) -> Result<Vec<labels::Label>, String> {
    let p = Path::new(&path);
    if path.to_lowercase().ends_with(".pdf") {
        let info = pdfops::info(p, password.as_deref().unwrap_or(""))?;
        labels::labels_from_file(p, Some(&info.metadata))
    } else { labels::labels_from_file(p, None) }
}

#[tauri::command]
fn msip_metadata(label: labels::Label, site_id: String, content_bits: u32, owner: String, previous: BTreeMap<String, String>) -> BTreeMap<String, String> {
    let mut m = labels::msip_clear_keys(&previous);
    m.extend(labels::msip_metadata(&label, &site_id, content_bits, &owner));
    m
}

#[tauri::command]
fn new_guid() -> String { labels::new_guid() }

// ---------- OS integration ----------
#[tauri::command]
fn shell_register(items: Vec<winshell::MenuItem>) -> Result<(), String> { winshell::register(&items) }
#[tauri::command]
fn shell_unregister() -> Result<(), String> { winshell::unregister() }
#[tauri::command]
fn shell_status() -> serde_json::Value { serde_json::json!({ "supported": cfg!(windows), "registered": winshell::is_registered() }) }

#[tauri::command]
fn take_pending(state: tauri::State<Pending>) -> Vec<OpenRequest> { std::mem::take(&mut *state.0.lock().unwrap()) }

/// Dev-only self-test trigger: the UI polls this; a JSON file at /tmp/pdfdlp-selftest.json starts a scripted run.
#[tauri::command]
fn dev_selftest_trigger() -> Option<String> {
    if !cfg!(debug_assertions) { return None; }
    let p = Path::new("/tmp/pdfdlp-selftest.json");
    let s = std::fs::read_to_string(p).ok()?;
    let _ = std::fs::remove_file(p);
    Some(s)
}

#[tauri::command]
fn temp_dir() -> String { std::env::temp_dir().join("pdfdlp").display().to_string() }

#[tauri::command]
fn home_dir() -> String { std::env::var("HOME").or_else(|_| std::env::var("USERPROFILE")).unwrap_or_default() }

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let initial: Vec<String> = std::env::args().skip(1).collect();
    let pending = Pending(Mutex::new(parse_args(&initial).into_iter().collect()));
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, argv, _cwd| {
            if let Some(req) = parse_args(&argv[1..]) {
                let _ = app.emit("open-files", req.clone());
                if let Some(s) = app.try_state::<Pending>() { s.0.lock().unwrap().push(req); }
            }
            if let Some(w) = app.get_webview_window("main") { let _ = w.set_focus(); }
        }))
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(pending)
        .setup(|app| {
            if cfg!(debug_assertions) {
                app.handle().plugin(tauri_plugin_log::Builder::default().level(log::LevelFilter::Info).build())?;
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            read_file, write_file_b64, copy_file, pdf_info, apply_ops, merge_pdfs, split_pdf, parse_page_ranges, images_to_pdf, file_size,
            discover_labels, labels_from_file, msip_metadata, new_guid,
            shell_register, shell_unregister, shell_status, take_pending, temp_dir, home_dir, dev_selftest_trigger
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app, event| {
            #[cfg(target_os = "macos")]
            if let tauri::RunEvent::Opened { urls } = event {
                let paths: Vec<String> = urls.iter().filter_map(|u| u.to_file_path().ok()).map(|p| p.display().to_string()).collect();
                if !paths.is_empty() {
                    let req = OpenRequest { action: "open".into(), paths };
                    let _ = app.emit("open-files", req.clone());
                    if let Some(s) = app.try_state::<Pending>() { s.0.lock().unwrap().push(req); }
                }
            }
            #[cfg(not(target_os = "macos"))]
            let _ = (app, event);
        });
}
