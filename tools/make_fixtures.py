#!/usr/bin/env python3
"""Generate test PDFs (Thai + English, with fake sensitive data) via Chrome headless.
Usage: python3 tools/make_fixtures.py  -> fixtures/*.pdf
"""
import os, subprocess, random, tempfile, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "fixtures"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

def thai_id():
    random.seed(42)
    d = [random.randint(1, 8)] + [random.randint(0, 9) for _ in range(11)]
    s = sum(d[i] * (13 - i) for i in range(12))
    d.append((11 - s % 11) % 10)
    x = "".join(map(str, d))
    return f"{x[0]}-{x[1:5]}-{x[5:10]}-{x[10:12]}-{x[12]}"

TID = thai_id()
PAGES = {
 "hr-confidential.pdf": [
  f"""<h1>บริษัท วันเอเบิล จำกัด — เอกสารฝ่ายบุคคล</h1>
  <p class=tag>CONFIDENTIAL — ห้ามเผยแพร่ภายนอก</p>
  <h2>ประวัติพนักงาน</h2>
  <table><tr><th>ชื่อ</th><td>นายสมชาย ใจดี</td></tr>
  <tr><th>เลขบัตรประชาชน</th><td>{TID}</td></tr>
  <tr><th>โทรศัพท์</th><td>081-234-5678</td></tr>
  <tr><th>อีเมล</th><td>somchai.j@example.co.th</td></tr>
  <tr><th>เงินเดือน</th><td>45,000 บาท</td></tr>
  <tr><th>บัญชีธนาคาร</th><td>123-4-56789-0 (กสิกรไทย)</td></tr></table>""",
  """<h1>Page 2 — Payment details</h1>
  <p>Corporate card: 4111 1111 1111 1111 (exp 12/28)</p>
  <p>Passport: AB1234567 · IBAN: GB82 WEST 1234 5698 7654 32</p>
  <p>Server: 10.20.30.40 internal use only</p>
  <p>ประวัติการรักษา: ตรวจสุขภาพประจำปี ผลปกติ</p>""",
  """<h1>หน้า 3 — ภาคผนวก</h1><p>ข้อความทั่วไป ไม่มีข้อมูลอ่อนไหว สำหรับทดสอบการหมุนหน้าและการตัดไฟล์</p>
  <p>Lorem ipsum dolor sit amet, consectetur adipiscing elit.</p>""",
 ],
 "public-brochure.pdf": [
  """<h1>โบรชัวร์สินค้า — OneAble Cloud</h1><p>บริการคลาวด์สำหรับธุรกิจไทย เริ่มต้น 299 บาท/เดือน</p>
  <p>ติดต่อ: sales@oneable.co.th · โทร 02-123-4567</p>""",
  """<h1>Page 2 — Features</h1><ul><li>Backup รายวัน</li><li>รองรับภาษาไทย</li><li>Support 24/7</li></ul>""",
 ],
}
CSS = """<style>@page{size:A4;margin:18mm}body{font-family:'Noto Sans Thai','Sarabun','Thonburi',sans-serif;font-size:14pt;color:#111}
h1{font-size:22pt}h2{font-size:17pt}.tag{color:#b00;font-weight:700}table{border-collapse:collapse}th,td{border:1px solid #999;padding:6px 10px;text-align:left}
.pb{page-break-after:always}</style>"""
OUT.mkdir(exist_ok=True)
for name, pages in PAGES.items():
    html = "<!doctype html><html><head><meta charset=utf-8>" + CSS + "</head><body>" + \
        "".join(f"<div class='{'pb' if i < len(pages)-1 else ''}'>{p}</div>" for i, p in enumerate(pages)) + "</body></html>"
    src = OUT / (name + ".html")
    src.write_text(html, encoding="utf-8")
    dst = OUT / name
    r = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--user-data-dir=/tmp/pdfdlp-chrome",
                    f"--print-to-pdf={dst}", f"file://{src}"], capture_output=True, text=True)
    src.unlink()
    if not dst.exists():
        raise SystemExit(f"chrome failed ({r.returncode}): {r.stderr[-800:]}")
    print("wrote", dst, dst.stat().st_size, "bytes")
print("thai id used:", TID)
