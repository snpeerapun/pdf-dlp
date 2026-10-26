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
