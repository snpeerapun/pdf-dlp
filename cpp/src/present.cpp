#include "present.h"
PresentWidget::PresentWidget(std::shared_ptr<PdfDoc> d, int startPage, QWidget* parent) : QWidget(parent), doc(std::move(d)), page(startPage) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint); setAttribute(Qt::WA_DeleteOnClose); setStyleSheet("background:#000;"); setCursor(Qt::BlankCursor); setMouseTracking(true);
    hideCursor.setSingleShot(true); hideCursor.setInterval(1500); connect(&hideCursor, &QTimer::timeout, this, [this]() { setCursor(Qt::BlankCursor); });
}
void PresentWidget::go(int delta) { const int n = std::clamp(page + delta, 0, doc->pages() - 1); if (n != page) { page = n; update(); } }
void PresentWidget::render() {
    if (imgPage == page && imgFor == size()) return;
    const QSizeF s = doc->sizes[page]; const qreal dpr = devicePixelRatioF();
    const double k = std::min(width() / s.width(), height() / s.height());
    img = doc->render(page, int(s.width() * k), dpr); imgPage = page; imgFor = size();
}
void PresentWidget::paintEvent(QPaintEvent*) {
    render(); QPainter p(this); p.fillRect(rect(), Qt::black);
    if (!img.isNull()) { const QSize sz = img.size() / img.devicePixelRatio(); p.drawImage(QRect((width() - sz.width()) / 2, (height() - sz.height()) / 2, sz.width(), sz.height()), img); }
    p.setPen(QColor(255, 255, 255, 120)); p.setFont(QFont("Noto Sans Thai", 13)); p.drawText(rect().adjusted(0, 0, -18, -12), Qt::AlignRight | Qt::AlignBottom, QString("%1 / %2").arg(page + 1).arg(doc->pages()));
}
void PresentWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Escape: case Qt::Key_Q: close(); break;
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: case Qt::Key_PageDown: case Qt::Key_Return: case Qt::Key_N: go(1); break;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_Backspace: case Qt::Key_P: go(-1); break;
    case Qt::Key_Home: go(-page); break; case Qt::Key_End: go(doc->pages()); break;
    default: QWidget::keyPressEvent(e);
    }
}
void PresentWidget::mousePressEvent(QMouseEvent* e) { if (e->button() == Qt::LeftButton) go(1); else if (e->button() == Qt::RightButton) go(-1); }
void PresentWidget::wheelEvent(QWheelEvent* e) { if (e->angleDelta().y() < 0) go(1); else if (e->angleDelta().y() > 0) go(-1); }
void PresentWidget::resizeEvent(QResizeEvent*) { update(); }
