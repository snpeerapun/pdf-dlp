#pragma once
// Edit session: a separate PDFium document (with form-fill environment) that we mutate in place and save as a new work copy.
#include <QtCore>
#include <QtGui>
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>
#include <fpdf_text.h>
#include <map>

struct EditObject { int index = -1; int type = 0; QRectF rect; QString text; float fontSize = 0; bool subsetFont = false; QList<int> members; /* glyph objects merged into this line */ };

class EditSession {
public:
    /// fonts: display name -> TTF bytes (first entry is the default)
    EditSession(const QString& path, const QString& password, const QList<QPair<QString, QByteArray>>& fonts);
    ~EditSession();
    bool ok() const { return doc != nullptr; }
    int pages() const { return int(sizes.size()); }
    QSizeF size(int page) const { return sizes[page]; }           // displayed size in points
    QImage render(int page, int widthPx, qreal dpr = 1.0);
    QList<EditObject> objects(int page);           // hidden (whited-out) objects are excluded
    EditObject objectAt(int page, QPointF viewerPt);
    void move(int page, int index, QPointF viewerDelta);
    void remove(int page, int index);
    bool setText(int page, int index, const QString& text, const QString& fontName = QString());
    void addText(int page, QPointF viewerPt, const QString& text, double fontSize, const QColor& color, const QString& fontName = QString());
    void addLine(int page, QPointF a, QPointF b, const QColor& color, double width);
    void addRect(int page, QRectF viewerRect, const QColor& stroke, double width, const QColor& fill, bool filled);
    QStringList fontNames() const { QStringList n; for (auto& f : fontList) n << f.first; return n; }
    void addImage(int page, QRectF viewerRect, const QImage& img);
    bool hasFieldAt(int page, QPointF viewerPt);
    void formMouse(int page, QPointF viewerPt, bool down);
    void formChar(int page, uint ch);
    void formKey(int page, int fwlKey, int modifiers);
    void killFocus();
    bool save(const QString& outPath);
    bool dirty = false;
    QString error;
    /// PDFium cannot safely regenerate existing content streams (text is lost on Skia/Chrome PDFs), so existing objects are
    /// never rewritten: move/edit/delete paint a white box over the original and append a new object.
    QString lastNote;
private:
    FPDF_PAGE pageHandle(int page);
    QPointF toPage(int page, QPointF v);
    QPointF toViewer(int page, double px, double py);
    FPDF_FONT fontFor(const QString& name);
    FPDF_DOCUMENT doc = nullptr; FPDF_FORMHANDLE form = nullptr; FPDF_FORMFILLINFO ffi{};
    std::map<int, FPDF_PAGE> pagesOpen; std::vector<QSizeF> sizes; QList<QPair<QString, QByteArray>> fontList; std::map<QString, FPDF_FONT> fontsLoaded; std::set<int> dirtyPages; std::map<int, std::set<int>> hidden;
    void whiteout(int page, QRectF viewerRect);
};
