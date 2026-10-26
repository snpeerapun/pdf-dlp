# PDF DLP — คู่มือโปรเจกต์ (สำหรับ Claude/Codex session ถัดไป)

แอป desktop multi-platform: **PDF preview + tools (merge/split/หมุน/ลบ/แทรก/encrypt AES-256/redact/watermark/เลขหน้า/ครอป/บีบอัด/sanitize/รูป↔PDF)**
+ **Data Classification** แบบที่ผู้ใช้เลือก/เปลี่ยนชั้นความลับของไฟล์เอง — เขียน metadata ในรูปแบบ **Microsoft Purview (MSIP_Label_*)**
+ ประทับป้ายหัว/ท้าย/ลายน้ำที่ถอดออกได้ · ทำงานในเครื่อง 100% ไม่อัปโหลด

## สแตก (ตัดสินใจแล้ว — เจ้าของเลือก Tauri + Rust, ไม่เอา Flutter; ขอให้มีเวอร์ชัน C++ ทดลองเทียบความลื่น)
- UI: React 18 + Vite + TS (`src/`), ธีมมืดเดียว, Font Awesome, i18n th/en (`src/i18n.ts`)
- Preview: pdf.js v6 ใน webview — render queue (2 งานพร้อมกัน, ใกล้หน้าปัจจุบันก่อน), CSS-scale ระหว่างซูม, swap canvas ไม่กะพริบ (`src/lib/pdf.ts`, `src/components/Viewer.tsx`)
- Core: Rust (`src-tauri/src/pdfops.rs`) บน **lopdf 0.46** — ทุก mutation = `apply_ops(path, password, ops[]) → temp file` แล้ว viewer โหลดใหม่ (ต้นฉบับไม่ถูกแก้จนกว่า "บันทึกเป็น")
- Labels: `src-tauri/src/labels.rs` — อ่านจากแคช MIP/Office บน Windows (best-effort), เรียนรู้จากไฟล์ที่ติด label (PDF/OOXML), เขียน `MSIP_Label_{GUID}_Enabled/Name/SetDate/Method/ContentBits/ActionId/SiteId`
- Windows คลิกขวา: `src-tauri/src/winshell.rs` (HKCU registry, ไม่ต้อง admin) + single-instance + fileAssociations .pdf + `--action open|classify|encrypt <file>`
- ต้นแบบ C++: `cpp/` (Qt6 Widgets + PDFium prebuilt) — `./build.sh` แล้ว `build/pdfdlp_native.app/Contents/MacOS/pdfdlp_native <file.pdf>` (พิมพ์เวลาเรนเดอร์ต่อหน้าใน stderr)

## รัน / ทดสอบ
- `npm install` · `npx tauri dev` (เว็บอย่างเดียว: `npm run dev` แล้วเปิดไฟล์ผ่านปุ่ม — ops ที่ต้อง Rust ใช้ไม่ได้)
- Rust tests (ของจริง: merge/rotate/delete/reorder/extract/encrypt-decrypt/stamp/remove_marks/redact/crop/sanitize/images→pdf ตรวจด้วย qpdf+pdftotext): `cd src-tauri && cargo test --test ops`
- E2E ในแอปจริง (debug build): ขณะ `tauri dev` รัน เขียน `{"fixture":"<pdf>","out":"<dir>"}` ลง `/tmp/pdfdlp-selftest.json` → ผลที่ `<dir>/selftest-result.json` + `selftest-classified-p1.png`
- Browser harness (เรนเดอร์/ซูม): `cp tools/ui-test.mjs ~/Projects/animation/_x.mjs && (cd ~/Projects/animation && node _x.mjs)`
- fixtures: `python3 tools/make_fixtures.py` (Chrome headless, ไทย + ข้อมูลอ่อนไหวปลอม)

## บทเรียน/กับดักที่เจอแล้ว
- **lopdf เขียน /Encrypt ไม่มี `/Length 256` และ CF `/Length 32`** → poppler/pdf.js/lopdf เองอ่านไม่ออก (qpdf ทน) → patch dict หลัง `doc.encrypt()` (ดู `pdfops::encrypt`)
- **เปิดไฟล์เข้ารหัสต้องใช้ `Document::load_with_password`** — `load()` + `decrypt()` ได้ objects ว่าง (pages=0)
- หลัง `prune_objects` ต้อง `renumber_objects` ไม่งั้น qpdf เตือน Size ไม่ตรง
- pdf.js v6: `render({canvas, canvasContext, viewport, transform})` ห้ามเรนเดอร์ canvas เดิมซ้อน (throw "same canvas") → ใช้ canvas ใหม่แล้ว swap
- `.pages` เป็น flex column → `.page{flex:none}` ไม่งั้นหน้าถูกบีบสูง 0 (เจอจริง)
- PDFium prebuilt (bblanchon) install name `./libpdfium.dylib` → ต้อง copy ไว้ข้าง binary + `install_name_tool` (อยู่ใน cpp/build.sh)
- auto mode บล็อก `gh repo create`/push → ผู้ใช้ต้อง push เอง (`gh repo create snpeerapun/pdf-dlp --private --source=. --push`)

## งานค้าง / ขั้นต่อไป
- ยืนยันการอ่าน label จากแคช Windows บนเครื่อง Windows จริง (ทดสอบ regex กับไฟล์ใน `%LocalAppData%\Microsoft\Office\CLP`) · ทางเลือก: Microsoft Graph `sensitivityLabels` (ต้องมี app registration + สิทธิ์ InformationProtectionPolicy.Read)
- Windows exe ผ่าน GitHub Actions (`.github/workflows/build.yml`) หลัง push · notarize mac ตาม skill dropacross
- ฟีเจอร์ Foxit ที่ยังไม่มี: ลายเซ็นดิจิทัล, OCR, แปลงเป็น Word/Excel, เปรียบเทียบไฟล์, แก้ข้อความเดิม, เข้ารหัสด้วย certificate, กรอกฟอร์ม, annotation แบบ interactive
- ถ้าต้องการเรนเดอร์ native ใน Tauri: ใช้ `pdfium-render` crate + dylib เดียวกับ cpp/pdfium แล้วส่งภาพข้าม IPC
