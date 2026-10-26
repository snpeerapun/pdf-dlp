#pragma once
// Document model + PDFium access. PDFium is not thread-safe: every FPDF_* call goes through pdfiumLock().
#include <QtCore>
#include <QtGui>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <memory>
#include <mutex>
#include <algorithm>

inline std::mutex& pdfiumLock() { static std::mutex m; return m; }

struct PdfDoc {
    FPDF_DOCUMENT doc = nullptr;
    std::vector<QSizeF> sizes; // points, as displayed (rotation applied by PDFium)
    ~PdfDoc() { if (doc) { std::lock_guard<std::mutex> l(pdfiumLock()); FPDF_CloseDocument(doc); } }
    static std::shared_ptr<PdfDoc> open(const QString& path, const QString& password, bool* needsPassword) {
        std::lock_guard<std::mutex> l(pdfiumLock());
        auto d = std::make_shared<PdfDoc>();
        d->doc = FPDF_LoadDocument(path.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        if (needsPassword) *needsPassword = false;
        if (!d->doc) { if (needsPassword && FPDF_GetLastError() == FPDF_ERR_PASSWORD) *needsPassword = true; return nullptr; }
        const int n = FPDF_GetPageCount(d->doc);
        for (int i = 0; i < n; i++) { FS_SIZEF sz{}; FPDF_GetPageSizeByIndexF(d->doc, i, &sz); d->sizes.push_back(QSizeF(sz.width, sz.height)); }
        return d;
    }
    int pages() const { return int(sizes.size()); }
    /// Render page `index` at `widthPx` pixels wide (white background).
    QImage render(int index, int widthPx, qreal dpr = 1.0, bool annotations = true) const {
        if (index < 0 || index >= pages()) return {};
        const QSizeF s = sizes[index];
        const int w = std::max(1, int(widthPx * dpr)), h = std::max(1, int(widthPx * dpr * s.height() / s.width()));
        QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
        {
            std::lock_guard<std::mutex> l(pdfiumLock());
            FPDF_PAGE page = FPDF_LoadPage(doc, index);
            if (!page) return {};
            FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, out.bits(), int(out.bytesPerLine()));
            FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
            FPDF_RenderPageBitmap(bmp, page, 0, 0, w, h, 0, (annotations ? FPDF_ANNOT : 0) | FPDF_LCD_TEXT);
            FPDFBitmap_Destroy(bmp); FPDF_ClosePage(page);
        }
        out.setDevicePixelRatio(dpr);
        return out;
    }
    struct SearchHit { int page = 0; QList<QRectF> rects; }; // rects in viewer points (scale 1, y down)
    /// Find all occurrences of `q` (PDFium text search) across pages; rects are merged per hit.
    QList<SearchHit> search(const QString& q, bool caseSensitive = false) const {
        QList<SearchHit> out; if (q.trimmed().isEmpty()) return out;
        std::lock_guard<std::mutex> l(pdfiumLock());
        const std::u16string w = q.toStdU16String();
        for (int i = 0; i < pages(); i++) {
            FPDF_PAGE page = FPDF_LoadPage(doc, i); if (!page) continue;
            FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
            if (tp) {
                FPDF_SCHHANDLE h = FPDFText_FindStart(tp, reinterpret_cast<FPDF_WIDESTRING>(w.c_str()), caseSensitive ? FPDF_MATCHCASE : 0, 0);
                const QSizeF s = sizes[i];
                while (h && FPDFText_FindNext(h)) {
                    const int start = FPDFText_GetSchResultIndex(h), count = FPDFText_GetSchCount(h);
                    const int nr = FPDFText_CountRects(tp, start, count); SearchHit hit; hit.page = i;
                    for (int r = 0; r < nr; r++) {
                        double l2, t2, r2, b2; if (!FPDFText_GetRect(tp, r, &l2, &t2, &r2, &b2)) continue;
                        int ax, ay, bx, by; FPDF_PageToDevice(page, 0, 0, int(s.width()), int(s.height()), 0, l2, t2, &ax, &ay); FPDF_PageToDevice(page, 0, 0, int(s.width()), int(s.height()), 0, r2, b2, &bx, &by);
                        hit.rects << QRectF(QPointF(ax, ay), QPointF(bx, by)).normalized().adjusted(-1, -1, 1, 1);
                    }
                    // merge per-glyph boxes on the same line into one highlight
                    std::sort(hit.rects.begin(), hit.rects.end(), [](const QRectF& a, const QRectF& b) { return a.left() < b.left(); });
                    QList<QRectF> merged;
                    for (const QRectF& r : hit.rects) {
                        if (!merged.isEmpty()) { QRectF& m = merged.last(); const double ov = std::min(m.bottom(), r.bottom()) - std::max(m.top(), r.top()); if (ov > std::min(m.height(), r.height()) * 0.5 && r.left() <= m.right() + 4) { m = m.united(r); continue; } }
                        merged << r;
                    }
                    hit.rects = merged;
                    if (!hit.rects.isEmpty()) out << hit;
                }
                if (h) FPDFText_FindClose(h);
                FPDFText_ClosePage(tp);
            }
            FPDF_ClosePage(page);
        }
        return out;
    }
    QString pageText(int index) const {
        std::lock_guard<std::mutex> l(pdfiumLock());
        FPDF_PAGE page = FPDF_LoadPage(doc, index); if (!page) return {};
        FPDF_TEXTPAGE tp = FPDFText_LoadPage(page); QString s;
        if (tp) { const int n = FPDFText_CountChars(tp); std::vector<unsigned short> buf(n + 1); FPDFText_GetText(tp, 0, n, buf.data()); s = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.data())); FPDFText_ClosePage(tp); }
        FPDF_ClosePage(page); return s;
    }
};

struct Document {
    QString name, path, workPath, password;
    std::shared_ptr<PdfDoc> pdf;
    QJsonObject info;      // from core: pages, encrypted, classification, has_marks, metadata, msip
    bool modified = false; bool checked = false;
    QStringList history;   // previous workPaths (undo)
    int pages() const { return pdf ? pdf->pages() : 0; }
    QString classification() const { const QString c = info.value("classification").toString(); return c.isEmpty() ? info.value("msip").toObject().value("name").toString() : c; }
};
