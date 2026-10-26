#pragma once
#include <QtWidgets>
#include <QtConcurrent>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <atomic>
#include <memory>
#include <mutex>

// Thread-safe-ish PDFium document holder (PDFium itself is single-threaded: we serialise with a mutex).
struct PdfDoc {
    FPDF_DOCUMENT doc = nullptr;
    std::mutex mu;
    std::vector<QSizeF> sizes; // points
    QString path;
    ~PdfDoc() { if (doc) FPDF_CloseDocument(doc); }
};

// A page tile rendered at a given scale. Old image is shown scaled until the new one arrives.
class PageWidget : public QWidget {
    Q_OBJECT
public:
    PageWidget(std::shared_ptr<PdfDoc> d, int index, QWidget* parent = nullptr);
    void setScale(double s);
    int index() const { return idx; }
    bool wanted = false; // visible (or near) -> keep rendered
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void requestRender();
    std::shared_ptr<PdfDoc> doc; int idx; double scale = 1.0; double imgScale = 0; QImage img; std::atomic<int> gen{0}; bool pending = false;
};

class Viewer : public QScrollArea {
    Q_OBJECT
public:
    explicit Viewer(QWidget* parent = nullptr);
    bool open(const QString& path);
    void zoomTo(double s); void zoomStep(int dir); void fitWidth();
    double scale() const { return sc; }
signals:
    void zoomChanged(double); void pageChanged(int, int);
protected:
    void wheelEvent(QWheelEvent* e) override; void resizeEvent(QResizeEvent* e) override; void scrollContentsBy(int dx, int dy) override;
private:
    void relayout(); void updateWanted();
    std::shared_ptr<PdfDoc> doc; QWidget* content; QVBoxLayout* vbox; std::vector<PageWidget*> pages; double sc = 1.0; bool fit = true; QTimer wantTimer;
};
