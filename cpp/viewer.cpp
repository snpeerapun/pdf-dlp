#include "viewer.h"

static QThreadPool& renderPool() { static QThreadPool p; p.setMaxThreadCount(2); return p; }

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
    const int w = int(doc->sizes[idx].width() * s * dpr), h = int(doc->sizes[idx].height() * s * dpr);
    auto d = doc; const int i = idx;
    QPointer<PageWidget> self(this);
    QtConcurrent::run(&renderPool(), [=]() {
        if (myGen != gen.load()) return;           // superseded while queued
        QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
        QElapsedTimer timer; timer.start();
        {
            std::lock_guard<std::mutex> lock(d->mu);
            FPDF_PAGE page = FPDF_LoadPage(d->doc, i);
            if (!page) return;
            FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, out.bits(), int(out.bytesPerLine()));
            FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
            FPDF_RenderPageBitmap(bmp, page, 0, 0, w, h, 0, FPDF_ANNOT | FPDF_LCD_TEXT);
            FPDFBitmap_Destroy(bmp);
            FPDF_ClosePage(page);
        }
        fprintf(stderr, "[render] page %d scale %.2f %dx%d px: %lld ms\n", i + 1, s, w, h, (long long)timer.elapsed());
        out.setDevicePixelRatio(dpr);
        QMetaObject::invokeMethod(qApp, [=]() {
            if (!self || myGen != self->gen.load()) return;
            self->img = out; self->imgScale = s; self->update();
        });
    });
}

void PageWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!img.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(rect(), img); // CSS-like: stretch old image until crisp one lands
    } else if (wanted) {
        p.setPen(QColor(150, 150, 160)); p.drawText(rect(), Qt::AlignCenter, "rendering…");
    }
    if (wanted && imgScale != scale && !pending) requestRender();
}

Viewer::Viewer(QWidget* parent) : QScrollArea(parent) {
    content = new QWidget; vbox = new QVBoxLayout(content); vbox->setSpacing(18); vbox->setContentsMargins(22, 22, 22, 22); vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setWidget(content); setWidgetResizable(true); setAlignment(Qt::AlignHCenter);
    viewport()->setStyleSheet("background:#0b0e12;");
    wantTimer.setSingleShot(true); wantTimer.setInterval(40);
    connect(&wantTimer, &QTimer::timeout, this, &Viewer::updateWanted);
}

bool Viewer::open(const QString& path) {
    auto d = std::make_shared<PdfDoc>();
    d->doc = FPDF_LoadDocument(path.toUtf8().constData(), nullptr);
    if (!d->doc) return false;
    d->path = path;
    const int n = FPDF_GetPageCount(d->doc);
    for (int i = 0; i < n; i++) { FS_SIZEF sz{}; FPDF_GetPageSizeByIndexF(d->doc, i, &sz); d->sizes.push_back(QSizeF(sz.width, sz.height)); }
    for (auto* p : pages) { vbox->removeWidget(p); p->deleteLater(); }
    pages.clear(); doc = d;
    for (int i = 0; i < n; i++) { auto* pw = new PageWidget(d, i, content); pages.push_back(pw); vbox->addWidget(pw, 0, Qt::AlignHCenter); }
    fit = true; relayout(); return true;
}

void Viewer::relayout() {
    if (!doc) return;
    if (fit) { double maxW = 1; for (auto& s : doc->sizes) maxW = std::max(maxW, s.width()); sc = (viewport()->width() - 44 - 12) / maxW; }
    for (auto* p : pages) p->setScale(sc);
    emit zoomChanged(sc); wantTimer.start();
}
void Viewer::updateWanted() {
    const QRect vis(QPoint(0, verticalScrollBar()->value()), viewport()->size());
    const QRect near = vis.adjusted(0, -900, 0, 900);
    int cur = 0, best = INT_MAX;
    for (auto* p : pages) {
        const QRect r = p->geometry();
        p->wanted = r.intersects(near);
        if (p->wanted) p->update();
        const int d = std::abs(r.top() - (vis.top() + vis.height() / 3)); if (d < best) { best = d; cur = p->index(); }
    }
    emit pageChanged(cur + 1, int(pages.size()));
}
void Viewer::zoomTo(double s) {
    const double old = sc; fit = false; sc = std::clamp(s, 0.2, 6.0);
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
void Viewer::fitWidth() { fit = true; relayout(); }
void Viewer::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) { const double k = std::exp(e->angleDelta().y() / 600.0); zoomTo(sc * k); e->accept(); return; }
    QScrollArea::wheelEvent(e);
}
void Viewer::resizeEvent(QResizeEvent* e) { QScrollArea::resizeEvent(e); if (fit) relayout(); else wantTimer.start(); }
void Viewer::scrollContentsBy(int dx, int dy) { QScrollArea::scrollContentsBy(dx, dy); wantTimer.start(); }
