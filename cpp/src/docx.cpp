#include "docx.h"
#include "edit.h"
#include <fpdf_edit.h>

namespace {
struct Ch { QChar ch; QRectF box; double size; bool bold; QString color; bool generated; };
struct Line { QList<Ch> chars; QRectF box; double size = 0; };
bool isThaiMark(QChar c) { const ushort u = c.unicode(); return u == 0x0E31 || (u >= 0x0E34 && u <= 0x0E3A) || (u >= 0x0E47 && u <= 0x0E4E); }
// Chrome/Skia PDFs duplicate "base+mark" pairs in the text stream; collapse X M X M -> X M.
QString dedupeThai(const QString& s) {
    QString o; o.reserve(s.size());
    for (int i = 0; i < s.size(); i++) {
        if (i + 3 < s.size() + 0 && i + 3 <= s.size() - 1 + 1 && i + 3 < s.size() + 1) {}
        if (o.size() >= 2 && isThaiMark(o[o.size() - 1]) && !isThaiMark(o[o.size() - 2]) && i + 1 < s.size() && s[i] == o[o.size() - 2] && s[i + 1] == o[o.size() - 1]) { i++; if (i + 1 < s.size() && s[i + 1] == ' ') i++; continue; }
        o += s[i];
    }
    return o;
}
QString hexColor(unsigned r, unsigned g, unsigned b) { return QString("#%1%2%3").arg(r, 2, 16, QChar('0')).arg(g, 2, 16, QChar('0')).arg(b, 2, 16, QChar('0')); }
}

