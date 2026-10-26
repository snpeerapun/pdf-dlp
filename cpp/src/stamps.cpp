#include "stamps.h"
static QFont uiFont(int px, int weight) { QFont f(QStringLiteral("Noto Sans Thai")); f.setPixelSize(px); f.setWeight(QFont::Weight(weight)); return f; }
namespace Stamps {
Png fromImage(const QImage& img) {
    QByteArray bytes; QBuffer buf(&bytes); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG");
    return { QString::fromLatin1(bytes.toBase64()), img.width() ? double(img.height()) / img.width() : 1.0 };
}
Png badge(const QString& text, const QColor& bg, const QColor& fg, int fontPx) {
    const QFont f = uiFont(fontPx, 700); const QFontMetrics fm(f); const int pad = 18;
    QImage img(fm.horizontalAdvance(text) + pad * 2, int(fontPx * 1.5) + pad, QImage::Format_ARGB32_Premultiplied); img.fill(Qt::transparent);
    QPainter p(&img); p.setRenderHint(QPainter::Antialiasing); p.setBrush(bg); p.setPen(Qt::NoPen); p.drawRoundedRect(img.rect(), 14, 14);
    p.setFont(f); p.setPen(fg); p.drawText(img.rect(), Qt::AlignCenter, text);
    return fromImage(img);
}
Png text(const QString& t, const QColor& color, int fontPx, int weight) {
    const QFont f = uiFont(fontPx, weight); const QFontMetrics fm(f);
    QImage img(std::max(1, fm.horizontalAdvance(t) + 8), int(fontPx * 1.45), QImage::Format_ARGB32_Premultiplied); img.fill(Qt::transparent);
    QPainter p(&img); p.setRenderHint(QPainter::TextAntialiasing); p.setFont(f); p.setPen(color); p.drawText(img.rect(), Qt::AlignCenter, t);
    return fromImage(img);
}
Png watermark(const QString& t, const QColor& color, int fontPx) {
    const QFont f = uiFont(fontPx, 700); const QFontMetrics fm(f);
    QImage img(fm.horizontalAdvance(t) + 40, int(fontPx * 1.5), QImage::Format_ARGB32_Premultiplied); img.fill(Qt::transparent);
    QPainter p(&img); p.setRenderHint(QPainter::Antialiasing); p.setFont(f);
    QPainterPath path; path.addText(0, 0, f, t); const QRectF b = path.boundingRect(); path.translate((img.width() - b.width()) / 2 - b.left(), (img.height() - b.height()) / 2 - b.top());
    p.setPen(QPen(QColor(255, 255, 255, 230), fontPx * 0.06)); p.setBrush(color); p.drawPath(path);
    return fromImage(img);
}
Png fromImageFile(const QString& path) { QImage img(path); return fromImage(img.convertToFormat(QImage::Format_ARGB32)); }
}
