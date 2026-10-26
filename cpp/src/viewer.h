#pragma once
#include <QtWidgets>
#include "doc.h"
#include <atomic>

class PageWidget : public QWidget {
    Q_OBJECT
public:
    PageWidget(std::shared_ptr<PdfDoc> d, int index, QWidget* parent = nullptr);
    void setScale(double s);
    int index() const { return idx; }
    bool wanted = false;
    int distance = 0; // pages away from current (render priority)
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void requestRender();
    std::shared_ptr<PdfDoc> doc; int idx; double scale = 1.0; double imgScale = 0; QImage img; std::atomic<int> gen{0};
};

class Viewer : public QScrollArea {
    Q_OBJECT
public:
    explicit Viewer(QWidget* parent = nullptr);
    void setDocument(std::shared_ptr<PdfDoc> d);
    void clear();
    void zoomTo(double s); void zoomStep(int dir); void fitWidth(); void fitPage();
    void gotoPage(int oneBased);
    double scale() const { return sc; }
    int currentPage() const { return cur; }
    bool fitMode() const { return fit == 1; }
signals:
    void zoomChanged(double); void pageChanged(int, int);
protected:
    void wheelEvent(QWheelEvent* e) override; void resizeEvent(QResizeEvent* e) override; void scrollContentsBy(int dx, int dy) override; void keyPressEvent(QKeyEvent* e) override;
private:
    void relayout(); void updateWanted();
    std::shared_ptr<PdfDoc> doc; QWidget* content; QVBoxLayout* vbox; std::vector<PageWidget*> pages; double sc = 1.0; int fit = 1; int cur = 0; QTimer wantTimer; bool programmatic = false;
};
