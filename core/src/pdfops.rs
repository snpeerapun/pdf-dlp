//! PDF operations on top of lopdf: assemble (subset/reorder/merge), rotate,
//! encrypt/decrypt (AES-256), stamp/watermark with alpha, raster redaction, metadata.
use base64::Engine;
use lopdf::encryption::crypt_filters::{Aes256CryptFilter, CryptFilter};
use lopdf::{dictionary, Dictionary, Document, EncryptionState, EncryptionVersion, Object, ObjectId, Permissions, Stream, StringFormat};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;

pub type R<T> = Result<T, String>;

fn e<T: std::fmt::Display>(x: T) -> String { x.to_string() }

#[derive(Deserialize, Debug, Clone)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum Op {
    /// pages: 1-based, empty = all. angle: multiple of 90
    Rotate { pages: Vec<u32>, angle: i64 },
    Delete { pages: Vec<u32> },
    /// full new order of 1-based page numbers (a page may be omitted = deleted)
    Reorder { order: Vec<u32> },
    Extract { pages: Vec<u32> },
    Encrypt {
        user_password: String,
        owner_password: String,
        allow_print: bool,
        allow_copy: bool,
        allow_modify: bool,
        allow_annotate: bool,
    },
    Decrypt {},
    /// PNG with alpha. x,y = top-left in fraction of displayed page (0..1, y from top),
    /// w = width fraction of displayed page. anchor "topleft" | "center" (then x,y = center).
    Stamp { pages: Vec<u32>, png_base64: String, x: f64, y: f64, w: f64, opacity: f64, rotate_deg: f64, anchor: String },
    /// Replace page with a flat JPEG (true redaction: text/vectors under the boxes are gone).
    Redact { page: u32, jpeg_base64: String, width_pt: f64, height_pt: f64 },
    Metadata { title: Option<String>, subject: Option<String>, keywords: Option<String>, classification: Option<String>, #[serde(default)] extra: BTreeMap<String, String> },
    /// Strip every visual mark this app previously stamped (header/footer badge, watermark).
    RemoveMarks {},
    /// Crop: insets as fraction of the displayed page (0..0.45 each side, viewer orientation).
    Crop { pages: Vec<u32>, left: f64, top: f64, right: f64, bottom: f64 },
    /// Remove hidden data: metadata, XMP, annotations/forms, JavaScript, embedded files, open actions.
    Sanitize { #[serde(default)] keep_annotations: bool },
}

pub const MARK_BEGIN: &[u8] = b"\n%PDFDLP-MARK-BEGIN\n";
pub const MARK_END: &[u8] = b"\n%PDFDLP-MARK-END\n";

#[derive(Serialize)]
pub struct PdfInfo {
    pub pages: u32,
    pub encrypted: bool,
    pub needs_password: bool,
    pub version: String,
    pub title: Option<String>,
    pub classification: Option<String>,
    pub has_marks: bool,
    pub metadata: BTreeMap<String, String>,
}

/// Is the file encrypted? (cheap: only the trailer is parsed for encrypted files)
pub fn is_encrypted_file(path: &Path) -> bool {
    Document::load(path).map(|d| d.is_encrypted()).unwrap_or(false)
}

/// Load (and transparently decrypt) a document. Errors with "wrong_password" when the password does not match.
pub fn load(path: &Path, password: &str) -> R<Document> {
    match Document::load_with_password(path, password) {
        Ok(doc) => Ok(doc),
        Err(x) => {
            if is_encrypted_file(path) { Err("wrong_password".into()) } else { Err(format!("โหลดไฟล์ไม่ได้: {x}")) }
        }
    }
}

pub fn info(path: &Path, password: &str) -> R<PdfInfo> {
    let encrypted = is_encrypted_file(path);
    let doc = match load(path, password) {
        Ok(d) => d,
        Err(e) if e == "wrong_password" => {
            return Ok(PdfInfo { pages: 0, encrypted, needs_password: true, version: String::new(), title: None, classification: None, has_marks: false, metadata: BTreeMap::new() });
        }
        Err(e) => return Err(e),
    };
    let (title, classification, metadata) = read_info(&doc);
    Ok(PdfInfo { pages: doc.get_pages().len() as u32, encrypted, needs_password: false, version: doc.version.clone(), title, classification, has_marks: has_marks(&doc), metadata })
}

fn read_info(doc: &Document) -> (Option<String>, Option<String>, BTreeMap<String, String>) {
    let info = doc.trailer.get(b"Info").ok().and_then(|o| match o {
        Object::Reference(id) => doc.get_object(*id).ok().and_then(|x| x.as_dict().ok()),
        Object::Dictionary(d) => Some(d),
        _ => None,
    });
    let mut meta = BTreeMap::new();
    if let Some(d) = info {
        for (k, v) in d.iter() {
            if let Ok(s) = v.as_str() { meta.insert(String::from_utf8_lossy(k).to_string(), decode_text_string(s)); }
        }
    }
    let title = meta.get("Title").cloned();
    // Our own key first, then Microsoft Information Protection label name, then generic keys.
    let classification = meta.get("Classification").cloned()
        .or_else(|| meta.iter().find(|(k, _)| k.starts_with("MSIP_Label_") && k.ends_with("_Name")).map(|(_, v)| v.clone()))
        .or_else(|| meta.get("Sensitivity").cloned());
    (title, classification, meta)
}

fn has_marks(doc: &Document) -> bool {
    ordered_pages(doc).iter().any(|p| find(&doc.get_page_content(*p), MARK_BEGIN, 0).is_some())
}

fn decode_text_string(b: &[u8]) -> String {
    if b.len() >= 2 && b[0] == 0xFE && b[1] == 0xFF {
        let u: Vec<u16> = b[2..].chunks(2).filter(|c| c.len() == 2).map(|c| u16::from_be_bytes([c[0], c[1]])).collect();
        String::from_utf16_lossy(&u)
    } else {
        String::from_utf8_lossy(b).to_string()
    }
}

fn text_string(s: &str) -> Object {
    if s.is_ascii() {
        Object::string_literal(s)
    } else {
        let mut b = vec![0xFE, 0xFF];
        for u in s.encode_utf16() { b.extend_from_slice(&u.to_be_bytes()); }
        Object::String(b, StringFormat::Hexadecimal)
    }
}

fn num(o: &Object) -> Option<f64> {
    match o { Object::Integer(i) => Some(*i as f64), Object::Real(r) => Some(*r as f64), _ => None }
}

fn ordered_pages(doc: &Document) -> Vec<ObjectId> { doc.get_pages().into_values().collect() }

/// Copy inheritable attributes (Resources, MediaBox, CropBox, Rotate) from the Parent chain onto the page.
fn inherit_attrs(doc: &mut Document, page_id: ObjectId) {
    const KEYS: [&[u8]; 4] = [b"Resources", b"MediaBox", b"CropBox", b"Rotate"];
    let mut found: Vec<(Vec<u8>, Object)> = vec![];
    for key in KEYS {
        let has = doc.get_object(page_id).ok().and_then(|o| o.as_dict().ok()).map(|d| d.has(key)).unwrap_or(false);
        if has { continue; }
        let mut cur = doc.get_object(page_id).ok().and_then(|o| o.as_dict().ok()).and_then(|d| d.get(b"Parent").ok()).and_then(|p| p.as_reference().ok());
        let mut hops = 0;
        while let Some(pid) = cur {
            hops += 1; if hops > 64 { break; }
            let Some(pd) = doc.get_object(pid).ok().and_then(|o| o.as_dict().ok()) else { break };
            if let Ok(v) = pd.get(key) { found.push((key.to_vec(), v.clone())); break; }
            cur = pd.get(b"Parent").ok().and_then(|p| p.as_reference().ok());
        }
    }
    if let Some(d) = doc.get_object_mut(page_id).ok().and_then(|o| o.as_dict_mut().ok()) {
        for (k, v) in found { d.set(k, v); }
    }
}

fn catalog_id(doc: &Document) -> R<ObjectId> {
    doc.trailer.get(b"Root").and_then(Object::as_reference).map_err(|_| "ไม่พบ Root ของเอกสาร".to_string())
}

/// Rebuild the page tree with exactly `ordered` pages (any order, duplicates not allowed).
fn rebuild_pages(doc: &mut Document, ordered: Vec<ObjectId>) -> R<()> {
    if ordered.is_empty() { return Err("ต้องเหลืออย่างน้อย 1 หน้า".into()); }
    let all = ordered_pages(doc);
    for p in &all { inherit_attrs(doc, *p); }
    let cat = catalog_id(doc)?;
    let root = doc.get_object(cat).and_then(Object::as_dict).map_err(e)?.get(b"Pages").and_then(Object::as_reference).map_err(e)?;
    for p in &ordered {
        let d = doc.get_object_mut(*p).and_then(Object::as_dict_mut).map_err(e)?;
        d.set("Parent", Object::Reference(root));
    }
    let count = ordered.len() as i64;
    let kids: Vec<Object> = ordered.into_iter().map(Object::Reference).collect();
    doc.objects.insert(root, Object::Dictionary(dictionary! { "Type" => "Pages", "Kids" => kids, "Count" => count }));
    // Drop structures that point at pages which may no longer exist.
    if let Ok(c) = doc.get_object_mut(cat).and_then(Object::as_dict_mut) {
        for k in ["Outlines", "PageLabels", "OpenAction", "StructTreeRoot", "PageMode"] { c.remove(k.as_bytes()); }
    }
    doc.prune_objects();
    Ok(())
}

fn select(all: &[ObjectId], pages: &[u32]) -> R<Vec<ObjectId>> {
    let mut out = vec![];
    for &p in pages {
        let i = p.checked_sub(1).ok_or("หมายเลขหน้าต้องเริ่มที่ 1")? as usize;
        out.push(*all.get(i).ok_or(format!("ไม่มีหน้า {p}"))?);
    }
    Ok(out)
}

fn page_set(doc: &Document, pages: &[u32]) -> Vec<ObjectId> {
    let all = ordered_pages(doc);
    if pages.is_empty() { all } else { pages.iter().filter_map(|p| all.get((*p as usize).saturating_sub(1)).copied()).collect() }
}

pub fn rotate(doc: &mut Document, pages: &[u32], angle: i64) -> R<()> {
    if angle % 90 != 0 { return Err("มุมต้องเป็น 90/180/270".into()); }
    for pid in page_set(doc, pages) {
        inherit_attrs(doc, pid);
        let d = doc.get_object_mut(pid).and_then(Object::as_dict_mut).map_err(e)?;
        let cur = d.get(b"Rotate").and_then(Object::as_i64).unwrap_or(0);
        d.set("Rotate", ((cur + angle) % 360 + 360) % 360);
    }
    Ok(())
}

fn permissions(print: bool, copy: bool, modify: bool, annotate: bool) -> Permissions {
    let mut p = Permissions::COPYABLE_FOR_ACCESSIBILITY;
    if print { p |= Permissions::PRINTABLE | Permissions::PRINTABLE_IN_HIGH_QUALITY; }
    if copy { p |= Permissions::COPYABLE; }
    if modify { p |= Permissions::MODIFIABLE | Permissions::ASSEMBLABLE; }
    if annotate { p |= Permissions::ANNOTABLE | Permissions::FILLABLE; }
    p
}

pub fn encrypt(doc: &mut Document, user: &str, owner: &str, perms: Permissions) -> R<()> {
    use rand::RngExt as _;
    if doc.is_encrypted() { return Err("เอกสารถูกเข้ารหัสอยู่แล้ว".into()); }
    let owner = if owner.is_empty() { user } else { owner };
    let mut key = [0u8; 32];
    rand::rng().fill(&mut key);
    let cf: Arc<dyn CryptFilter> = Arc::new(Aes256CryptFilter);
    let state = EncryptionState::try_from(EncryptionVersion::V5 {
        encrypt_metadata: true,
        crypt_filters: BTreeMap::from([(b"StdCF".to_vec(), cf)]),
        file_encryption_key: &key,
        stream_filter: b"StdCF".to_vec(),
        string_filter: b"StdCF".to_vec(),
        owner_password: owner,
        user_password: user,
        permissions: perms,
    }).map_err(e)?;
    // PDF 2.0 security handler (R6) — bump header version so readers accept it.
    doc.version = "1.7".into();
    doc.encrypt(&state).map_err(e)?;
    // lopdf omits /Length entries; poppler, pdf.js and lopdf's own reader then derive a wrong key length.
    let enc_id = doc.trailer.get(b"Encrypt").and_then(Object::as_reference).map_err(e)?;
    if let Ok(d) = doc.get_object_mut(enc_id).and_then(Object::as_dict_mut) {
        d.set("Length", 256);
        if let Ok(cf) = d.get_mut(b"CF").and_then(Object::as_dict_mut) {
            if let Ok(std) = cf.get_mut(b"StdCF").and_then(Object::as_dict_mut) { std.set("Length", 32); std.set("AuthEvent", "DocOpen"); }
        }
    }
    Ok(())
}

/// Image XObject with alpha (SMask) from PNG bytes. Returns (id, width, height).
fn add_png_with_alpha(doc: &mut Document, png: &[u8]) -> R<(ObjectId, u32, u32)> {
    let img = image::load_from_memory(png).map_err(e)?.to_rgba8();
    let (w, h) = img.dimensions();
    let mut rgb = Vec::with_capacity((w * h * 3) as usize);
    let mut alpha = Vec::with_capacity((w * h) as usize);
    for px in img.pixels() { rgb.extend_from_slice(&px.0[..3]); alpha.push(px.0[3]); }
    let mut smask = Stream::new(dictionary! { "Type" => "XObject", "Subtype" => "Image", "Width" => w as i64, "Height" => h as i64, "ColorSpace" => "DeviceGray", "BitsPerComponent" => 8 }, alpha);
    let _ = smask.compress();
    let smask_id = doc.add_object(smask);
    let mut im = Stream::new(dictionary! { "Type" => "XObject", "Subtype" => "Image", "Width" => w as i64, "Height" => h as i64, "ColorSpace" => "DeviceRGB", "BitsPerComponent" => 8, "SMask" => Object::Reference(smask_id) }, rgb);
    let _ = im.compress();
    Ok((doc.add_object(im), w, h))
}

struct Geo { x0: f64, y0: f64, x1: f64, y1: f64, rot: i64 }

fn geometry(doc: &Document, pid: ObjectId) -> R<Geo> {
    let d = doc.get_object(pid).and_then(Object::as_dict).map_err(e)?;
    let bx = d.get(b"CropBox").or_else(|_| d.get(b"MediaBox")).map_err(|_| "ไม่พบขนาดหน้า".to_string())?;
    let arr = bx.as_array().map_err(e)?;
    let mut v = [0f64, 0.0, 612.0, 792.0];
    for (i, o) in arr.iter().take(4).enumerate() { let o = if let Object::Reference(r) = o { doc.get_object(*r).map_err(e)? } else { o }; v[i] = num(o).unwrap_or(v[i]); }
    let (x0, x1) = (v[0].min(v[2]), v[0].max(v[2]));
    let (y0, y1) = (v[1].min(v[3]), v[1].max(v[3]));
    let rot = ((d.get(b"Rotate").and_then(Object::as_i64).unwrap_or(0) % 360) + 360) % 360;
    Ok(Geo { x0, y0, x1, y1, rot })
}

fn f(x: f64) -> String { format!("{:.4}", x) }

/// Build "viewer space (y down, rotation applied) -> page space" matrix.
fn viewer_to_page(g: &Geo) -> [f64; 6] {
    match g.rot {
        90 => [0.0, 1.0, 1.0, 0.0, g.x0, g.y0],
        180 => [-1.0, 0.0, 0.0, 1.0, g.x1, g.y0],
        270 => [0.0, -1.0, -1.0, 0.0, g.x1, g.y1],
        _ => [1.0, 0.0, 0.0, -1.0, g.x0, g.y1],
    }
}

pub fn stamp(doc: &mut Document, pages: &[u32], png: &[u8], x: f64, y: f64, w: f64, opacity: f64, rotate_deg: f64, anchor: &str) -> R<()> {
    let (img_id, iw, ih) = add_png_with_alpha(doc, png)?;
    let gs_id = doc.add_object(dictionary! { "Type" => "ExtGState", "ca" => opacity.clamp(0.0, 1.0), "CA" => opacity.clamp(0.0, 1.0) });
    let tag = format!("{:x}", chrono::Utc::now().timestamp_millis() % 0xFFFFFF);
    let im_name = format!("DlpIm{tag}");
    let gs_name = format!("DlpGs{tag}");
    for pid in page_set(doc, pages) {
        inherit_attrs(doc, pid);
        let g = geometry(doc, pid)?;
        let (pw, ph) = (g.x1 - g.x0, g.y1 - g.y0);
        let (vw_page, vh_page) = if g.rot == 90 || g.rot == 270 { (ph, pw) } else { (pw, ph) };
        let sw = w * vw_page;
        let sh = sw * (ih as f64 / iw as f64);
        let (sx, sy) = if anchor == "center" { (x * vw_page - sw / 2.0, y * vh_page - sh / 2.0) } else { (x * vw_page, y * vh_page) };
        let (cx, cy) = (sx + sw / 2.0, sy + sh / 2.0);
        let t = viewer_to_page(&g);
        let phi = -rotate_deg.to_radians();
        let (c, s) = (phi.cos(), phi.sin());
        let mut ops = String::new();
        ops.push_str(&format!("\nq /{gs_name} gs {} {} {} {} {} {} cm", f(t[0]), f(t[1]), f(t[2]), f(t[3]), f(t[4]), f(t[5])));
        ops.push_str(&format!(" 1 0 0 1 {} {} cm {} {} {} {} 0 0 cm 1 0 0 1 {} {} cm", f(cx), f(cy), f(c), f(s), f(-s), f(c), f(-cx), f(-cy)));
        ops.push_str(&format!(" {} 0 0 {} {} {} cm /{im_name} Do Q\n", f(sw), f(-sh), f(sx), f(sy + sh)));
        let mut content = b"q\n".to_vec();
        content.extend_from_slice(&doc.get_page_content(pid));
        content.extend_from_slice(b"\nQ");
        content.extend_from_slice(MARK_BEGIN);
        content.extend_from_slice(ops.as_bytes());
        content.extend_from_slice(MARK_END);
        doc.change_page_content(pid, content).map_err(e)?;
        doc.add_xobject(pid, im_name.as_str(), img_id).map_err(e)?;
        doc.add_graphics_state(pid, gs_name.as_str(), gs_id).map_err(e)?;
    }
    Ok(())
}

pub fn redact(doc: &mut Document, page: u32, jpeg: Vec<u8>, width_pt: f64, height_pt: f64) -> R<()> {
    let all = ordered_pages(doc);
    let pid = *all.get(page.checked_sub(1).ok_or("หน้าไม่ถูกต้อง")? as usize).ok_or(format!("ไม่มีหน้า {page}"))?;
    let im = lopdf::xobject::image_from(jpeg).map_err(e)?;
    let im_id = doc.add_object(im);
    let content = format!("q {} 0 0 {} 0 0 cm /ImR Do Q", f(width_pt), f(height_pt)).into_bytes();
    let cid = doc.add_object(Stream::new(dictionary! {}, content));
    let res = doc.add_object(dictionary! { "XObject" => dictionary! { "ImR" => Object::Reference(im_id) } });
    let d = doc.get_object_mut(pid).and_then(Object::as_dict_mut).map_err(e)?;
    d.set("MediaBox", vec![0.into(), 0.into(), Object::Real(width_pt as f32), Object::Real(height_pt as f32)]);
    for k in ["CropBox", "BleedBox", "TrimBox", "ArtBox", "Annots", "Group", "StructParents", "Thumb"] { d.remove(k.as_bytes()); }
    d.set("Rotate", 0);
    d.set("Contents", Object::Reference(cid));
    d.set("Resources", Object::Reference(res));
    doc.prune_objects();
    Ok(())
}

fn find(h: &[u8], n: &[u8], from: usize) -> Option<usize> { if from > h.len() { return None; } h[from..].windows(n.len()).position(|w| w == n).map(|i| i + from) }

/// Remove all mark segments from every page. Returns number of segments removed.
pub fn remove_marks(doc: &mut Document) -> R<u32> {
    let mut removed = 0;
    for pid in ordered_pages(doc) {
        let content = doc.get_page_content(pid);
        if find(&content, MARK_BEGIN, 0).is_none() { continue; }
        let mut out = Vec::with_capacity(content.len());
        let mut i = 0;
        while let Some(b) = find(&content, MARK_BEGIN, i) {
            out.extend_from_slice(&content[i..b]);
            match find(&content, MARK_END, b) {
                Some(en) => { i = en + MARK_END.len(); removed += 1; }
                None => { i = content.len(); }
            }
        }
        out.extend_from_slice(&content[i.min(content.len())..]);
        doc.change_page_content(pid, out).map_err(e)?;
        // drop our resources (names start with DlpIm/DlpGs)
        if let Ok(res) = doc.get_or_create_resources(pid).and_then(Object::as_dict_mut) {
            for key in [b"XObject".as_slice(), b"ExtGState".as_slice()] {
                if let Ok(d) = res.get_mut(key).and_then(Object::as_dict_mut) {
                    let names: Vec<Vec<u8>> = d.iter().filter(|(k, _)| k.starts_with(b"DlpIm") || k.starts_with(b"DlpGs")).map(|(k, _)| k.clone()).collect();
                    for n in names { d.remove(&n); }
                }
            }
        }
    }
    doc.prune_objects();
    Ok(removed)
}

pub fn set_metadata(doc: &mut Document, title: Option<&str>, subject: Option<&str>, keywords: Option<&str>, classification: Option<&str>) -> R<()> {
    set_metadata_ext(doc, title, subject, keywords, classification, &BTreeMap::new())
}

pub fn set_metadata_ext(doc: &mut Document, title: Option<&str>, subject: Option<&str>, keywords: Option<&str>, classification: Option<&str>, extra: &BTreeMap<String, String>) -> R<()> {
    let info_id = match doc.trailer.get(b"Info") {
        Ok(Object::Reference(id)) if doc.get_object(*id).map(|o| o.as_dict().is_ok()).unwrap_or(false) => *id,
        _ => { let id = doc.add_object(Dictionary::new()); doc.trailer.set("Info", Object::Reference(id)); id }
    };
    let d = doc.get_object_mut(info_id).and_then(Object::as_dict_mut).map_err(e)?;
    if let Some(t) = title { d.set("Title", text_string(t)); }
    if let Some(t) = subject { d.set("Subject", text_string(t)); }
    if let Some(t) = keywords { d.set("Keywords", text_string(t)); }
    if let Some(t) = classification { d.set("Classification", text_string(t)); }
    for (k, v) in extra { if v.is_empty() { d.remove(k.as_bytes()); } else { d.set(k.as_bytes().to_vec(), text_string(v)); } }
    d.set("ModDate", Object::string_literal(chrono::Utc::now().format("D:%Y%m%d%H%M%SZ").to_string()));
    d.set("Producer", Object::string_literal("PDF DLP (oneable)"));
    Ok(())
}

fn b64(s: &str) -> R<Vec<u8>> {
    let s = s.split(',').next_back().unwrap_or(s);
    base64::engine::general_purpose::STANDARD.decode(s.trim()).map_err(|x| format!("base64 ไม่ถูกต้อง: {x}"))
}

/// Apply a list of ops to one document. Encrypt is always applied last.
pub fn apply(doc: &mut Document, ops: &[Op]) -> R<()> {
    let mut encrypt_op: Option<&Op> = None;
    for op in ops {
        match op {
            Op::Rotate { pages, angle } => rotate(doc, pages, *angle)?,
            Op::Delete { pages } => {
                let all = ordered_pages(doc);
                let keep: Vec<ObjectId> = all.iter().enumerate().filter(|(i, _)| !pages.contains(&((*i as u32) + 1))).map(|(_, id)| *id).collect();
                rebuild_pages(doc, keep)?;
            }
            Op::Reorder { order } => { let all = ordered_pages(doc); let sel = select(&all, order)?; rebuild_pages(doc, sel)?; }
            Op::Extract { pages } => { let all = ordered_pages(doc); let sel = select(&all, pages)?; rebuild_pages(doc, sel)?; }
            Op::Encrypt { .. } => encrypt_op = Some(op),
            Op::Decrypt {} => {} // load() already decrypted; saving without Encrypt = decrypted output
            Op::Stamp { pages, png_base64, x, y, w, opacity, rotate_deg, anchor } => { let png = b64(png_base64)?; stamp(doc, pages, &png, *x, *y, *w, *opacity, *rotate_deg, anchor)?; }
            Op::Redact { page, jpeg_base64, width_pt, height_pt } => { let j = b64(jpeg_base64)?; redact(doc, *page, j, *width_pt, *height_pt)?; }
            Op::Metadata { title, subject, keywords, classification, extra } => set_metadata_ext(doc, title.as_deref(), subject.as_deref(), keywords.as_deref(), classification.as_deref(), extra)?,
            Op::RemoveMarks {} => { remove_marks(doc)?; }
            Op::Crop { pages, left, top, right, bottom } => crop(doc, pages, *left, *top, *right, *bottom)?,
            Op::Sanitize { keep_annotations } => sanitize(doc, *keep_annotations)?,
        }
    }
    doc.renumber_objects();
    doc.compress();
    if let Some(Op::Encrypt { user_password, owner_password, allow_print, allow_copy, allow_modify, allow_annotate }) = encrypt_op {
        encrypt(doc, user_password, owner_password, permissions(*allow_print, *allow_copy, *allow_modify, *allow_annotate))?;
    }
    Ok(())
}

/// Crop by shrinking CropBox (and MediaBox) using viewer-orientation insets.
pub fn crop(doc: &mut Document, pages: &[u32], left: f64, top: f64, right: f64, bottom: f64) -> R<()> {
    for v in [left, top, right, bottom] { if !(0.0..0.49).contains(&v) { return Err("ค่าครอปต้องอยู่ระหว่าง 0-49%".into()); } }
    for pid in page_set(doc, pages) {
        inherit_attrs(doc, pid);
        let g = geometry(doc, pid)?;
        let (pw, ph) = (g.x1 - g.x0, g.y1 - g.y0);
        // viewer insets -> page-space insets depending on rotation
        let (l, t, r, b) = match g.rot { 90 => (top, right, bottom, left), 180 => (right, bottom, left, top), 270 => (bottom, left, top, right), _ => (left, top, right, bottom) };
        let nb = vec![Object::Real((g.x0 + pw * l) as f32), Object::Real((g.y0 + ph * b) as f32), Object::Real((g.x1 - pw * r) as f32), Object::Real((g.y1 - ph * t) as f32)];
        let d = doc.get_object_mut(pid).and_then(Object::as_dict_mut).map_err(e)?;
        d.set("CropBox", nb.clone());
        d.set("MediaBox", nb);
        for k in ["BleedBox", "TrimBox", "ArtBox"] { d.remove(k.as_bytes()); }
    }
    Ok(())
}

pub fn sanitize(doc: &mut Document, keep_annotations: bool) -> R<()> {
    // Info dictionary: wipe everything (classification included — caller re-applies if wanted)
    if let Ok(Object::Reference(id)) = doc.trailer.get(b"Info").cloned() { doc.objects.remove(&id); }
    doc.trailer.remove(b"Info");
    let cat = catalog_id(doc)?;
    if let Ok(c) = doc.get_object_mut(cat).and_then(Object::as_dict_mut) {
        for k in ["Metadata", "OpenAction", "AA", "Names", "JavaScript", "EmbeddedFiles", "Collection", "Requirements", "Perms", "PieceInfo", "SpiderInfo", "OCProperties"] { c.remove(k.as_bytes()); }
        if !keep_annotations { c.remove(b"AcroForm"); }
    }
    for pid in ordered_pages(doc) {
        if let Ok(d) = doc.get_object_mut(pid).and_then(Object::as_dict_mut) {
            for k in ["Metadata", "AA", "PieceInfo", "Thumb"] { d.remove(k.as_bytes()); }
            if !keep_annotations { d.remove(b"Annots"); }
        }
    }
    // XMP streams anywhere
    let ids: Vec<ObjectId> = doc.objects.iter().filter(|(_, o)| o.as_stream().map(|s| s.dict.has_type(b"Metadata")).unwrap_or(false)).map(|(id, _)| *id).collect();
    for id in ids { doc.objects.remove(&id); }
    doc.prune_objects();
    Ok(())
}

/// Build a PDF from image files (JPEG/PNG/WebP), one page per image, fitted to A4 portrait/landscape or native pixel size at 96 dpi.
pub fn images_to_pdf(paths: &[PathBuf], fit_a4: bool) -> R<Document> {
    let mut doc = Document::with_version("1.7");
    let pages_id = doc.new_object_id();
    let mut kids = vec![];
    for p in paths {
        let bytes = std::fs::read(p).map_err(|x| format!("อ่านรูปไม่ได้ {}: {x}", p.display()))?;
        let (iw, ih) = image::load_from_memory(&bytes).map_err(e).map(|i| (i.width() as f64, i.height() as f64))?;
        let is_jpeg = bytes.starts_with(&[0xFF, 0xD8]);
        let img_id = if is_jpeg { doc.add_object(lopdf::xobject::image_from(bytes).map_err(e)?) } else { add_png_with_alpha(&mut doc, &bytes)?.0 };
        let (pw, ph) = if fit_a4 { if iw >= ih { (841.89, 595.28) } else { (595.28, 841.89) } } else { (iw * 72.0 / 96.0, ih * 72.0 / 96.0) };
        let s = (pw / iw).min(ph / ih); let (dw, dh) = (iw * s, ih * s); let (ox, oy) = ((pw - dw) / 2.0, (ph - dh) / 2.0);
        let content = format!("q {} 0 0 {} {} {} cm /Im0 Do Q", f(dw), f(dh), f(ox), f(oy)).into_bytes();
        let cid = doc.add_object(Stream::new(dictionary! {}, content));
        let res = doc.add_object(dictionary! { "XObject" => dictionary! { "Im0" => Object::Reference(img_id) } });
        let page = doc.add_object(dictionary! { "Type" => "Page", "Parent" => Object::Reference(pages_id), "MediaBox" => vec![0.into(), 0.into(), Object::Real(pw as f32), Object::Real(ph as f32)], "Contents" => Object::Reference(cid), "Resources" => Object::Reference(res) });
        kids.push(Object::Reference(page));
    }
    if kids.is_empty() { return Err("ไม่มีรูป".into()); }
    let count = kids.len() as i64;
    doc.objects.insert(pages_id, Object::Dictionary(dictionary! { "Type" => "Pages", "Kids" => kids, "Count" => count }));
    let cat = doc.add_object(dictionary! { "Type" => "Catalog", "Pages" => Object::Reference(pages_id) });
    doc.trailer.set("Root", Object::Reference(cat));
    set_metadata(&mut doc, None, None, None, None)?;
    doc.compress();
    Ok(doc)
}

pub fn save(doc: &mut Document, out: &Path) -> R<()> {
    if let Some(p) = out.parent() { std::fs::create_dir_all(p).map_err(e)?; }
    if !doc.is_encrypted() { doc.renumber_objects(); }
    doc.save(out).map(|_| ()).map_err(|x| format!("บันทึกไฟล์ไม่ได้: {x}"))
}

/// Merge: all pages of each input in order. Each input: (path, password, pages 1-based or empty=all).
pub fn merge(inputs: &[(PathBuf, String, Vec<u32>)]) -> R<Document> {
    let mut out = Document::with_version("1.7");
    let root = out.new_object_id();
    let mut max_id = root.0 + 1;
    let mut kids: Vec<Object> = vec![];
    for (path, pw, pages) in inputs {
        let mut doc = load(path, pw)?;
        for p in ordered_pages(&doc) { inherit_attrs(&mut doc, p); }
        doc.renumber_objects_with(max_id);
        max_id = doc.max_id + 1;
        let all = ordered_pages(&doc);
        let chosen = if pages.is_empty() { all } else { select(&all, pages)? };
        let objects = std::mem::take(&mut doc.objects);
        for (id, obj) in objects {
            match obj.type_name().unwrap_or(b"") {
                b"Catalog" | b"Pages" | b"Outlines" | b"Outline" => continue,
                _ => { out.objects.insert(id, obj); }
            }
        }
        for pid in chosen {
            if let Some(d) = out.objects.get_mut(&pid).and_then(|o| o.as_dict_mut().ok()) { d.set("Parent", Object::Reference(root)); }
            kids.push(Object::Reference(pid));
        }
    }
    if kids.is_empty() { return Err("ไม่มีหน้าให้รวม".into()); }
    out.max_id = max_id;
    let count = kids.len() as i64;
    out.objects.insert(root, Object::Dictionary(dictionary! { "Type" => "Pages", "Kids" => kids, "Count" => count }));
    let cat = out.add_object(dictionary! { "Type" => "Catalog", "Pages" => Object::Reference(root) });
    out.trailer.set("Root", Object::Reference(cat));
    set_metadata(&mut out, None, None, None, None)?;
    out.prune_objects();
    out.renumber_objects();
    out.compress();
    Ok(out)
}

/// Parse "1-3,5,8-" style ranges into 1-based page numbers.
pub fn parse_ranges(spec: &str, total: u32) -> R<Vec<u32>> {
    let mut out = vec![];
    for part in spec.split([',', ' ']).map(str::trim).filter(|s| !s.is_empty()) {
        if let Some((a, b)) = part.split_once('-') {
            let a: u32 = if a.trim().is_empty() { 1 } else { a.trim().parse().map_err(|_| format!("ช่วงหน้าไม่ถูกต้อง: {part}"))? };
            let b: u32 = if b.trim().is_empty() { total } else { b.trim().parse().map_err(|_| format!("ช่วงหน้าไม่ถูกต้อง: {part}"))? };
            if a < 1 || b > total || a > b { return Err(format!("ช่วงหน้า {part} อยู่นอก 1-{total}")); }
            out.extend(a..=b);
        } else {
            let p: u32 = part.parse().map_err(|_| format!("หน้าไม่ถูกต้อง: {part}"))?;
            if p < 1 || p > total { return Err(format!("หน้า {p} อยู่นอก 1-{total}")); }
            out.push(p);
        }
    }
    if out.is_empty() { return Err("ไม่ได้ระบุหน้า".into()); }
    Ok(out)
}
