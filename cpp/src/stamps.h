#pragma once
#include <QtGui>
// PNG generators (base64) for visual marks. Thai works through the bundled Noto Sans Thai.
namespace Stamps {
struct Png { QString base64; double aspect; };
Png badge(const QString& text, const QColor& bg, const QColor& fg = Qt::white, int fontPx = 44);
Png text(const QString& text, const QColor& color, int fontPx = 40, int weight = 500);
Png watermark(const QString& text, const QColor& color, int fontPx = 120);
Png fromImageFile(const QString& path);
Png fromImage(const QImage& img);
}
