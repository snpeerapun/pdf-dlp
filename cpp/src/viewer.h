#pragma once
#include <QtWidgets>
#include "doc.h"
#include "edit.h"
#include <atomic>

enum class EditTool { Select, Text, Image, Line, Rect, TextField, Checkbox };
struct EditState {
    EditSession* session = nullptr; EditTool tool = EditTool::Select;
    QString font; double fontSize = 14; QColor color = QColor("#1f2733");
    double stroke = 1.5; QColor strokeColor = QColor("#1f2733"); QColor fill = QColor("#fff3d6"); bool filled = false;
};

class Viewer;
class PageWidget : public QWidget {
    Q_OBJECT
public:
    PageWidget(std::shared_ptr<PdfDoc> d, int index, Viewer* v, QWidget* parent = nullptr);
    void setScale(double s);
    int index() const { return idx; }
    void invalidate() { imgScale = 0; objs.clear(); update(); }
    bool wanted = false; int distance = 0;
    int selected = -1;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override; void mouseMoveEvent(QMouseEvent*) override; void mouseReleaseEvent(QMouseEvent*) override; void mouseDoubleClickEvent(QMouseEvent*) override; void leaveEvent(QEvent*) override;
private:
    void requestRender(); QPointF vp(const QPoint& p) const { return QPointF(p) / scale; }
    const EditObject* at(QPointF v);
    std::shared_ptr<PdfDoc> doc; int idx; Viewer* viewer; double scale = 1.0; double imgScale = 0; QImage img; std::atomic<int> gen{0};
    QList<EditObject> objs; bool objsLoaded = false; int hover = -1; bool dragging = false; QPointF dragStart, dragCur; bool rubber = false;
};

class Viewer : public QScrollArea {
    Q_OBJECT
public:
    explicit Viewer(QWidget* parent = nullptr);
    void setDocument(std::shared_ptr<PdfDoc> d);
    void clear();
    void zoomTo(double s); void zoomAt(const QPointF& viewportPos, double s); void zoomStep(int dir); void fitWidth(); void fitPage();
    void gotoPage(int oneBased);
    double scale() const { return sc; }
    int currentPage() const { return cur; }
    // editing
    EditState edit;
    void setEditSession(EditSession* s);
    void refreshPage(int page); void refreshAllPages();
    void deleteSelected();
    void openInlineEditor(PageWidget* pw, QPointF viewerPt, int objIndex, const QString& text, double fontSizePt, QRectF rectPt);
    void commitInlineEditor(bool accept);
    QTextEdit* inlineEdit = nullptr; int inlinePage = -1; int inlineObj = -1; QPointF inlinePt;
    bool hasSelection() const;
    int formPage = -1; // page with a focused form field
    // search highlights
    QList<PdfDoc::SearchHit> hits; int currentHit = -1;
    void setSearchHits(const QList<PdfDoc::SearchHit>& h, int current); void gotoHit(int i);
signals:
    void zoomChanged(double); void pageChanged(int, int);
    void textCommitted(int page, QPointF viewerPt, QString text);
    void textEdited(int page, int objIndex, QString text);
    void imageRectDrawn(int page, QRectF viewerRect);
    void fieldRectDrawn(int page, QRectF viewerRect, bool checkbox);
    void selectionChanged();
protected:
    void wheelEvent(QWheelEvent* e) override; void resizeEvent(QResizeEvent* e) override; void scrollContentsBy(int dx, int dy) override; void keyPressEvent(QKeyEvent* e) override; bool viewportEvent(QEvent* e) override;
private:
    void relayout(); void updateWanted();
    std::shared_ptr<PdfDoc> doc; QWidget* content; QVBoxLayout* vbox; std::vector<PageWidget*> pages; double sc = 1.0; int fit = 1; int cur = 0; QTimer wantTimer;
    friend class PageWidget;
};
