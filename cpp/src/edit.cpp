#include "edit.h"
#include <cmath>
#include "doc.h"
#include <fpdf_save.h>
#include <fpdf_fwlevent.h>

static bool asciiOnly(const QString& s) { for (QChar c : s) if (c.unicode() > 0x7E) return false; return true; }

EditSession::EditSession(const QString& path, const QString& password, const QList<QPair<QString, QByteArray>>& fonts) : fontList(fonts) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    doc = FPDF_LoadDocument(path.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
    if (!doc) { error = "open failed"; return; }
    ffi.version = 1;
    form = FPDFDOC_InitFormFillEnvironment(doc, &ffi);
    if (form) { FPDF_SetFormFieldHighlightColor(form, FPDF_FORMFIELD_UNKNOWN, 0xCFE0FF); FPDF_SetFormFieldHighlightAlpha(form, 70); }
    const int n = FPDF_GetPageCount(doc);
    for (int i = 0; i < n; i++) { FS_SIZEF sz{}; FPDF_GetPageSizeByIndexF(doc, i, &sz); sizes.push_back(QSizeF(sz.width, sz.height)); }
}
EditSession::~EditSession() {
    std::lock_guard<std::mutex> l(pdfiumLock());
    for (auto& [i, p] : pagesOpen) { if (form) FORM_OnBeforeClosePage(p, form); FPDF_ClosePage(p); }
    for (auto& [n, f] : fontsLoaded) FPDFFont_Close(f);
    if (form) FPDFDOC_ExitFormFillEnvironment(form);
    if (doc) FPDF_CloseDocument(doc);
}
FPDF_PAGE EditSession::pageHandle(int page) {
    auto it = pagesOpen.find(page); if (it != pagesOpen.end()) return it->second;
    FPDF_PAGE p = FPDF_LoadPage(doc, page); if (p && form) FORM_OnAfterLoadPage(p, form);
    pagesOpen[page] = p; return p;
}
QPointF EditSession::toPage(int page, QPointF v) {
    const QSizeF s = sizes[page]; double px = 0, py = 0;
    FPDF_DeviceToPage(pageHandle(page), 0, 0, int(s.width()), int(s.height()), 0, int(v.x()), int(v.y()), &px, &py);
    return QPointF(px, py);
}
QPointF EditSession::toViewer(int page, double px, double py) {
    const QSizeF s = sizes[page]; int dx = 0, dy = 0;
    FPDF_PageToDevice(pageHandle(page), 0, 0, int(s.width()), int(s.height()), 0, px, py, &dx, &dy);
    return QPointF(dx, dy);
}
QImage EditSession::render(int page, int widthPx, qreal dpr) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    const QSizeF s = sizes[page];
    const int w = std::max(1, int(widthPx * dpr)), h = std::max(1, int(widthPx * dpr * s.height() / s.width()));
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    FPDF_PAGE p = pageHandle(page); if (!p) return {};
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, out.bits(), int(out.bytesPerLine()));
    FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bmp, p, 0, 0, w, h, 0, FPDF_ANNOT | FPDF_LCD_TEXT);
    if (form) FPDF_FFLDraw(form, bmp, p, 0, 0, w, h, 0, FPDF_ANNOT);
    FPDFBitmap_Destroy(bmp);
    out.setDevicePixelRatio(dpr); return out;
}
QList<EditObject> EditSession::objects(int page) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    QList<EditObject> raw; FPDF_PAGE p = pageHandle(page); if (!p) return raw;
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(p);
    const int n = FPDFPage_CountObjects(p);
    for (int i = 0; i < n; i++) {
        if (hidden.count(page) && hidden[page].count(i)) continue;
        FPDF_PAGEOBJECT o = FPDFPage_GetObject(p, i); EditObject e; e.index = i; e.type = FPDFPageObj_GetType(o); e.members << i;
        float l = 0, b = 0, r = 0, t = 0; if (!FPDFPageObj_GetBounds(o, &l, &b, &r, &t)) continue;
        const QPointF a = toViewer(page, l, b), c2 = toViewer(page, r, t);
        e.rect = QRectF(a, c2).normalized();
        if (e.type == FPDF_PAGEOBJ_TEXT) {
            const unsigned long len = FPDFTextObj_GetText(o, tp, nullptr, 0);
            if (len > 0) { std::vector<unsigned short> buf(len / 2 + 1); FPDFTextObj_GetText(o, tp, buf.data(), len); e.text = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.data())); }
            float fs = 0; FPDFTextObj_GetFontSize(o, &fs); FS_MATRIX m{1, 0, 0, 1, 0, 0}; FPDFPageObj_GetMatrix(o, &m); const double sc = std::sqrt(std::abs(double(m.a) * m.d - double(m.b) * m.c)); e.fontSize = float(fs * (sc > 0.01 && sc < 100 ? sc : 1.0)); // effective size in points (Skia PDFs put the scale in the matrix)
            if (FPDF_FONT f = FPDFTextObj_GetFont(o)) { char name[128] = {0}; FPDFFont_GetBaseFontName(f, name, sizeof(name)); const QString nm = QString(name); e.subsetFont = nm.contains('+'); int wt = FPDFFont_GetWeight(f); char fam[128] = {0}; FPDFFont_GetFamilyName(f, fam, sizeof(fam)); const int flags = FPDFFont_GetFlags(f); e.bold = wt >= 600 || nm.contains("Bold", Qt::CaseInsensitive) || QString(fam).contains("Bold", Qt::CaseInsensitive) || (flags & (1 << 18)); static const bool dbg = qEnvironmentVariableIsSet("PDFDLP_DEBUG_FONTS"); if (dbg) fprintf(stderr, "obj %d size=%.2f base='%s' fam='%s' wt=%d flags=%d h=%.2f text='%s'\n", i, fs, name, fam, wt, flags, e.rect.height(), e.text.left(12).toUtf8().constData()); }
            unsigned int R = 0, G = 0, B = 0, A = 255; if (FPDFPageObj_GetFillColor(o, &R, &G, &B, &A)) e.color = QString("#%1%2%3").arg(R, 2, 16, QChar('0')).arg(G, 2, 16, QChar('0')).arg(B, 2, 16, QChar('0'));
        }
        if (e.rect.width() < 0.3 && e.rect.height() < 0.3) continue;
        raw << e;
    }
    // Merge per-glyph text objects (Chrome/Skia emit one object per glyph, marks as separate tiny objects) into lines.
    QList<EditObject> out; QList<EditObject> texts;
    for (const EditObject& e : raw) { if (e.type == FPDF_PAGEOBJ_TEXT) texts << e; else out << e; }
    std::sort(texts.begin(), texts.end(), [](const EditObject& a, const EditObject& b) { return a.rect.left() < b.rect.left(); });
    struct Line { EditObject obj; QList<EditObject> glyphs; };
    QList<Line> lines;
    for (const EditObject& g : texts) {
        Line* best = nullptr; double bestGap = 1e18;
        for (Line& L : lines) {
            const double hh = std::max(L.obj.rect.height(), g.rect.height());
            const bool mark = g.rect.height() < hh * 0.4 || g.text.trimmed().isEmpty();                 // combining mark / tiny glyph piece
            const double overlap = std::min(L.obj.rect.bottom(), g.rect.bottom()) - std::max(L.obj.rect.top(), g.rect.top());
            if (mark) { if (g.rect.bottom() < L.obj.rect.top() - hh * 0.9 || g.rect.top() > L.obj.rect.bottom() + hh * 0.5) continue; }
            else if (overlap < std::min(L.obj.rect.height(), g.rect.height()) * 0.4) continue;         // not on the same line
            if (std::abs(L.obj.fontSize - g.fontSize) > 0.01 + L.obj.fontSize * 0.3) continue;           // different size = different run
            if (L.obj.bold != g.bold) continue;                                                             // bold vs regular = different run
            const double gap = g.rect.left() - L.obj.rect.right();
            if (gap > hh * 1.6 || g.rect.right() < L.obj.rect.left() - hh * 1.6) continue;
            if (std::abs(gap) < bestGap) { bestGap = std::abs(gap); best = &L; }
        }
        if (best) { best->obj.rect = best->obj.rect.united(g.rect); best->obj.members << g.index; best->glyphs << g; }
        else { Line L; L.obj = g; L.glyphs << g; lines << L; }
    }
    for (Line& L : lines) {
        if (L.glyphs.size() > 1) {
            // line text = base glyphs only (combining-mark objects are short and repeat the base text)
            const double lineH = L.obj.rect.height(); QString t;
            auto hasMark = [](const QString& x) { for (QChar ch : x) { const ushort u = ch.unicode(); if (u == 0x0E31 || (u >= 0x0E34 && u <= 0x0E3A) || (u >= 0x0E47 && u <= 0x0E4E)) return true; } return false; };
            for (int gi = 0; gi < L.glyphs.size(); gi++) {
                const EditObject& g = L.glyphs[gi]; const QString gt = g.text.trimmed();
                if (gt.isEmpty()) { if (!g.text.isEmpty() && !t.endsWith(' ')) t += ' '; continue; }
                // a combining-mark object (tiny box) repeats the "base+mark" text of its base glyph (before or after it in x order) → skip it
                if (g.rect.height() < lineH * 0.45 && hasMark(gt)) {
                    bool dup = t.endsWith(gt);
                    for (int k = gi + 1; k <= gi + 2 && k < L.glyphs.size() && !dup; k++) if (L.glyphs[k].text.trimmed() == gt && L.glyphs[k].rect.height() >= lineH * 0.45) dup = true;
                    if (dup) continue;
                }
                t += g.text;
            }
            if (t.trimmed().isEmpty() && tp) {
                const QPointF a = toPage(page, L.obj.rect.topLeft()), b = toPage(page, L.obj.rect.bottomRight());
                const double left = std::min(a.x(), b.x()) - 0.5, right = std::max(a.x(), b.x()) + 0.5, top = std::max(a.y(), b.y()) + 0.5, bottom = std::min(a.y(), b.y()) - 0.5;
                const int cnt = FPDFText_GetBoundedText(tp, left, top, right, bottom, nullptr, 0);
                if (cnt > 0) { std::vector<unsigned short> buf(cnt + 1); FPDFText_GetBoundedText(tp, left, top, right, bottom, buf.data(), cnt); t = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.data()), cnt); }
            }
            L.obj.text = t.trimmed();
            std::sort(L.obj.members.begin(), L.obj.members.end());
        }
        out << L.obj;
    }
    if (tp) FPDFText_ClosePage(tp);
    std::sort(out.begin(), out.end(), [](const EditObject& a, const EditObject& b) { return a.index < b.index; });
    return out;
}
EditObject EditSession::objectAt(int page, QPointF v) {
    EditObject best; double bestArea = 1e18;
    for (const EditObject& e : objects(page)) { if (e.type == FPDF_PAGEOBJ_PATH && e.rect.width() * e.rect.height() > sizes[page].width() * sizes[page].height() * 0.85) continue; if (e.rect.adjusted(-2, -2, 2, 2).contains(v)) { const double a = e.rect.width() * e.rect.height(); if (a < bestArea) { bestArea = a; best = e; } } }
    return best;
}
void EditSession::whiteout(int page, QRectF r) {
    FPDF_PAGE p = pageHandle(page); if (!p) return;
    r = r.adjusted(-1.0, -1.0, 1.0, 1.0);
    const QPointF a = toPage(page, r.topLeft()), b = toPage(page, r.bottomRight());
    FPDF_PAGEOBJECT o = FPDFPageObj_CreateNewRect(float(std::min(a.x(), b.x())), float(std::min(a.y(), b.y())), float(std::abs(b.x() - a.x())), float(std::abs(b.y() - a.y())));
    FPDFPageObj_SetFillColor(o, 255, 255, 255, 255); FPDFPath_SetDrawMode(o, FPDF_FILLMODE_WINDING, 0);
    FPDFPage_InsertObject(p, o);
}
static EditObject findObj(const QList<EditObject>& l, int index) { for (const EditObject& e : l) if (e.index == index) return e; return {}; }
void EditSession::move(int page, int index, QPointF d) {
    const EditObject e = findObj(objects(page), index); if (e.index < 0) return;
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); FPDF_PAGEOBJECT o = FPDFPage_GetObject(p, index); if (!o) return;
    const QPointF a = toPage(page, QPointF(0, 0)), b = toPage(page, d); const double dx = b.x() - a.x(), dy = b.y() - a.y();
    FPDF_PAGEOBJECT n = nullptr;
    if (e.type == FPDF_PAGEOBJ_TEXT) {
        // leftmost glyph carries the line origin; always re-typeset with an embedded font (original CID fonts cannot be re-encoded reliably)
        int first = e.members.first(); double fx = 1e18; for (int mi : e.members) { FPDF_PAGEOBJECT g = FPDFPage_GetObject(p, mi); float l2, b2, r2, t2; if (g && FPDFPageObj_GetBounds(g, &l2, &b2, &r2, &t2) && l2 < fx) { fx = l2; first = mi; } }
        FPDF_PAGEOBJECT fo = FPDFPage_GetObject(p, first);
        float fs = 12; FPDFTextObj_GetFontSize(fo, &fs); FS_MATRIX m{}; FPDFPageObj_GetMatrix(fo, &m); unsigned int R = 0, G = 0, B = 0, A = 255; FPDFPageObj_GetFillColor(fo, &R, &G, &B, &A);
        FPDF_FONT f = fontFor(QString()); if (!f) return;
        n = FPDFPageObj_CreateTextObj(doc, f, fs); const std::u16string w = e.text.toStdU16String(); FPDFText_SetText(n, reinterpret_cast<FPDF_WIDESTRING>(w.c_str()));
        m.e += float(dx); m.f += float(dy); FPDFPageObj_SetMatrix(n, &m); FPDFPageObj_SetFillColor(n, R, G, B, A);
    } else if (e.type == FPDF_PAGEOBJ_IMAGE) {
        FPDF_BITMAP bmp = FPDFImageObj_GetBitmap(o); if (!bmp) return;
        FS_MATRIX m{}; FPDFPageObj_GetMatrix(o, &m); n = FPDFPageObj_NewImageObj(doc); FPDF_PAGE pages[1] = {p}; FPDFImageObj_SetBitmap(pages, 1, n, bmp); FPDFBitmap_Destroy(bmp);
        m.e += float(dx); m.f += float(dy); FPDFPageObj_SetMatrix(n, &m);
    } else if (e.type == FPDF_PAGEOBJ_PATH) {
        const int ns = FPDFPath_CountSegments(o); if (ns <= 0) return;
        float sx = 0, sy = 0; bool started = false;
        for (int si = 0; si < ns; si++) {
            FPDF_PATHSEGMENT seg = FPDFPath_GetPathSegment(o, si); float x, y; FPDFPathSegment_GetPoint(seg, &x, &y); const int st = FPDFPathSegment_GetType(seg);
            if (!started) { n = FPDFPageObj_CreateNewPath(x, y); started = true; sx = x; sy = y; continue; }
            if (st == FPDF_SEGMENT_MOVETO) FPDFPath_MoveTo(n, x, y);
            else if (st == FPDF_SEGMENT_LINETO) FPDFPath_LineTo(n, x, y);
            else if (st == FPDF_SEGMENT_BEZIERTO) { // three consecutive bezier points
                if (si + 2 < ns) { float x2, y2, x3, y3; FPDFPathSegment_GetPoint(FPDFPath_GetPathSegment(o, si + 1), &x2, &y2); FPDFPathSegment_GetPoint(FPDFPath_GetPathSegment(o, si + 2), &x3, &y3); FPDFPath_BezierTo(n, x, y, x2, y2, x3, y3); si += 2; }
            }
            if (FPDFPathSegment_GetClose(seg)) FPDFPath_Close(n);
        }
        (void)sx; (void)sy;
        if (!n) return;
        FS_MATRIX m{}; FPDFPageObj_GetMatrix(o, &m); FPDFPageObj_SetMatrix(n, &m);
        unsigned int R = 0, G = 0, B = 0, A = 255; if (FPDFPageObj_GetFillColor(o, &R, &G, &B, &A)) FPDFPageObj_SetFillColor(n, R, G, B, A);
        if (FPDFPageObj_GetStrokeColor(o, &R, &G, &B, &A)) FPDFPageObj_SetStrokeColor(n, R, G, B, A);
        float sw = 1; if (FPDFPageObj_GetStrokeWidth(o, &sw)) FPDFPageObj_SetStrokeWidth(n, sw);
        int fill = 0; FPDF_BOOL stroke = 0; if (FPDFPath_GetDrawMode(o, &fill, &stroke)) FPDFPath_SetDrawMode(n, fill, stroke);
        FPDFPageObj_Transform(n, 1, 0, 0, 1, dx, dy);
    } else { lastNote = "unsupported"; return; }
    whiteout(page, e.rect); for (int mi : e.members) hidden[page].insert(mi);
    FPDFPage_InsertObject(p, n); FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page);
}
void EditSession::remove(int page, int index) {
    const EditObject e = findObj(objects(page), index); if (e.index < 0) return;
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); if (!p) return;
    whiteout(page, e.rect); for (int mi : e.members) hidden[page].insert(mi);
    FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page);
}
FPDF_FONT EditSession::fontFor(const QString& name) {
    if (fontList.isEmpty()) return nullptr;
    QString key = name; const QByteArray* data = nullptr;
    for (auto& f : fontList) if (f.first == name) data = &f.second;
    if (!data) { key = fontList.first().first; data = &fontList.first().second; }
    auto it = fontsLoaded.find(key); if (it != fontsLoaded.end()) return it->second;
    FPDF_FONT f = FPDFText_LoadFont(doc, reinterpret_cast<const uint8_t*>(data->constData()), uint32_t(data->size()), FPDF_FONT_TRUETYPE, 1);
    if (f) fontsLoaded[key] = f; return f;
}
bool EditSession::setText(int page, int index, const QString& text, const QString& fontName) {
    const EditObject e = findObj(objects(page), index); if (e.index < 0 || e.type != FPDF_PAGEOBJ_TEXT) return false;
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); if (!p) return false;
    int first = e.members.first(); double fx = 1e18; for (int mi : e.members) { FPDF_PAGEOBJECT g = FPDFPage_GetObject(p, mi); float l2, b2, r2, t2; if (g && FPDFPageObj_GetBounds(g, &l2, &b2, &r2, &t2) && l2 < fx) { fx = l2; first = mi; } }
    FPDF_PAGEOBJECT o = FPDFPage_GetObject(p, first); if (!o) return false;
    float fs = 12; FPDFTextObj_GetFontSize(o, &fs); FS_MATRIX m{}; FPDFPageObj_GetMatrix(o, &m); unsigned int R = 0, G = 0, B = 0, A = 255; FPDFPageObj_GetFillColor(o, &R, &G, &B, &A);
    FPDF_FONT f = fontFor(fontName); if (!f) return false;
    FPDF_PAGEOBJECT n = FPDFPageObj_CreateTextObj(doc, f, fs); if (!n) return false;
    const std::u16string w = text.toStdU16String(); FPDFText_SetText(n, reinterpret_cast<FPDF_WIDESTRING>(w.c_str()));
    FPDFPageObj_SetMatrix(n, &m); FPDFPageObj_SetFillColor(n, R, G, B, A);
    whiteout(page, e.rect); for (int mi : e.members) hidden[page].insert(mi);
    FPDFPage_InsertObject(p, n); FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page); return true;
}
void EditSession::addText(int page, QPointF v, const QString& text, double fontSize, const QColor& color, const QString& fontName) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); FPDF_FONT nf = fontFor(fontName); if (!p || !nf) return;
    const QPointF pp = toPage(page, v);
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); i++) {
        FPDF_PAGEOBJECT n = FPDFPageObj_CreateTextObj(doc, nf, float(fontSize)); if (!n) return;
        const std::u16string w = lines[i].toStdU16String(); FPDFText_SetText(n, reinterpret_cast<FPDF_WIDESTRING>(w.c_str()));
        FPDFPageObj_SetFillColor(n, color.red(), color.green(), color.blue(), 255);
        FPDFPageObj_Transform(n, 1, 0, 0, 1, pp.x(), pp.y() - i * fontSize * 1.35);
        FPDFPage_InsertObject(p, n);
    }
    FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page);
}
void EditSession::addImage(int page, QRectF v, const QImage& img) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); if (!p) return;
    QImage rgba = img.convertToFormat(QImage::Format_ARGB32);
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(rgba.width(), rgba.height(), FPDFBitmap_BGRA, rgba.bits(), int(rgba.bytesPerLine()));
    FPDF_PAGEOBJECT o = FPDFPageObj_NewImageObj(doc);
    FPDF_PAGE pages[1] = {p}; FPDFImageObj_SetBitmap(pages, 1, o, bmp); FPDFBitmap_Destroy(bmp);
    const QPointF a = toPage(page, v.bottomLeft()), b = toPage(page, v.topRight());
    const double w = std::abs(b.x() - a.x()), h = std::abs(b.y() - a.y());
    FS_MATRIX m{float(w), 0, 0, float(h), float(std::min(a.x(), b.x())), float(std::min(a.y(), b.y()))}; FPDFPageObj_SetMatrix(o, &m);
    FPDFPage_InsertObject(p, o); FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page);
}
void EditSession::addLine(int page, QPointF a, QPointF b, const QColor& color, double width) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); if (!p) return;
    const QPointF pa = toPage(page, a), pb = toPage(page, b);
    FPDF_PAGEOBJECT o = FPDFPageObj_CreateNewPath(float(pa.x()), float(pa.y()));
    FPDFPath_LineTo(o, float(pb.x()), float(pb.y()));
    FPDFPageObj_SetStrokeColor(o, color.red(), color.green(), color.blue(), 255); FPDFPageObj_SetStrokeWidth(o, float(width));
    FPDFPath_SetDrawMode(o, FPDF_FILLMODE_NONE, 1);
    FPDFPage_InsertObject(p, o); FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page);
}
void EditSession::addRect(int page, QRectF v, const QColor& stroke, double width, const QColor& fill, bool filled) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    FPDF_PAGE p = pageHandle(page); if (!p) return;
    const QPointF a = toPage(page, v.topLeft()), b = toPage(page, v.bottomRight());
    const float x = float(std::min(a.x(), b.x())), y = float(std::min(a.y(), b.y())), w = float(std::abs(b.x() - a.x())), h = float(std::abs(b.y() - a.y()));
    FPDF_PAGEOBJECT o = FPDFPageObj_CreateNewRect(x, y, w, h);
    FPDFPageObj_SetStrokeColor(o, stroke.red(), stroke.green(), stroke.blue(), 255); FPDFPageObj_SetStrokeWidth(o, float(width));
    if (filled) FPDFPageObj_SetFillColor(o, fill.red(), fill.green(), fill.blue(), fill.alpha());
    FPDFPath_SetDrawMode(o, filled ? FPDF_FILLMODE_WINDING : FPDF_FILLMODE_NONE, width > 0 ? 1 : 0);
    FPDFPage_InsertObject(p, o); FPDFPage_GenerateContent(p); dirty = true; dirtyPages.insert(page);
}
bool EditSession::hasFieldAt(int page, QPointF v) { std::lock_guard<std::mutex> l(pdfiumLock()); if (!form) return false; const QPointF pp = toPage(page, v); return FPDFPage_HasFormFieldAtPoint(form, pageHandle(page), pp.x(), pp.y()) >= 0; }
void EditSession::formMouse(int page, QPointF v, bool down) { std::lock_guard<std::mutex> l(pdfiumLock()); if (!form) return; const QPointF pp = toPage(page, v); if (down) FORM_OnLButtonDown(form, pageHandle(page), 0, pp.x(), pp.y()); else FORM_OnLButtonUp(form, pageHandle(page), 0, pp.x(), pp.y()); dirty = true; }
void EditSession::formChar(int page, uint ch) { std::lock_guard<std::mutex> l(pdfiumLock()); if (form) { FORM_OnChar(form, pageHandle(page), int(ch), 0); dirty = true; } }
void EditSession::formKey(int page, int key, int mods) { std::lock_guard<std::mutex> l(pdfiumLock()); if (form) { FORM_OnKeyDown(form, pageHandle(page), key, mods); dirty = true; } }
void EditSession::killFocus() { std::lock_guard<std::mutex> l(pdfiumLock()); if (form) FORM_ForceToKillFocus(form); }

struct Writer : FPDF_FILEWRITE { QFile* f; };
static int writeBlock(FPDF_FILEWRITE* w, const void* data, unsigned long size) { return static_cast<Writer*>(w)->f->write(static_cast<const char*>(data), qint64(size)) == qint64(size) ? 1 : 0; }
bool EditSession::save(const QString& outPath) {
    std::lock_guard<std::mutex> l(pdfiumLock());
    if (form) FORM_ForceToKillFocus(form);
    for (int pg : dirtyPages) if (auto it = pagesOpen.find(pg); it != pagesOpen.end()) FPDFPage_GenerateContent(it->second);
    QFile f(outPath); if (!f.open(QIODevice::WriteOnly)) { error = "cannot write"; return false; }
    Writer w{}; w.version = 1; w.WriteBlock = writeBlock; w.f = &f;
    const bool ok = FPDF_SaveAsCopy(doc, &w, 0); f.close();
    if (!ok) error = "save failed"; return ok;
}
