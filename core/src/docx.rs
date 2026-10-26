//! Minimal DOCX writer: paragraphs (size/bold/colour/alignment), inline images, page breaks, page size.
//! Input is a JSON document produced by the C++ extractor (PDFium text + image objects).
use base64::Engine;
use serde::Deserialize;
use std::io::Write;

#[derive(Deserialize)] pub struct Doc { pub pages: Vec<Page> }
#[derive(Deserialize)] pub struct Page { pub width: f64, pub height: f64, pub blocks: Vec<Block> }
#[derive(Deserialize)] #[serde(tag = "type", rename_all = "snake_case")]
pub enum Block {
    P { runs: Vec<Run>, #[serde(default)] align: String, #[serde(default)] space_before: f64 },
    Img { png_base64: String, w: f64, h: f64, #[serde(default)] align: String },
}
#[derive(Deserialize)] pub struct Run { pub text: String, pub size: f64, #[serde(default)] pub bold: bool, #[serde(default)] pub italic: bool, #[serde(default)] pub color: String }

fn esc(s: &str) -> String { s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;") }

pub fn write(doc: &Doc, out: &std::path::Path) -> Result<(), String> {
    let file = std::fs::File::create(out).map_err(|e| e.to_string())?;
    let mut zip = zip::ZipWriter::new(file);
    let opt = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    let (pw, ph) = doc.pages.first().map(|p| (p.width, p.height)).unwrap_or((595.3, 841.9));
    let mut body = String::new(); let mut media: Vec<(String, Vec<u8>)> = vec![]; let mut rels = String::new();
    for (pi, page) in doc.pages.iter().enumerate() {
        if pi > 0 { body.push_str("<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"); }
        for b in &page.blocks {
            match b {
                Block::P { runs, align, space_before } => {
                    body.push_str("<w:p><w:pPr>");
                    if align == "center" || align == "right" { body.push_str(&format!("<w:jc w:val=\"{align}\"/>")); }
                    body.push_str(&format!("<w:spacing w:before=\"{}\" w:after=\"60\"/></w:pPr>", (space_before.max(0.0) * 20.0) as i64));
                    for r in runs {
                        body.push_str("<w:r><w:rPr>");
                        if r.bold { body.push_str("<w:b/><w:bCs/>"); }
                        if r.italic { body.push_str("<w:i/><w:iCs/>"); }
                        if !r.color.is_empty() && r.color != "#000000" { body.push_str(&format!("<w:color w:val=\"{}\"/>", r.color.trim_start_matches('#'))); }
                        let half = ((r.size.max(4.0)) * 2.0).round() as i64;
                        body.push_str(&format!("<w:sz w:val=\"{half}\"/><w:szCs w:val=\"{half}\"/></w:rPr><w:t xml:space=\"preserve\">{}</w:t></w:r>", esc(&r.text)));
                    }
                    body.push_str("</w:p>");
                }
                Block::Img { png_base64, w, h, align } => {
                    let bytes = base64::engine::general_purpose::STANDARD.decode(png_base64.split(',').next_back().unwrap_or("")).map_err(|e| e.to_string())?;
                    let id = media.len() + 1; let name = format!("image{id}.png"); let rid = format!("rIdImg{id}");
                    media.push((name.clone(), bytes));
                    rels.push_str(&format!("<Relationship Id=\"{rid}\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/{name}\"/>"));
                    let (cx, cy) = ((w * 12700.0) as i64, (h * 12700.0) as i64);
                    body.push_str(&format!("<w:p><w:pPr>{}</w:pPr><w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"{cx}\" cy=\"{cy}\"/><wp:docPr id=\"{id}\" name=\"Picture {id}\"/><a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:nvPicPr><pic:cNvPr id=\"{id}\" name=\"{name}\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"{rid}\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"{cx}\" cy=\"{cy}\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>",
                        if align == "center" { "<w:jc w:val=\"center\"/>" } else { "" }));
                }
            }
        }
    }
    let sect = format!("<w:sectPr><w:pgSz w:w=\"{}\" w:h=\"{}\"{}/><w:pgMar w:top=\"1000\" w:right=\"1000\" w:bottom=\"1000\" w:left=\"1000\" w:header=\"500\" w:footer=\"500\" w:gutter=\"0\"/></w:sectPr>", (pw * 20.0) as i64, (ph * 20.0) as i64, if pw > ph { " w:orient=\"landscape\"" } else { "" });
    let document = format!("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\"><w:body>{body}{sect}</w:body></w:document>");
    let styles = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Sarabun\" w:hAnsi=\"Sarabun\" w:cs=\"Sarabun\" w:eastAsia=\"Sarabun\"/><w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/><w:lang w:val=\"en-US\" w:bidi=\"th-TH\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"60\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style></w:styles>";
    let content_types = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/></Types>";
    let root_rels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
    let doc_rels = format!("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>{rels}</Relationships>");
    let mut put = |name: &str, data: &[u8]| -> Result<(), String> { zip.start_file(name, opt).map_err(|e| e.to_string())?; zip.write_all(data).map_err(|e| e.to_string()) };
    put("[Content_Types].xml", content_types.as_bytes())?; put("_rels/.rels", root_rels.as_bytes())?; put("word/document.xml", document.as_bytes())?; put("word/styles.xml", styles.as_bytes())?; put("word/_rels/document.xml.rels", doc_rels.as_bytes())?;
    for (name, bytes) in &media { put(&format!("word/media/{name}"), bytes)?; }
    zip.finish().map_err(|e| e.to_string())?;
    Ok(())
}
