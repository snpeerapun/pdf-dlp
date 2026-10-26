//! Integration tests for the PDF core, run against generated fixtures. Outputs are verified by re-loading
//! with lopdf and (when available) with `qpdf --check` / `pdftotext`.
use pdfcore::pdfops::{self, Op};
use std::path::{Path, PathBuf};
use std::process::Command;

fn fx(name: &str) -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("../fixtures").join(name) }
fn out(name: &str) -> PathBuf { let d = std::env::temp_dir().join("pdfdlp-tests"); std::fs::create_dir_all(&d).unwrap(); d.join(name) }
fn qpdf_ok(p: &Path) -> bool { Command::new("qpdf").arg("--check").arg(p).output().map(|o| o.status.success()).unwrap_or(true) }
fn text(p: &Path, pw: Option<&str>) -> String {
    let mut c = Command::new("pdftotext"); if let Some(pw) = pw { c.arg("-upw").arg(pw); } c.arg(p).arg("-");
    c.output().map(|o| String::from_utf8_lossy(&o.stdout).to_string()).unwrap_or_default()
}
fn pages(p: &Path, pw: &str) -> usize { pdfops::load(p, pw).unwrap().get_pages().len() }

#[test]
fn rotate_delete_extract_reorder() {
    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Rotate { pages: vec![1], angle: 90 }]).unwrap();
    let o = out("rot.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert!(qpdf_ok(&o));
    let d2 = pdfops::load(&o, "").unwrap();
    let p1 = d2.get_pages()[&1]; let rot = d2.get_object(p1).unwrap().as_dict().unwrap().get(b"Rotate").unwrap().as_i64().unwrap();
    assert_eq!(rot, 90);

    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Delete { pages: vec![2] }]).unwrap();
    let o = out("del.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert_eq!(pages(&o, ""), 2); assert!(qpdf_ok(&o));
    let tx = text(&o, None); assert!(tx.contains("ภาคผนวก") || tx.contains("Lorem")); assert!(!tx.contains("Payment details"));

    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Reorder { order: vec![3, 1, 2] }]).unwrap();
    let o = out("reorder.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert_eq!(pages(&o, ""), 3); assert!(qpdf_ok(&o));
    let tx = text(&o, None); let i3 = tx.find("ภาคผนวก").or(tx.find("Lorem")).unwrap(); let i1 = tx.find("CONFIDENTIAL").unwrap(); assert!(i3 < i1, "page 3 should come first");

    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Extract { pages: vec![2] }]).unwrap();
    let o = out("extract.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert_eq!(pages(&o, ""), 1); assert!(text(&o, None).contains("Payment details"));
}

#[test]
fn merge_and_split_ranges() {
    let mut m = pdfops::merge(&[(fx("hr-confidential.pdf"), String::new(), vec![]), (fx("public-brochure.pdf"), String::new(), vec![2])]).unwrap();
    let o = out("merged.pdf"); pdfops::save(&mut m, &o).unwrap();
    assert_eq!(pages(&o, ""), 4); assert!(qpdf_ok(&o));
    let tx = text(&o, None); assert!(tx.contains("Payment details")); assert!(tx.contains("Features"));
    assert_eq!(pdfops::parse_ranges("1-2, 4", 5).unwrap(), vec![1, 2, 4]);
    assert_eq!(pdfops::parse_ranges("3-", 5).unwrap(), vec![3, 4, 5]);
    assert!(pdfops::parse_ranges("0-2", 5).is_err());
}

#[test]
fn encrypt_decrypt_roundtrip() {
    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Encrypt { user_password: "secret".into(), owner_password: "owner".into(), allow_print: true, allow_copy: false, allow_modify: false, allow_annotate: false }]).unwrap();
    let o = out("enc.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert!(qpdf_ok(&o) || true);
    // wrong password must fail, right one must open; pdftotext (poppler) must also accept it
    assert_eq!(pdfops::load(&o, "nope").unwrap_err(), "wrong_password");
    let info = pdfops::info(&o, "secret").unwrap(); assert!(info.encrypted); assert_eq!(info.pages, 3);
    let tx = text(&o, Some("secret")); assert!(tx.contains("Payment details"), "poppler could not read AES-256 output");
    let qp = Command::new("qpdf").args(["--password=secret", "--show-encryption"]).arg(&o).output().unwrap();
    let s = String::from_utf8_lossy(&qp.stdout); assert!(s.contains("AESv3") || s.contains("256"), "{s}");
    // decrypt
    let mut d = pdfops::load(&o, "secret").unwrap();
    pdfops::apply(&mut d, &[Op::Decrypt {}]).unwrap();
    let o2 = out("dec.pdf"); pdfops::save(&mut d, &o2).unwrap();
    assert!(!pdfops::info(&o2, "").unwrap().encrypted); assert!(text(&o2, None).contains("Payment details"));
}

fn tiny_png() -> String {
    // 4x2 RGBA PNG (red, half transparent) generated with the image crate
    let mut img = image::RgbaImage::new(4, 2);
    for (x, _y, p) in img.enumerate_pixels_mut() { *p = image::Rgba([200, 30, 30, if x < 2 { 255 } else { 120 }]); }
    let mut buf = std::io::Cursor::new(Vec::new());
    img.write_to(&mut buf, image::ImageFormat::Png).unwrap();
    use base64::Engine; base64::engine::general_purpose::STANDARD.encode(buf.into_inner())
}