namespace DocxExport {
QJsonObject extract(const PdfDoc& doc, EditSession* session) {
    QJsonArray pages;
    for (int pi = 0; pi < doc.pages(); pi++) {
        QList<Line> linesPre; // objects() locks internally
        pdfiumLock().lock(); FPDF_PAGE page = FPDF_LoadPage(doc.doc, pi); pdfiumLock().unlock(); if (!page) continue;
        const QSizeF ps = doc.sizes[pi]; const int W = int(ps.width()), H = int(ps.height());
        auto toV = [&](double x, double y) { int dx = 0, dy = 0; FPDF_PageToDevice(page, 0, 0, W, H, 0, x, y, &dx, &dy); return QPointF(dx, dy); };
        // ---- lines from page objects (same merge logic as the editor: robust for per-glyph PDFs) ----
        QList<Line> lines;
        if (session) {
            QList<EditObject> objs = session->objects(pi); std::sort(objs.begin(), objs.end(), [](const EditObject& x, const EditObject& y) { return std::abs(x.rect.top() - y.rect.top()) > 2 ? x.rect.top() < y.rect.top() : x.rect.left() < y.rect.left(); });
            for (const EditObject& e : objs) {
                if (e.type != FPDF_PAGEOBJ_TEXT || e.text.trimmed().isEmpty()) continue;
                Line ln; ln.box = e.rect; ln.size = e.rect.height() > 0 ? e.rect.height() * 0.78 : e.fontSize;
                QString t = e.text; // drop spaces injected after Thai vowel/mark objects (artifact of per-glyph PDFs)
                QString cleaned; for (int k = 0; k < t.size(); k++) { if (t[k] == ' ' && k > 0 && k + 1 < t.size() && (isThaiMark(t[k - 1]) || t[k - 1].unicode() == 0x0E33) && t[k + 1].unicode() >= 0x0E00 && t[k + 1].unicode() <= 0x0E7F) continue; cleaned += t[k]; }
                for (QChar ch : cleaned) ln.chars << Ch{ch, e.rect, ln.size, e.bold, e.color.isEmpty() ? "#000000" : e.color, false};
                lines << ln;
            }
        }
        // drop fragments (mark-only sub-lines) whose text is contained in a neighbouring line
        for (int i = lines.size() - 1; i >= 0; i--) {
            QString t; for (const Ch& c : lines[i].chars) t += c.ch; t = t.trimmed();
            auto textOf = [&](int k) { QString u; for (const Ch& c : lines[k].chars) u += c.ch; return u; };
            const bool dupPrev = i > 0 && t.size() <= 6 && textOf(i - 1).contains(t), dupNext = i + 1 < lines.size() && t.size() <= 6 && textOf(i + 1).contains(t);
            if (!t.isEmpty() && (dupPrev || dupNext)) lines.removeAt(i);
        }
        std::lock_guard<std::mutex> lk(pdfiumLock());
        // ---- paragraphs ----
        struct Para { QList<Line> lines; QRectF box; };
        QList<Para> paras;
        for (const Line& ln : lines) {
            if (!paras.isEmpty()) {
                Para& p = paras.last(); const Line& prev = p.lines.last();
                const double gap = ln.box.top() - prev.box.bottom(); const bool sizeSame = std::abs(ln.size - prev.size) < prev.size * 0.2;
                if (gap >= -prev.size * 0.3 && gap < prev.size * 0.75 && sizeSame && std::abs(ln.box.left() - p.box.left()) < prev.size * 3) { p.lines << ln; p.box = p.box.united(ln.box); continue; }
            }
            Para p; p.lines << ln; p.box = ln.box; paras << p;
        }
        // ---- blocks (paragraphs + images), ordered by top ----
        struct Blk { double top; QJsonObject obj; };
        QList<Blk> blocks; double prevBottom = 0;
        for (const Para& p : paras) {
            QJsonArray runs; QString text; QString runColor; bool runBold = false; double runSize = 0;
            auto flush = [&]() { if (!text.isEmpty()) { runs.append(QJsonObject{{"text", dedupeThai(text)}, {"size", runSize > 0 ? runSize : 12.0}, {"bold", runBold}, {"color", runColor}}); text.clear(); } };
            for (int li = 0; li < p.lines.size(); li++) {
                const Line& ln = p.lines[li];
                for (const Ch& ch : ln.chars) {
                    if (!text.isEmpty() && (ch.bold != runBold || std::abs(ch.size - runSize) > 0.5 || ch.color != runColor) && ch.ch != ' ') flush();
                    if (text.isEmpty()) { runBold = ch.bold; runSize = ch.size; runColor = ch.color; }
                    text += ch.ch;
                }
                if (li + 1 < p.lines.size()) { const QChar last = text.isEmpty() ? QChar(' ') : text.back(); const bool thai = last.unicode() >= 0x0E00 && last.unicode() <= 0x0E7F; if (!thai && !text.endsWith(' ')) text += ' '; }
            }
            flush();
            if (runs.isEmpty()) continue;
            const double cx = p.box.center().x(); QString align = "left";
            if (std::abs(cx - ps.width() / 2) < ps.width() * 0.03 && p.box.left() > ps.width() * 0.15) align = "center";
            else if (p.box.right() > ps.width() * 0.85 && p.box.left() > ps.width() * 0.45) align = "right";
            const double before = prevBottom > 0 ? std::clamp(p.box.top() - prevBottom - 4, 0.0, 36.0) : 0; prevBottom = p.box.bottom();
            blocks << Blk{p.box.top(), QJsonObject{{"type", "p"}, {"runs", runs}, {"align", align}, {"space_before", before}}};
        }
        const int no = FPDFPage_CountObjects(page);
        for (int i = 0; i < no; i++) {
            FPDF_PAGEOBJECT o = FPDFPage_GetObject(page, i); if (FPDFPageObj_GetType(o) != FPDF_PAGEOBJ_IMAGE) continue;
            float l2, b2, r2, t2; if (!FPDFPageObj_GetBounds(o, &l2, &b2, &r2, &t2)) continue;
            const QRectF box = QRectF(toV(l2, t2), toV(r2, b2)).normalized(); if (box.width() < 8 || box.height() < 8) continue;
            FPDF_BITMAP bmp = FPDFImageObj_GetBitmap(o); if (!bmp) continue;
            const int bw = FPDFBitmap_GetWidth(bmp), bh = FPDFBitmap_GetHeight(bmp), stride = FPDFBitmap_GetStride(bmp); const int fmt = FPDFBitmap_GetFormat(bmp);
            QImage img; if (fmt == FPDFBitmap_BGRA) img = QImage(static_cast<const uchar*>(FPDFBitmap_GetBuffer(bmp)), bw, bh, stride, QImage::Format_ARGB32).copy(); else if (fmt == FPDFBitmap_BGR) img = QImage(static_cast<const uchar*>(FPDFBitmap_GetBuffer(bmp)), bw, bh, stride, QImage::Format_BGR888).copy(); else if (fmt == FPDFBitmap_BGRx) img = QImage(static_cast<const uchar*>(FPDFBitmap_GetBuffer(bmp)), bw, bh, stride, QImage::Format_RGB32).copy(); else if (fmt == FPDFBitmap_Gray) img = QImage(static_cast<const uchar*>(FPDFBitmap_GetBuffer(bmp)), bw, bh, stride, QImage::Format_Grayscale8).copy();
            FPDFBitmap_Destroy(bmp); if (img.isNull()) continue;
            QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG");
            const double maxW = ps.width() - 100; const double w = std::min(box.width(), maxW), h = box.height() * (w / box.width());
            blocks << Blk{box.top(), QJsonObject{{"type", "img"}, {"png_base64", QString::fromLatin1(png.toBase64())}, {"w", w}, {"h", h}, {"align", std::abs(box.center().x() - ps.width() / 2) < ps.width() * 0.05 ? "center" : "left"}}};
        }
        std::stable_sort(blocks.begin(), blocks.end(), [](const Blk& a, const Blk& b) { return a.top < b.top; });
        QJsonArray arr; for (const Blk& b : blocks) arr.append(b.obj);
        pages.append(QJsonObject{{"width", ps.width()}, {"height", ps.height()}, {"blocks", arr}});
        FPDF_ClosePage(page); // lk still held
    }
    return QJsonObject{{"pages", pages}};
}
}
