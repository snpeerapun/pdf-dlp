# PDF DLP — คู่มือโปรเจกต์ (สำหรับ Claude/Codex session ถัดไป)

แอป desktop **native C++ (Qt6 Widgets + PDFium)** บนแกน **Rust (lopdf)** ที่คอมไพล์เป็น static lib เรียกผ่าน C ABI
= PDF preview + tools (merge/split/หมุน/ลบ/แทรก/แยกหน้า/encrypt AES-256/decrypt/redact/watermark/เลขหน้า/ครอป/บีบอัด/sanitize/รูป↔PDF/พิมพ์)
+ **Data Classification** ที่ผู้ใช้เลือก/เปลี่ยนชั้นความลับเอง → เขียน metadata แบบ **Microsoft Purview (MSIP_Label_*)** + ประทับป้ายหัว/ท้าย/ลายน้ำที่ถอดออกได้ + batch หลายไฟล์
ทำงานในเครื่อง 100% · ธีมสว่าง + สีเน้น RGB · ไทย/อังกฤษ · Windows: คลิกขวาใน Explorer (registry HKCU) + single instance

ประวัติการตัดสินใจ (เจ้าของสั่ง 2026-10-26): Flutter → Tauri+React → **ย้ายเป็น C++ ทั้งหมด ลบ React/Tauri ทิ้ง** (ต้องการ native + ความลื่น) · แกน Rust คงไว้เพราะเทสต์ผ่านครบและใช้ร่วมได้

## โครงสร้าง
- `core/` — crate `pdfcore` (lopdf 0.46): `pdfops.rs` (ops ทั้งหมด), `labels.rs` (MSIP label discovery/metadata), `winshell.rs` (registry Windows) · tests: `core/tests/ops.rs` (ตรวจด้วย qpdf + pdftotext)
- `core-ffi/` — staticlib `libpdfcore.a` + `pdfcore.h` (JSON in/out: `pdfcore_info/apply/merge/split/images_to_pdf/discover_labels/labels_from_file/msip_metadata/new_guid/shell`)
- `cpp/src/` — `main.cpp` (fonts/theme/single-instance/`--selftest`), `mainwindow.*` (toolbar/docks/ops/undo/save), `viewer.*` (PDFium render queue 2 threads, CSS-like scale-then-swap, ctrl+wheel zoom), `sidebar.*` (files + thumbnails multi-select + right-click), `panels.*` (ToolsPanel/ClassifyPanel/InfoPanel), `dialogs.*` (ทุกเครื่องมือ), `stamps.*` (QPainter → PNG base64), `core.*` (bridge), `icons.h` (Font Awesome glyphs), `i18n.*`, `theme.qss`
- `cpp/assets/fonts` — Noto Sans Thai + Font Awesome 6 OTF · `cpp/pdfium/` — prebuilt bblanchon (gitignored; โหลดใหม่ได้จาก releases)
- `fixtures/` + `tools/make_fixtures.py` (Chrome headless สร้าง PDF ไทยทดสอบ) · `.github/workflows/build.yml` (Windows zip ผ่าน aqt + pdfium + windeployqt)

## รัน / ทดสอบ (macOS)
- build: `cd cpp && ./build.sh` → `build/PDF DLP.app` (bundle มี libpdfium.dylib, fonts, theme) · เปิด: `open "cpp/build/PDF DLP.app"` หรือ `"…/Contents/MacOS/PDF DLP" file.pdf`
- Rust tests: `cd core && cargo test --test ops` (6 เทสต์: rotate/delete/reorder/extract, merge+ranges, encrypt-decrypt, stamp+metadata+remove_marks, redact+crop+sanitize, images→pdf)
- E2E headless ทั้งสาย C++→Rust→PDFium: `"cpp/build/PDF DLP.app/Contents/MacOS/PDF DLP" --selftest fixtures/hr-confidential.pdf /tmp/out` → ต้อง `RESULT PASS` (10 ขั้น) + ดู `/tmp/out/selftest-classified-p1.png`
- screenshot หน้าต่าง: Quartz หา owner `PDF DLP` เอาหน้าต่างใหญ่สุด แล้ว `screencapture -l <id>` (มีหน้าต่าง 500×500 ซ่อนของ Qt ปนมาด้วย)

