//! C ABI over pdfcore. Every function returns a malloc'd UTF-8 JSON string: {"ok":true,...} or {"ok":false,"error":"..."}.
//! Free results with pdfcore_free. Inputs are UTF-8 C strings (NULL allowed where documented).
use std::ffi::{c_char, CStr, CString};

fn arg(p: *const c_char) -> String { if p.is_null() { String::new() } else { unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned() } }
fn opt(p: *const c_char) -> Option<String> { if p.is_null() { None } else { Some(arg(p)) } }
fn out(r: Result<serde_json::Value, String>) -> *mut c_char {
    let v = match r { Ok(mut v) => { if let serde_json::Value::Object(ref mut m) = v { m.insert("ok".into(), true.into()); v } else { serde_json::json!({ "ok": true, "value": v }) } }, Err(e) => serde_json::json!({ "ok": false, "error": e }) };
    CString::new(v.to_string()).unwrap_or_default().into_raw()
}
fn j<T: serde::Serialize>(t: T) -> Result<serde_json::Value, String> { serde_json::to_value(t).map_err(|e| e.to_string()) }

#[unsafe(no_mangle)] pub extern "C" fn pdfcore_free(p: *mut c_char) { if !p.is_null() { unsafe { drop(CString::from_raw(p)) } } }

#[unsafe(no_mangle)] pub extern "C" fn pdfcore_info(path: *const c_char, password: *const c_char) -> *mut c_char {
    out(pdfcore::info(&arg(path), &arg(password)).and_then(j))
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_apply(path: *const c_char, password: *const c_char, ops_json: *const c_char, out_path: *const c_char) -> *mut c_char {
    out((|| { let ops: Vec<pdfcore::pdfops::Op> = serde_json::from_str(&arg(ops_json)).map_err(|e| format!("ops json: {e}"))?; pdfcore::apply(&arg(path), &arg(password), &ops, opt(out_path).as_deref()).and_then(j) })())
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_merge(inputs_json: *const c_char, out_path: *const c_char) -> *mut c_char {
    out((|| { let inputs: Vec<pdfcore::MergeInput> = serde_json::from_str(&arg(inputs_json)).map_err(|e| format!("inputs json: {e}"))?; pdfcore::merge(inputs, opt(out_path).as_deref()).and_then(j) })())
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_split(path: *const c_char, password: *const c_char, mode: *const c_char, spec: *const c_char, every: u32, out_dir: *const c_char) -> *mut c_char {
    out(pdfcore::split(&arg(path), &arg(password), &arg(mode), &arg(spec), every, &arg(out_dir)).map(|files| serde_json::json!({ "files": files })))
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_images_to_pdf(paths_json: *const c_char, fit_a4: i32, out_path: *const c_char) -> *mut c_char {
    out((|| { let paths: Vec<String> = serde_json::from_str(&arg(paths_json)).map_err(|e| e.to_string())?; pdfcore::images_to_pdf(paths, fit_a4 != 0, opt(out_path).as_deref()).and_then(j) })())
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_parse_ranges(spec: *const c_char, total: u32) -> *mut c_char {
    out(pdfcore::pdfops::parse_ranges(&arg(spec), total).map(|p| serde_json::json!({ "pages": p })))
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_discover_labels() -> *mut c_char { out(j(pdfcore::labels::discover_local())) }
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_labels_from_file(path: *const c_char, password: *const c_char) -> *mut c_char {
    out(pdfcore::labels_from_file(&arg(path), &arg(password)).map(|l| serde_json::json!({ "labels": l })))
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_msip_metadata(label_json: *const c_char, site_id: *const c_char, content_bits: u32, owner: *const c_char, previous_json: *const c_char) -> *mut c_char {
    out((|| {
        let label: pdfcore::labels::Label = serde_json::from_str(&arg(label_json)).map_err(|e| e.to_string())?;
        let prev: std::collections::BTreeMap<String, String> = if previous_json.is_null() { Default::default() } else { serde_json::from_str(&arg(previous_json)).map_err(|e| e.to_string())? };
        let mut m = pdfcore::labels::msip_clear_keys(&prev);
        m.extend(pdfcore::labels::msip_metadata(&label, &arg(site_id), content_bits, &arg(owner)));
        Ok(serde_json::json!({ "metadata": m }))
    })())
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_write_docx(doc_json: *const c_char, out_path: *const c_char) -> *mut c_char {
    out((|| { let d: pdfcore::docx::Doc = serde_json::from_str(&arg(doc_json)).map_err(|e| format!("docx json: {e}"))?; pdfcore::docx::write(&d, std::path::Path::new(&arg(out_path)))?; Ok(serde_json::json!({ "path": arg(out_path) })) })())
}
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_new_guid() -> *mut c_char { out(Ok(serde_json::json!({ "guid": pdfcore::labels::new_guid() }))) }
/// action: "register" (items_json = [{"action","label"}]), "unregister", "status"
#[unsafe(no_mangle)] pub extern "C" fn pdfcore_shell(action: *const c_char, items_json: *const c_char) -> *mut c_char {
    out((|| match arg(action).as_str() {
        "register" => { let items: Vec<pdfcore::winshell::MenuItem> = serde_json::from_str(&arg(items_json)).map_err(|e| e.to_string())?; pdfcore::winshell::register(&items).map(|_| serde_json::json!({})) }
        "unregister" => pdfcore::winshell::unregister().map(|_| serde_json::json!({})),
        _ => Ok(serde_json::json!({ "supported": cfg!(windows), "registered": pdfcore::winshell::is_registered() })),
    })())
}
