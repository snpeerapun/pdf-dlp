#pragma once
// Font Awesome 6 glyph icons (solid), coloured per use. Codepoints from the FA cheatsheet.
#include <QtGui>
#include <QtWidgets>
namespace FA {
constexpr ushort FolderOpen = 0xf07c, Save = 0xf0c7, Undo = 0xf0e2, Gear = 0xf013, Plus = 0x2b, Minus = 0xf068, Xmark = 0xf00d, Check = 0xf00c,
    ChevronUp = 0xf077, ChevronDown = 0xf078, ArrowsLR = 0xf07e, Expand = 0xf065, Sliders = 0xf1de, Columns = 0xf0db, FilePdf = 0xf1c1,
    ObjectGroup = 0xf247, Scissors = 0xf0c4, FileCirclePlus = 0xe494, RotateRight = 0xf2f9, RotateLeft = 0xf2ea, Trash = 0xf2ed, FileExport = 0xf56e,
    Lock = 0xf023, LockOpen = 0xf3c1, Marker = 0xf5a1, Droplet = 0xf043, ListOl = 0xf0cb, Crop = 0xf565, Images = 0xf302, FileImage = 0xf1c5,
    Compress = 0xf066, Broom = 0xf51a, PenToSquare = 0xf044, Print = 0xf02f, Tag = 0xf02b, Info = 0xf05a, Wrench = 0xf7d9, Clone = 0xf24d,
    Stamp = 0xf5bf, Eraser = 0xf12d, Windows = 0xf17a, Graduation = 0xf19d, Layers = 0xf5fd, Copy = 0xf0c5, Search = 0xf002, ShieldHalved = 0xf3ed, Circle = 0xf111;

inline QString font() { return QStringLiteral("Font Awesome 6 Free"); }
inline QIcon icon(ushort code, const QColor& color, int px = 18, bool brands = false) {
    QIcon ic;
    for (qreal dpr : {1.0, 2.0}) {
        QPixmap pm(int(px * dpr), int(px * dpr)); pm.setDevicePixelRatio(dpr); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
        QFont f(brands ? QStringLiteral("Font Awesome 6 Brands") : font()); f.setPixelSize(int(px * 0.82)); f.setWeight(brands ? QFont::Normal : QFont::Black);
        p.setFont(f); p.setPen(color); p.drawText(QRect(0, 0, px, px), Qt::AlignCenter, QString(QChar(code)));
        ic.addPixmap(pm);
    }
    return ic;
}
inline QLabel* label(ushort code, const QColor& color, int px, QWidget* parent = nullptr) {
    auto* l = new QLabel(QString(QChar(code)), parent);
    // style sheet wins over setFont(): declare the icon font in QSS so the global "*" rule does not override it
    l->setStyleSheet(QStringLiteral("font-family:\"%1\";font-weight:900;font-size:%2px;color:%3;background:transparent").arg(font()).arg(px).arg(color.name()));
    return l;
}
}
