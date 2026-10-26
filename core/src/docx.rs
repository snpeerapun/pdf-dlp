//! Minimal DOCX writer: paragraphs (size/bold/colour/alignment), inline images, page breaks, page size.
//! Input is a JSON document produced by the C++ extractor (PDFium text + image objects).
use base64::Engine;
use serde::Deserialize;
use std::io::Write;

#[derive(Deserialize)] pub struct Doc { pub pages: Vec<Page> }
#[derive(Deserialize)] pub struct Page { pub width: f64, pub height: f64, pub blocks: Vec<Block> }
#[derive(Deserialize)] #[serde(tag = "type", rename_all = "snake_case")]
pub enum Block {
    P { runs: Vec<Run>, #[serde(default)] align: String, #[serde(default)] space_before: f64, #[serde(default)] tabs: Vec<f64> },
    Img { png_base64: String, w: f64, h: f64, #[serde(default)] align: String },
    Table { cols: Vec<f64>, rows: Vec<Vec<Cell>>, #[serde(default)] borders: bool, #[serde(default)] left: f64, #[serde(default)] space_before: f64 },
}
#[derive(Deserialize)] pub struct Cell { pub runs: Vec<Run>, #[serde(default)] pub align: String }
#[derive(Deserialize)] pub struct Run { pub text: String, pub size: f64, #[serde(default)] pub bold: bool, #[serde(default)] pub italic: bool, #[serde(default)] pub color: String }

fn esc(s: &str) -> String { s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;") }

fn para(runs: &[Run], align: &str, space_before: f64, tabs: &[f64], after: i64) -> String {
    let mut s = String::from("<w:p><w:pPr>");
    if !tabs.is_empty() { s.push_str("<w:tabs>"); for t in tabs { s.push_str(&format!("<w:tab w:val=\"left\" w:pos=\"{}\"/>", ((t - 50.0).max(0.0) * 20.0) as i64)); } s.push_str("</w:tabs>"); }
    s.push_str(&format!("<w:spacing w:before=\"{}\" w:after=\"{after}\"/>", (space_before.max(0.0) * 20.0) as i64));
    if align == "center" || align == "right" { s.push_str(&format!("<w:jc w:val=\"{align}\"/>")); }
    s.push_str("</w:pPr>");
    for r in runs {
        let mut rpr = String::new();
        if r.bold { rpr.push_str("<w:b/><w:bCs/>"); }
        if r.italic { rpr.push_str("<w:i/><w:iCs/>"); }
        if !r.color.is_empty() && r.color != "#000000" { rpr.push_str(&format!("<w:color w:val=\"{}\"/>", r.color.trim_start_matches('#'))); }
        let half = ((r.size.max(4.0)) * 2.0).round() as i64;
        rpr.push_str(&format!("<w:sz w:val=\"{half}\"/><w:szCs w:val=\"{half}\"/>"));
        // split on tabs: each becomes <w:tab/>
        for (i, piece) in r.text.split('\t').enumerate() {
            if i > 0 { s.push_str(&format!("<w:r><w:rPr>{rpr}</w:rPr><w:tab/></w:r>")); }
            if !piece.is_empty() { s.push_str(&format!("<w:r><w:rPr>{rpr}</w:rPr><w:t xml:space=\"preserve\">{}</w:t></w:r>", esc(piece))); }
        }
    }
    s.push_str("</w:p>");
    s
}

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
                Block::P { runs, align, space_before, tabs } => {
                    body.push_str(&para(runs, align, *space_before, tabs, 60));
                }
                Block::Table { cols, rows, borders, left, space_before } => {
                    let ncol = cols.len().max(1);
                    let tw: i64 = cols.iter().map(|w| (w * 20.0) as i64).sum();
                    let bd = if *borders { "<w:tblBorders><w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/><w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/><w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/><w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/><w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/><w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/></w:tblBorders>" } else { "<w:tblBorders><w:top w:val=\"nil\"/><w:left w:val=\"nil\"/><w:bottom w:val=\"nil\"/><w:right w:val=\"nil\"/><w:insideH w:val=\"nil\"/><w:insideV w:val=\"nil\"/></w:tblBorders>" };
                    let pad = if *borders { 80 } else { 20 };
                    if *space_before > 2.0 { body.push_str(&format!("<w:p><w:pPr><w:spacing w:before=\"{}\" w:after=\"0\" w:line=\"20\" w:lineRule=\"exact\"/><w:rPr><w:sz w:val=\"2\"/></w:rPr></w:pPr></w:p>", ((space_before - 4.0).clamp(0.0, 36.0) * 20.0) as i64)); }
                    body.push_str(&format!("<w:tbl><w:tblPr><w:tblW w:w=\"{tw}\" w:type=\"dxa\"/><w:tblInd w:w=\"{}\" w:type=\"dxa\"/>{bd}<w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:left w:w=\"{pad}\" w:type=\"dxa\"/><w:right w:w=\"{pad}\" w:type=\"dxa\"/></w:tblCellMar><w:tblLook w:val=\"0000\"/></w:tblPr><w:tblGrid>", ((left - 50.0).max(0.0) * 20.0) as i64)); // page margin is 50pt
                    for w in cols { body.push_str(&format!("<w:gridCol w:w=\"{}\"/>", (w * 20.0) as i64)); }
                    body.push_str("</w:tblGrid>");
                    for row in rows {
                        body.push_str("<w:tr>");
                        for ci in 0..ncol {
                            let w = (cols.get(ci).copied().unwrap_or(60.0) * 20.0) as i64;
                            body.push_str(&format!("<w:tc><w:tcPr><w:tcW w:w=\"{w}\" w:type=\"dxa\"/><w:vAlign w:val=\"center\"/></w:tcPr>"));
                            match row.get(ci) { Some(c) if !c.runs.is_empty() => body.push_str(&para(&c.runs, &c.align, 0.0, &[], 0)), _ => body.push_str("<w:p/>") }
                            body.push_str("</w:tc>");
                        }
                        body.push_str("</w:tr>");
                    }
                    body.push_str("</w:tbl>");
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
    let document = format!("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\"><w:body>{body}<w:p/>{sect}</w:body></w:document>");
    let settings = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:defaultTabStop w:val=\"720\"/><w:characterSpacingControl w:val=\"doNotCompress\"/><w:compat><w:compatSetting w:name=\"compatibilityMode\" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/></w:compat></w:settings>";
    let styles = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Sarabun\" w:hAnsi=\"Sarabun\" w:cs=\"Sarabun\" w:eastAsia=\"Sarabun\"/><w:sz w:val=\"24\"/><w:szCs w:val=\"24\"/><w:lang w:val=\"en-US\" w:bidi=\"th-TH\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"60\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style></w:styles>";
    let content_types = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/><Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/></Types>";
    let root_rels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
    let doc_rels = format!("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/><Relationship Id=\"rIdSettings\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>{rels}</Relationships>");
    let mut put = |name: &str, data: &[u8]| -> Result<(), String> { zip.start_file(name, opt).map_err(|e| e.to_string())?; zip.write_all(data).map_err(|e| e.to_string()) };
    put("[Content_Types].xml", content_types.as_bytes())?; put("_rels/.rels", root_rels.as_bytes())?; put("word/document.xml", document.as_bytes())?; put("word/styles.xml", styles.as_bytes())?; put("word/settings.xml", settings.as_bytes())?; put("word/_rels/document.xml.rels", doc_rels.as_bytes())?;
    for (name, bytes) in &media { put(&format!("word/media/{name}"), bytes)?; }
    zip.finish().map_err(|e| e.to_string())?;
    Ok(())
}