## โหมดแก้ไข (cpp/src/edit.*) — ข้อตกลงสำคัญ
- EditSession เปิด FPDF_DOCUMENT แยก + form-fill env · **ห้ามเรียก PDFium เขียน content stream เดิมใหม่** (`FPDFPageObj_Transform`/`RemoveObject` บน object เดิม → `FPDFPage_GenerateContent` เขียน stream ใหม่แล้วตัวหนังสือหายทั้งหน้าใน PDF จาก Chrome/Skia — พิสูจน์ด้วย `--editdebug`)
  ⇒ ย้าย/แก้/ลบ object เดิม = **ปิดทับด้วยกล่องขาว + วาง object ใหม่** (เพิ่มเข้า stream ใหม่เท่านั้น) · ของที่ไม่ได้แตะคงเดิมไบต์ต่อไบต์
- PDF จาก Chrome แตก text เป็น object ละ 1 glyph + สระ/วรรณยุกต์เป็น object จิ๋วแยก → `objects()` รวมเป็น "บรรทัด" (ซ้อนแนวตั้ง/ช่องว่าง ≤1.6×สูง) และสร้างข้อความบรรทัดจาก glyph ฐาน โดยข้าม mark object ที่ซ้ำ (เช็คทั้งก่อน/หลังใน x-order)
- บั๊กที่เคยทำให้ "คลิก object ไม่ติด" (แก้ 2026-10-26 เย็น): (1) `PageWidget::invalidate()` ล้าง objs แต่ไม่รีเซ็ต `objsLoaded` → หลังแก้ไขครั้งแรก/เข้า edit รอบสอง รายการว่างถาวร (2) line builder ใช้ความสูงของกล่องรวมเป็นเกณฑ์ → ลูกโซ่กลืน glyph ทั้งหน้าเป็นก้อนเดียว (Plantora pdf: 554 glyphs → 482×686) ⇒ ตอนนี้ใช้ core band + ref = effective font size เท่านั้น
- debug: `--objdump <pdf> [page]` พิมพ์ object ที่รวมแล้ว · `PDFDLP_START_EDIT=1` เข้าโหมด edit อัตโนมัติ · `PDFDLP_NO_SINGLE=1` เปิด instance ที่สองได้ (ทดสอบคลิกด้วย cliclick โดยไม่แตะแอปที่เจ้าของเปิดอยู่)
- ข้อความที่วางใหม่ใช้ฟอนต์ฝัง (TTF ใน `assets/fonts/edit`, CID) เสมอ — SetText กับฟอนต์เดิมของเอกสารแปลง charcode ไม่ได้ (เรนเดอร์ว่าง)
- ช่องฟอร์มใหม่สร้างฝั่ง Rust (`Op::AddField`) หลัง PDFium save · กรอกฟอร์มเดิมผ่าน FORM_On* + FFLDraw
- debug: `"PDF DLP" --editdebug <pdf> <outdir>` เรนเดอร์ 9 กรณีเทียบ · selftest มีขั้น edit

## เพิ่มเติม 2026-10-26 (เย็น)
- ค้นหาข้อความ (⌘F, `PdfDoc::search` ผ่าน FPDFText_Find*, รวมกรอบต่อคำ) · ไฮไลต์เหลือง/ส้ม · `PDFDLP_SEARCH=<คำ>` เปิดแถบค้นหาอัตโนมัติสำหรับ screenshot
- แก้ไข: พิมพ์ในกล่อง inline บนหน้า (`Viewer::openInlineEditor`) · ย้าย/ลบได้ทุก object (path คัดลอก segment) · ปุ่ม "บันทึกการแก้ไข" = apply + เขียนทับไฟล์เดิม (สำรอง) · ยกเลิก = ทิ้ง
- แปลงเป็น Word: `cpp/src/docx.cpp` (บรรทัดจาก EditSession::objects → ย่อหน้า/จัดกึ่งกลาง/ตัวหนา/สี + รูป) → `core/src/docx.rs` เขียน .docx · ตรวจด้วย `textutil -convert txt`
- แปลง Word ตรวจกับ MS Word จริงแล้ว (2026-10-26 เย็น): ไม่ขึ้น Compatibility Mode (settings.xml compat 15) · ตัวหนา/ขนาดจริง/ตารางมีเส้น ✓
  · PDFium คืนชื่อฟอนต์ว่างสำหรับ Type3 (Chrome/Skia) → `pdfcore_prepare_export` ทำสำเนาชั่วคราว mirror FontDescriptor /FontName → /BaseFont ก่อนเปิด EditSession
  · ขนาดฟอนต์จริง = FPDFTextObj_GetFontSize × sqrt(det(FPDFPageObj_GetMatrix)) (Skia ใส่ scale 0.75 ไว้ใน CTM) · line builder แยกบรรทัดเมื่อ bold ต่างกัน
  · `docx.cpp` จัดกลุ่ม "แถว" (บรรทัดซ้อนแนวตั้ง) → ตารางเมื่อ ≥2 แถวติดกันมี ≥2 เซลล์ซ้าย-ตรงกัน (เส้นขอบถ้ามี path บาง ๆ ทับ) · แถวเดียวหลายเซลล์ = tab stops
  · debug: `"PDF DLP" --docxjson <pdf>` พิมพ์ JSON ที่ส่งให้ Rust · `PDFDLP_DEBUG_FONTS=1` พิมพ์ฟอนต์ต่อ object
