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
                Line ln; ln.box = e.rect; ln.size = (e.fontSize >= 3 && e.fontSize <= 300) ? e.fontSize : (e.rect.height() > 0 ? e.rect.height() * 0.78 : 12.0);
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
            bool dup = false; if (!t.isEmpty() && t.size() <= 6) for (int k = std::max(0, i - 3); k <= std::min(int(lines.size()) - 1, i + 3) && !dup; k++) if (k != i && textOf(k).contains(t) && textOf(k).trimmed().size() > t.size()) dup = true;
            if (dup) lines.removeAt(i);
        }
        // ---- thin path objects (table rules) for border detection ----
        QList<QRectF> rules;
        if (session) for (const EditObject& e : session->objects(pi)) if (e.type == FPDF_PAGEOBJ_PATH && (e.rect.width() < 3 || e.rect.height() < 3) && (e.rect.width() > 8 || e.rect.height() > 8)) rules << e.rect;
        std::lock_guard<std::mutex> lk(pdfiumLock());
        // ---- rows: lines that overlap vertically (label + value cells on one table row) ----
        struct Row { QList<Line> cells; QRectF box; double size = 0; };
        QList<Row> rows;
        std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.box.top() < b.box.top(); });
        for (const Line& ln : lines) {
            Row* hit = nullptr;
            for (int k = rows.size() - 1; k >= 0 && k >= rows.size() - 4; k--) {
                Row& r = rows[k]; const double ov = std::min(r.box.bottom(), ln.box.bottom()) - std::max(r.box.top(), ln.box.top());
                if (ov >= std::min(r.box.height(), ln.box.height()) * 0.5) { hit = &r; break; }
            }
            if (hit) { hit->cells << ln; hit->box = hit->box.united(ln.box); hit->size = std::max(hit->size, ln.size); }
            else { Row r; r.cells << ln; r.box = ln.box; r.size = ln.size; rows << r; }
        }
        for (Row& r : rows) std::stable_sort(r.cells.begin(), r.cells.end(), [](const Line& a, const Line& b) { return a.box.left() < b.box.left(); });
        // ---- runs for a list of lines (one paragraph / one cell) ----
        auto runsFor = [&](const QList<Line>& ls) {
            QJsonArray runs; QString text; QString runColor; bool runBold = false; double runSize = 0;
            auto flush = [&]() { if (!text.isEmpty()) { runs.append(QJsonObject{{"text", dedupeThai(text)}, {"size", runSize > 0 ? runSize : 12.0}, {"bold", runBold}, {"color", runColor}}); text.clear(); } };
            for (int li = 0; li < ls.size(); li++) {
                const Line& ln = ls[li];
                for (const Ch& ch : ln.chars) {
                    if (!text.isEmpty() && (ch.bold != runBold || std::abs(ch.size - runSize) > 0.5 || ch.color != runColor) && ch.ch != ' ') flush();
                    if (text.isEmpty()) { runBold = ch.bold; runSize = ch.size; runColor = ch.color; }
                    text += ch.ch;
                }
                if (li + 1 < ls.size()) { const QChar last = text.isEmpty() ? QChar(' ') : text.back(); const bool thai = last.unicode() >= 0x0E00 && last.unicode() <= 0x0E7F; if (!thai && !text.endsWith(' ')) text += ' '; }
            }
            flush(); return runs;
        };
        auto alignFor = [&](const QRectF& box) { QString align = "left"; const double cx = box.center().x(); if (std::abs(cx - ps.width() / 2) < ps.width() * 0.03 && box.left() > ps.width() * 0.15) align = "center"; else if (box.right() > ps.width() * 0.85 && box.left() > ps.width() * 0.45) align = "right"; return align; };
        // ---- tables: consecutive multi-cell rows whose cell lefts line up ----
        struct Blk { double top; QJsonObject obj; };
        QList<Blk> blocks; double prevBottom = 0;
        auto spaceBefore = [&](const QRectF& box) { const double b = prevBottom > 0 ? std::clamp(box.top() - prevBottom - 4, 0.0, 36.0) : 0; prevBottom = box.bottom(); return b; };
        auto aligned = [](double a, double b, double tol) { return std::abs(a - b) <= tol; };
        for (int ri = 0; ri < rows.size();) {
            // try to grow a table from row ri
            int rj = ri; QList<double> cols;
            if (rows[ri].cells.size() >= 2) {
                for (const Line& c : rows[ri].cells) cols << c.box.left();
                while (rj + 1 < rows.size()) {
                    const Row& nx = rows[rj + 1]; const double tol = std::max(6.0, nx.size * 0.8);
                    const bool gapOk = nx.box.top() - rows[rj].box.bottom() < std::max(rows[rj].size, nx.size) * 2.2;
                    if (!gapOk) break;
                    int matched = 0; for (const Line& c : nx.cells) for (double x : cols) if (aligned(c.box.left(), x, tol)) { matched++; break; }
                    const bool continuation = nx.cells.size() >= 1 && matched == nx.cells.size() && !aligned(nx.cells.first().box.left(), cols.first(), tol); // wrapped cell text (2nd column only)
                    if (nx.cells.size() >= 2 && matched >= 2) { for (const Line& c : nx.cells) { bool have = false; for (double x : cols) if (aligned(c.box.left(), x, tol)) have = true; if (!have) cols << c.box.left(); } rj++; }
                    else if (continuation) rj++;
                    else break;
                }
            }
            if (rj > ri) {
                std::sort(cols.begin(), cols.end());
                QRectF tbox; for (int k = ri; k <= rj; k++) tbox = tbox.isNull() ? rows[k].box : tbox.united(rows[k].box);
                const int nc = cols.size(); QList<QList<QList<Line>>> grid; // row -> col -> lines
                for (int k = ri; k <= rj; k++) {
                    const Row& r = rows[k]; const double tol = std::max(6.0, r.size * 0.8);
                    QList<QList<Line>> cells(nc);
                    for (const Line& c : r.cells) { int best = 0; double bd = 1e18; for (int ci = 0; ci < nc; ci++) { const double d = std::abs(c.box.left() - cols[ci]); if (d < bd) { bd = d; best = ci; } } cells[best] << c; }
                    Q_UNUSED(tol);
                    if (!grid.isEmpty() && cells[0].isEmpty()) { for (int ci = 0; ci < nc; ci++) grid.last()[ci] += cells[ci]; } else grid << cells;
                }
                bool borders = false; for (const QRectF& r : rules) if (r.intersects(tbox.adjusted(-6, -6, 6, 6))) { borders = true; break; }
                const double pad = borders ? 8.0 : 2.0;
                QJsonArray widths; for (int ci = 0; ci < nc; ci++) { double right = ci + 1 < nc ? cols[ci + 1] : tbox.right() + pad; double w = right - cols[ci]; if (ci == 0) w += pad; widths.append(std::max(w, 20.0)); }
                QJsonArray jrows;
                for (const QList<QList<Line>>& gr : grid) { QJsonArray jr; for (const QList<Line>& cell : gr) { QJsonArray runs = runsFor(cell); jr.append(QJsonObject{{"runs", runs}, {"align", "left"}}); } jrows.append(jr); }
                const double before = spaceBefore(tbox);
                blocks << Blk{tbox.top(), QJsonObject{{"type", "table"}, {"cols", widths}, {"rows", jrows}, {"borders", borders}, {"left", std::max(0.0, cols.first() - pad)}, {"space_before", before}}};
                ri = rj + 1; continue;
            }
            // ---- paragraph: merge consecutive single-cell rows (same size, small gap, same left) ----
            Row& r0 = rows[ri]; QList<Line> plines; QRectF pbox = r0.box; QJsonArray tabs;
            if (r0.cells.size() >= 2) { // one multi-cell row that is not a table: cells joined by tab stops
                for (int ci = 1; ci < r0.cells.size(); ci++) tabs.append(r0.cells[ci].box.left());
                Line joined = r0.cells[0]; for (int ci = 1; ci < r0.cells.size(); ci++) { joined.chars << Ch{'\t', joined.box, joined.size, false, joined.chars.isEmpty() ? "#000000" : joined.chars.last().color, false}; joined.chars += r0.cells[ci].chars; joined.box = joined.box.united(r0.cells[ci].box); }
                plines << joined; ri++;
            } else {
                plines << r0.cells[0]; int rk = ri + 1;
                while (rk < rows.size() && rows[rk].cells.size() == 1) {
                    const Line& prev = plines.last(); const Line& ln = rows[rk].cells[0];
                    const double gap = ln.box.top() - prev.box.bottom(); const bool sizeSame = std::abs(ln.size - prev.size) < prev.size * 0.2;
                    if (gap >= -prev.size * 0.3 && gap < prev.size * 0.75 && sizeSame && std::abs(ln.box.left() - pbox.left()) < prev.size * 3) { plines << ln; pbox = pbox.united(ln.box); rk++; } else break;
                }
                ri = rk;
            }
            QJsonArray runs = runsFor(plines); if (runs.isEmpty()) continue;
            const double before = spaceBefore(pbox);
            QJsonObject po{{"type", "p"}, {"runs", runs}, {"align", tabs.isEmpty() ? alignFor(pbox) : QString("left")}, {"space_before", before}};
            if (!tabs.isEmpty()) po["tabs"] = tabs;
            blocks << Blk{pbox.top(), po};
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
