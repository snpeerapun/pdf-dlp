#pragma once
// Full-screen presentation of a document: one page per slide, black background, keyboard/mouse navigation.
#include <QtWidgets>
#include "doc.h"
class PresentWidget : public QWidget {
    Q_OBJECT
public:
    PresentWidget(std::shared_ptr<PdfDoc> d, int startPage, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override; void keyPressEvent(QKeyEvent* e) override; void mousePressEvent(QMouseEvent* e) override; void resizeEvent(QResizeEvent*) override; void wheelEvent(QWheelEvent* e) override;
private:
    void go(int delta); void render();
    std::shared_ptr<PdfDoc> doc; int page; QImage img; int imgPage = -1; QSize imgFor; QTimer hideCursor;
};