- writeBack ต้อง atomic (copy เป็น .pdfdlp-tmp แล้ว rename) — รุ่นก่อนลบไฟล์เดิมก่อน copy ทำไฟล์หายได้
- Windows/MSVC: `near`/`far`/`min`/`max` เป็น macro ของ windows.h → ห้ามใช้เป็นชื่อตัวแปร, ตั้ง NOMINMAX
- ห้าม `pkill` แอปตอนเจ้าของกำลังใช้งาน — build ใหม่แล้วบอกให้เขาเปิดใหม่เอง

## บทเรียน/กับดัก
- **lopdf**: `/Encrypt` ที่ lopdf เขียนไม่มี `/Length 256` + CF `/Length 32` → poppler/pdf.js/lopdf อ่านไม่ออก (qpdf ทน) → patch ใน `pdfops::encrypt` · เปิดไฟล์เข้ารหัสต้อง `Document::load_with_password` (`load()`+`decrypt()` ได้ objects ว่าง) · หลัง prune ต้อง `renumber_objects`
- **Qt**: QSS `* { font-family }` ทับ `setFont()` → ไอคอน Font Awesome ใน QLabel ต้องตั้ง font-family ใน stylesheet (`FA::label`) · macro ชื่อ `T` ชน template ของ QtConcurrent → ใช้ `TXT()` · PDFium ไม่ thread-safe → ทุก FPDF_* ผ่าน `pdfiumLock()` · PDFium prebuilt install name `./libpdfium.dylib` → copy เข้า bundle + `install_name_tool` (อยู่ใน build.sh)
- **edition 2024**: `#[unsafe(no_mangle)]` · if-let temporaries ต่างจาก 2021 (core ใช้ 2024)
- auto mode บล็อก `gh repo create`/push → ผู้ใช้ push เอง: `gh repo create snpeerapun/pdf-dlp --private --source=. --push`

## งานค้าง / ขั้นต่อไป
- ยืนยันการอ่าน label จากแคช Windows (`%LocalAppData%\Microsoft\Office\CLP`, `…\MSIP`) บนเครื่อง Windows จริง · ทางเลือก Microsoft Graph `sensitivityLabels`
- Windows: push แล้วดู Actions → ได้ `PDF-DLP-win64.zip` · ทดสอบคลิกขวา Explorer + single instance บน Windows จริง
- mac: เซ็น/notarize ตาม skill dropacross · dmg + หน้า downloads (skill oneable-downloads)
- ทำแล้วรอบ 2026-10-26 (บ่าย): home card recent+pinned, toolbox บน, viewbar, checkbox ไฟล์, email แนบไฟล์ (Outlook/Mail), edit mode (text/image/line/box/field/checkbox/form-fill, 25 ฟอนต์ TH/EN), present (F5), classification auto write-back (สำรองที่ AppData/backup)
- ฟีเจอร์ Foxit ที่ยังไม่มี: ค้นหาข้อความ/เลือกข้อความ (PDFium text API มีแล้วใน `PdfDoc::pageText`), bookmark/outline, ไฟล์แนบ, annotation interactive, ลายเซ็นดิจิทัล, OCR, แปลงเป็น Word/Excel, เปรียบเทียบไฟล์, แก้ข้อความเดิม, เข้ารหัสด้วย certificate, กรอกฟอร์ม