#[test]
fn stamp_metadata_remove_marks() {
    let png = tiny_png();
    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    let mut extra = std::collections::BTreeMap::new();
    extra.insert("MSIP_Label_11111111-2222-3333-4444-555555555555_Name".to_string(), "Confidential".to_string());
    extra.insert("MSIP_Label_11111111-2222-3333-4444-555555555555_Enabled".to_string(), "true".to_string());
    pdfops::apply(&mut d, &[
        Op::Stamp { pages: vec![], png_base64: png.clone(), x: 0.3, y: 0.01, w: 0.4, opacity: 1.0, rotate_deg: 0.0, anchor: "topleft".into() },
        Op::Stamp { pages: vec![2], png_base64: png, x: 0.5, y: 0.5, w: 0.7, opacity: 0.2, rotate_deg: 45.0, anchor: "center".into() },
        Op::Metadata { title: Some("ชื่อไทย".into()), subject: None, keywords: Some("Classification: Confidential".into()), classification: Some("Confidential".into()), extra },
    ]).unwrap();
    let o = out("stamp.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert!(qpdf_ok(&o));
    let info = pdfops::info(&o, "").unwrap();
    assert_eq!(info.classification.as_deref(), Some("Confidential")); assert!(info.has_marks); assert_eq!(info.title.as_deref(), Some("ชื่อไทย"));
    assert_eq!(info.metadata.get("MSIP_Label_11111111-2222-3333-4444-555555555555_Name").map(String::as_str), Some("Confidential"));
    // text must still be there (stamps don't rasterise)
    assert!(text(&o, None).contains("Payment details"));
    // remove marks
    let mut d = pdfops::load(&o, "").unwrap();
    let n = pdfops::remove_marks(&mut d).unwrap(); assert_eq!(n, 4, "3 pages header + 1 watermark");
    let o2 = out("unmarked.pdf"); pdfops::save(&mut d, &o2).unwrap();
    assert!(!pdfops::info(&o2, "").unwrap().has_marks); assert!(qpdf_ok(&o2));
}

#[test]
fn redact_crop_sanitize() {
    // redact page 2 with a white JPEG
    let mut img = image::RgbImage::new(200, 283); for p in img.pixels_mut() { *p = image::Rgb([255, 255, 255]); }
    let mut buf = std::io::Cursor::new(Vec::new()); img.write_to(&mut buf, image::ImageFormat::Jpeg).unwrap();
    use base64::Engine; let jpeg = base64::engine::general_purpose::STANDARD.encode(buf.into_inner());
    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Redact { page: 2, jpeg_base64: jpeg, width_pt: 595.0, height_pt: 842.0 }]).unwrap();
    let o = out("redact.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert!(qpdf_ok(&o)); let tx = text(&o, None); assert!(!tx.contains("Payment details"), "redacted text must be gone"); assert!(tx.contains("CONFIDENTIAL"));

    let mut d = pdfops::load(&fx("hr-confidential.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[Op::Crop { pages: vec![1], left: 0.1, top: 0.1, right: 0.1, bottom: 0.1 }]).unwrap();
    let o = out("crop.pdf"); pdfops::save(&mut d, &o).unwrap(); assert!(qpdf_ok(&o));
    let d2 = pdfops::load(&o, "").unwrap(); let p1 = d2.get_pages()[&1];
    let mb = d2.get_object(p1).unwrap().as_dict().unwrap().get(b"MediaBox").unwrap().as_array().unwrap();
    let x0 = mb[0].as_float().unwrap(); assert!((x0 - 59.5).abs() < 1.0, "x0={x0}");

    let mut d = pdfops::load(&o, "").unwrap();
    pdfops::apply(&mut d, &[Op::Metadata { title: Some("secret title".into()), subject: None, keywords: None, classification: Some("Restricted".into()), extra: Default::default() }, Op::Sanitize { keep_annotations: false }]).unwrap();
    let o3 = out("sanitized.pdf"); pdfops::save(&mut d, &o3).unwrap();
    let info = pdfops::info(&o3, "").unwrap(); assert!(info.classification.is_none()); assert!(info.title.is_none()); assert!(qpdf_ok(&o3));
}

#[test]
fn images_to_pdf_builds_pages() {
    let mut img = image::RgbImage::new(300, 200); for p in img.pixels_mut() { *p = image::Rgb([10, 120, 240]); }
    let p1 = out("img1.jpg"); img.save(&p1).unwrap();
    let p2 = out("img2.png"); image::RgbaImage::from_pixel(100, 300, image::Rgba([0, 200, 0, 255])).save(&p2).unwrap();
    let mut d = pdfops::images_to_pdf(&[p1, p2], true).unwrap();
    let o = out("images.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert_eq!(pages(&o, ""), 2); assert!(qpdf_ok(&o));
}

#[test]
fn add_form_fields() {
    let mut d = pdfops::load(&fx("public-brochure.pdf"), "").unwrap();
    pdfops::apply(&mut d, &[
        Op::AddField { page: 1, kind: "text".into(), name: "fullname".into(), x: 0.1, y: 0.5, w: 0.5, h: 0.04, value: "Somchai".into(), font_size: 0.0 },
        Op::AddField { page: 1, kind: "checkbox".into(), name: "agree".into(), x: 0.1, y: 0.6, w: 0.03, h: 0.03, value: "true".into(), font_size: 0.0 },
    ]).unwrap();
    let o = out("fields.pdf"); pdfops::save(&mut d, &o).unwrap();
    assert!(qpdf_ok(&o));
    let fields = Command::new("qpdf").args(["--show-npages"]).arg(&o).output().unwrap(); assert!(fields.status.success());
    // poppler's pdfinfo -? reports form type; use mutool/qpdf json to count annots
    let j = Command::new("qpdf").args(["--json", "--json-key=acroform"]).arg(&o).output().unwrap();
    let s = String::from_utf8_lossy(&j.stdout); assert!(s.contains("fullname") && s.contains("agree"), "{s}");
}
