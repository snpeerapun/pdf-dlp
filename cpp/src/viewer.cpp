#include "viewer.h"
#include <QtConcurrent>

static QThreadPool& renderPool() { static QThreadPool p; p.setMaxThreadCount(2); return p; }
static const int PAD = 22, GAP = 18;

PageWidget::PageWidget(std::shared_ptr<PdfDoc> d, int index, QWidget* parent) : QWidget(parent), doc(std::move(d)), idx(index) {
    setAttribute(Qt::WA_OpaquePaintEvent);
}
void PageWidget::setScale(double s) {
    if (scale == s) return;
    scale = s;
    const QSizeF pt = doc->sizes[idx];
    setFixedSize(int(pt.width() * s), int(pt.height() * s));
    update();
    if (wanted) requestRender();
}
void PageWidget::requestRender() {
    if (imgScale == scale) return;
    const int myGen = ++gen;
    const double s = scale; const qreal dpr = devicePixelRatioF();
    const int w = int(doc->sizes[idx].width() * s);
    auto d = doc; const int i = idx; const int prio = -distance;
    QPointer<PageWidget> self(this);
    auto* task = QRunnable::create([=]() {
        if (myGen != gen.load()) return;
        QElapsedTimer t; t.start();
        QImage out = d->render(i, w, dpr);
        fprintf(stderr, "[render] page %d scale %.2f %dx%d px: %lld ms\n", i + 1, s, out.width(), out.height(), (long long)t.elapsed());
        QMetaObject::invokeMethod(qApp, [=]() { if (!self || myGen != self->gen.load()) return; self->img = out; self->imgScale = s; self->update(); });
    });
    renderPool().start(task, prio);
}
void PageWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!img.isNull()) { p.setRenderHint(QPainter::SmoothPixmapTransform, true); p.drawImage(rect(), img); }
    else if (wanted) { p.setPen(QColor(150, 155, 165)); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("…")); }
    if (wanted && imgScale != scale) requestRender();
}

Viewer::Viewer(QWidget* parent) : QScrollArea(parent) {
    content = new QWidget; vbox = new QVBoxLayout(content); vbox->setSpacing(GAP); vbox->setContentsMargins(PAD, PAD, PAD, PAD); vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setWidget(content); setWidgetResizable(true); setAlignment(Qt::AlignHCenter); setFrameShape(QFrame::NoFrame);
    viewport()->setStyleSheet("background:#e4e9f0;");
    content->setStyleSheet("background:#e4e9f0;");
    wantTimer.setSingleShot(true); wantTimer.setInterval(40);
    connect(&wantTimer, &QTimer::timeout, this, &Viewer::updateWanted);
    setFocusPolicy(Qt::StrongFocus);
}
void Viewer::clear() { for (auto* p : pages) { vbox->removeWidget(p); p->deleteLater(); } pages.clear(); doc.reset(); cur = 0; }
void Viewer::setDocument(std::shared_ptr<PdfDoc> d) {
    clear(); doc = d; if (!d) return;
    for (int i = 0; i < d->pages(); i++) { auto* pw = new PageWidget(d, i, content); pw->setStyleSheet("background:white;"); pages.push_back(pw); vbox->addWidget(pw, 0, Qt::AlignHCenter); }
    relayout();
}
void Viewer::relayout() {
    if (!doc) return;
    double maxW = 1, maxH = 1; for (auto& s : doc->sizes) { maxW = std::max(maxW, s.width()); maxH = std::max(maxH, s.height()); }
    if (fit == 1) sc = (viewport()->width() - PAD * 2 - 12) / maxW;
    else if (fit == 2) sc = std::min((viewport()->width() - PAD * 2 - 12) / maxW, (viewport()->height() - PAD * 2) / maxH);
    for (auto* p : pages) p->setScale(sc);
    emit zoomChanged(sc); wantTimer.start();
}
void Viewer::updateWanted() {
    if (!doc) return;
    const QRect vis(QPoint(0, verticalScrollBar()->value()), viewport()->size());
    const QRect near = vis.adjusted(0, -900, 0, 900);
    int best = 0, bestD = INT_MAX;
    for (auto* p : pages) { const int d = std::abs(p->geometry().top() - (vis.top() + vis.height() / 3)); if (d < bestD) { bestD = d; best = p->index(); } }
    for (auto* p : pages) { p->wanted = p->geometry().intersects(near); p->distance = std::abs(p->index() - best); if (p->wanted) p->update(); }
    if (best != cur || true) { cur = best; emit pageChanged(cur + 1, int(pages.size())); }
}
void Viewer::zoomTo(double s) {
    const double old = sc; fit = 0; sc = std::clamp(s, 0.2, 6.0);
    const QPointF c(horizontalScrollBar()->value() + viewport()->width() / 2.0, verticalScrollBar()->value() + viewport()->height() / 2.0);
    relayout();
    const double r = sc / old;
    horizontalScrollBar()->setValue(int(c.x() * r - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(int(c.y() * r - viewport()->height() / 2.0));
}
void Viewer::zoomStep(int dir) {
    static const double steps[] = {0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4};
    double next = sc;
    if (dir > 0) { for (double s : steps) if (s > sc + 1e-3) { next = s; break; } }
    else { for (int i = 14; i >= 0; i--) if (steps[i] < sc - 1e-3) { next = steps[i]; break; } }
    zoomTo(next);
}
void Viewer::fitWidth() { fit = 1; relayout(); }
void Viewer::fitPage() { fit = 2; relayout(); }
void Viewer::gotoPage(int n) {
    if (pages.empty()) return;
    n = std::clamp(n, 1, int(pages.size()));
    verticalScrollBar()->setValue(pages[n - 1]->geometry().top() - PAD);
    cur = n - 1; emit pageChanged(n, int(pages.size())); wantTimer.start();
}
void Viewer::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) { const double k = std::exp(e->angleDelta().y() / 600.0); zoomTo(sc * k); e->accept(); return; }
    QScrollArea::wheelEvent(e);
}
void Viewer::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::ZoomIn) || (e->modifiers() & Qt::ControlModifier && e->key() == Qt::Key_Equal)) { zoomStep(1); return; }
    if (e->matches(QKeySequence::ZoomOut)) { zoomStep(-1); return; }
    if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_0) { fitWidth(); return; }
    QScrollArea::keyPressEvent(e);
}
void Viewer::resizeEvent(QResizeEvent* e) { QScrollArea::resizeEvent(e); if (fit) relayout(); else wantTimer.start(); }
void Viewer::scrollContentsBy(int dx, int dy) { QScrollArea::scrollContentsBy(dx, dy); wantTimer.start(); }
