#include "viewer.h"
#include <QtConcurrent>
#include <fpdf_fwlevent.h>

static QThreadPool& renderPool() { static QThreadPool p; p.setMaxThreadCount(2); return p; }
static const int PAD = 22, GAP = 18;

PageWidget::PageWidget(std::shared_ptr<PdfDoc> d, int index, Viewer* v, QWidget* parent) : QWidget(parent), doc(std::move(d)), idx(index), viewer(v) {
    setAttribute(Qt::WA_OpaquePaintEvent); setMouseTracking(true);
}
void PageWidget::setScale(double s) {
    if (scale == s) return;
    scale = s;
    const QSizeF pt = viewer->edit.session ? viewer->edit.session->size(idx) : doc->sizes[idx];
    setFixedSize(int(pt.width() * s), int(pt.height() * s));
    update();
    if (wanted) requestRender();
}
void PageWidget::requestRender() {
    if (imgScale == scale) return;
    const int myGen = ++gen;
    const double s = scale; const qreal dpr = devicePixelRatioF();
    const int w = int((viewer->edit.session ? viewer->edit.session->size(idx) : doc->sizes[idx]).width() * s);
    auto d = doc; const int i = idx; const int prio = -distance; EditSession* es = viewer->edit.session;
    QPointer<PageWidget> self(this);
    if (es) { // edit mode: synchronous, the session is mutated from the UI thread
        img = es->render(i, w, dpr); imgScale = s; update(); return;
    }
    auto* task = QRunnable::create([=]() {
        if (myGen != gen.load()) return;
        QElapsedTimer t; t.start();
        QImage out = d->render(i, w, dpr);
        fprintf(stderr, "[render] page %d scale %.2f %dx%d px: %lld ms\n", i + 1, s, out.width(), out.height(), (long long)t.elapsed());
        QMetaObject::invokeMethod(qApp, [=]() { if (!self || myGen != self->gen.load()) return; self->img = out; self->imgScale = s; self->update(); });
    });
    renderPool().start(task, prio);
}
const EditObject* PageWidget::at(QPointF v) {
    if (!objsLoaded && viewer->edit.session) { objs = viewer->edit.session->objects(idx); objsLoaded = true; }
    const EditObject* best = nullptr; double bestArea = 1e18;
    for (const EditObject& e : objs) { if (e.type == FPDF_PAGEOBJ_PATH && e.rect.width() * e.rect.height() > width() * height() / (scale * scale) * 0.6) continue; if (e.rect.adjusted(-2, -2, 2, 2).contains(v)) { const double a = e.rect.width() * e.rect.height(); if (a < bestArea) { bestArea = a; best = &e; } } }
    return best;
}
void PageWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!img.isNull()) { p.setRenderHint(QPainter::SmoothPixmapTransform, true); p.drawImage(rect(), img); }
    else if (wanted) { p.setPen(QColor(150, 155, 165)); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("…")); }
    if (wanted && imgScale != scale) requestRender();
    if (!viewer->hits.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen);
        for (int i = 0; i < viewer->hits.size(); i++) { const auto& h = viewer->hits[i]; if (h.page != idx) continue; const bool cur = i == viewer->currentHit; p.setBrush(cur ? QColor(255, 140, 0, 120) : QColor(255, 220, 0, 90)); for (const QRectF& r : h.rects) p.drawRoundedRect(QRectF(r.topLeft() * scale, r.size() * scale), 2, 2); if (cur) { p.setPen(QPen(QColor("#ff8c00"), 1.5)); p.setBrush(Qt::NoBrush); for (const QRectF& r : h.rects) p.drawRoundedRect(QRectF(r.topLeft() * scale, r.size() * scale), 2, 2); p.setPen(Qt::NoPen); } }
    }
    if (!viewer->edit.session) return;
    p.setRenderHint(QPainter::Antialiasing);
    auto rectOf = [&](const EditObject& e) { return QRectF(e.rect.topLeft() * scale, e.rect.size() * scale); };
    if (hover >= 0 && hover < objs.size() && hover != selected) { p.setPen(QPen(QColor(79, 140, 255, 140), 1, Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawRect(rectOf(objs[hover])); }
    if (selected >= 0) { for (const EditObject& e : objs) if (e.index == selected) { QRectF r = rectOf(e); if (dragging) r.translate((dragCur - dragStart) * scale); p.setPen(QPen(QColor("#4f8cff"), 1.5)); p.setBrush(QColor(79, 140, 255, 30)); p.drawRect(r); } }
    if (rubber) {
        const QRectF r = QRectF(dragStart * scale, dragCur * scale).normalized();
        if (viewer->edit.tool == EditTool::Line) { p.setPen(QPen(viewer->edit.strokeColor, viewer->edit.stroke * scale)); p.drawLine(dragStart * scale, dragCur * scale); }
        else { p.setPen(QPen(viewer->edit.tool == EditTool::Rect ? viewer->edit.strokeColor : QColor("#4f8cff"), viewer->edit.tool == EditTool::Rect ? viewer->edit.stroke * scale : 1, viewer->edit.tool == EditTool::Rect ? Qt::SolidLine : Qt::DashLine)); p.setBrush(viewer->edit.tool == EditTool::Rect && viewer->edit.filled ? viewer->edit.fill : QColor(79, 140, 255, 25)); p.drawRect(r); }
    }
}
void PageWidget::mousePressEvent(QMouseEvent* e) {
    if (!viewer->edit.session || e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    EditSession* s = viewer->edit.session; const QPointF v = vp(e->pos()); setFocus();
    switch (viewer->edit.tool) {
    case EditTool::Select: {
        if (s->hasFieldAt(idx, v)) { s->formMouse(idx, v, true); s->formMouse(idx, v, false); viewer->formPage = idx; selected = -1; invalidate(); emit viewer->selectionChanged(); return; }
        viewer->formPage = -1;
        const EditObject* o = at(v); selected = o ? o->index : -1; dragging = o != nullptr; dragStart = dragCur = v; update(); emit viewer->selectionChanged(); break; }
    case EditTool::Text: emit viewer->textRequested(idx, v); break;
    default: rubber = true; dragStart = dragCur = v; update(); break;
    }
}
void PageWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!viewer->edit.session) { QWidget::mouseMoveEvent(e); return; }
    const QPointF v = vp(e->pos());
    if (dragging || rubber) { dragCur = v; update(); return; }
    if (viewer->edit.tool == EditTool::Select) { const EditObject* o = at(v); const int h = o ? int(o - objs.constData()) : -1; if (h != hover) { hover = h; update(); } setCursor(o ? Qt::SizeAllCursor : (viewer->edit.session->hasFieldAt(idx, v) ? Qt::IBeamCursor : Qt::ArrowCursor)); }
    else setCursor(viewer->edit.tool == EditTool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
}
void PageWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (!viewer->edit.session) { QWidget::mouseReleaseEvent(e); return; }
    EditSession* s = viewer->edit.session; const QPointF v = vp(e->pos());
    if (dragging) { dragging = false; const QPointF d = v - dragStart; if (selected >= 0 && (std::abs(d.x()) > 0.5 || std::abs(d.y()) > 0.5)) { s->move(idx, selected, d); invalidate(); } else update(); return; }
    if (rubber) {
        rubber = false; const QRectF r = QRectF(dragStart, v).normalized();
        switch (viewer->edit.tool) {
        case EditTool::Line: if (QLineF(dragStart, v).length() > 2) { s->addLine(idx, dragStart, v, viewer->edit.strokeColor, viewer->edit.stroke); invalidate(); } break;
        case EditTool::Rect: if (r.width() > 2 && r.height() > 2) { s->addRect(idx, r, viewer->edit.strokeColor, viewer->edit.stroke, viewer->edit.fill, viewer->edit.filled); invalidate(); } break;
        case EditTool::Image: if (r.width() > 4 && r.height() > 4) emit viewer->imageRectDrawn(idx, r); break;
        case EditTool::TextField: if (r.width() > 4 && r.height() > 4) emit viewer->fieldRectDrawn(idx, r, false); break;
        case EditTool::Checkbox: { const double sz = std::max(8.0, std::max(r.width(), r.height())); emit viewer->fieldRectDrawn(idx, QRectF(dragStart, QSizeF(sz, sz)), true); break; }
        default: break;
        }
        update();
    }
}
void PageWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (!viewer->edit.session) { QWidget::mouseDoubleClickEvent(e); return; }
    const EditObject* o = at(vp(e->pos()));
    if (o && o->type == FPDF_PAGEOBJ_TEXT) emit viewer->editTextRequested(idx, o->index, o->text);
}
void PageWidget::leaveEvent(QEvent*) { if (hover >= 0) { hover = -1; update(); } }

Viewer::Viewer(QWidget* parent) : QScrollArea(parent) {
    content = new QWidget; vbox = new QVBoxLayout(content); vbox->setSpacing(GAP); vbox->setContentsMargins(PAD, PAD, PAD, PAD); vbox->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setWidget(content); setWidgetResizable(true); setAlignment(Qt::AlignHCenter); setFrameShape(QFrame::NoFrame);
    viewport()->setStyleSheet("background:#e4e9f0;"); content->setStyleSheet("background:#e4e9f0;");
    wantTimer.setSingleShot(true); wantTimer.setInterval(40);
    connect(&wantTimer, &QTimer::timeout, this, &Viewer::updateWanted);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->grabGesture(Qt::PinchGesture);
}
bool Viewer::viewportEvent(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) { zoomAt(g->position(), sc * (1.0 + g->value())); return true; }
        if (g->gestureType() == Qt::SmartZoomNativeGesture) { if (fit == 1) zoomAt(g->position(), sc * 2.0); else fitWidth(); return true; }
    } else if (e->type() == QEvent::Gesture) {
        auto* ge = static_cast<QGestureEvent*>(e);
        if (auto* pinch = static_cast<QPinchGesture*>(ge->gesture(Qt::PinchGesture))) { if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) zoomAt(viewport()->mapFromGlobal(pinch->centerPoint().toPoint()), sc * pinch->scaleFactor()); return true; }
    }
    return QScrollArea::viewportEvent(e);
}
void Viewer::zoomAt(const QPointF& vp, double s) {
    const double old = sc; fit = 0; sc = std::clamp(s, 0.2, 6.0);
    if (std::abs(sc - old) < 1e-4) return;
    const double docX = horizontalScrollBar()->value() + vp.x(), docY = verticalScrollBar()->value() + vp.y();
    relayout();
    const double r = sc / old;
    horizontalScrollBar()->setValue(int(docX * r - vp.x())); verticalScrollBar()->setValue(int(docY * r - vp.y()));
}
void Viewer::clear() { for (auto* p : pages) { vbox->removeWidget(p); p->deleteLater(); } pages.clear(); doc.reset(); cur = 0; }
void Viewer::setDocument(std::shared_ptr<PdfDoc> d) {
    clear(); doc = d; if (!d) return;
    for (int i = 0; i < d->pages(); i++) { auto* pw = new PageWidget(d, i, this, content); pw->setStyleSheet("background:white;"); pages.push_back(pw); vbox->addWidget(pw, 0, Qt::AlignHCenter); }
    relayout();
}
void Viewer::setSearchHits(const QList<PdfDoc::SearchHit>& h, int current) { hits = h; currentHit = current; for (auto* p : pages) p->update(); }
void Viewer::gotoHit(int i) {
    if (i < 0 || i >= hits.size() || pages.empty()) return;
    currentHit = i; const auto& h = hits[i]; PageWidget* pw = pages[std::clamp(h.page, 0, int(pages.size()) - 1)];
    const double y = pw->geometry().top() + h.rects.first().top() * sc - viewport()->height() * 0.35;
    verticalScrollBar()->setValue(int(std::max(0.0, y))); for (auto* p : pages) p->update(); wantTimer.start();
}
void Viewer::setEditSession(EditSession* s) { edit.session = s; formPage = -1; for (auto* p : pages) { p->selected = -1; p->invalidate(); } emit selectionChanged(); }
void Viewer::refreshPage(int page) { if (page >= 0 && page < int(pages.size())) pages[page]->invalidate(); }
void Viewer::refreshAllPages() { for (auto* p : pages) p->invalidate(); }
bool Viewer::hasSelection() const { for (auto* p : pages) if (p->selected >= 0) return true; return false; }
void Viewer::deleteSelected() { if (!edit.session) return; for (auto* p : pages) if (p->selected >= 0) { edit.session->remove(p->index(), p->selected); p->selected = -1; p->invalidate(); } emit selectionChanged(); }
void Viewer::relayout() {
    if (!doc) return;
    double maxW = 1, maxH = 1; for (auto& s : doc->sizes) { maxW = std::max(maxW, s.width()); maxH = std::max(maxH, s.height()); }
    const int avail = width() - verticalScrollBar()->sizeHint().width() - 2;
    if (fit == 1) sc = (avail - PAD * 2) / maxW;
    else if (fit == 2) sc = std::min((avail - PAD * 2) / maxW, (viewport()->height() - PAD * 2) / maxH);
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
    cur = best; emit pageChanged(cur + 1, int(pages.size()));
}
void Viewer::zoomTo(double s) { zoomAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0), s); }
void Viewer::zoomStep(int dir) {
    static const double steps[] = {0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4};
    double next = sc;
    if (dir > 0) { for (double s : steps) if (s > sc + 1e-3) { next = s; break; } } else { for (int i = 14; i >= 0; i--) if (steps[i] < sc - 1e-3) { next = steps[i]; break; } }
    zoomTo(next);
}
void Viewer::fitWidth() { fit = 1; relayout(); }
void Viewer::fitPage() { fit = 2; relayout(); }
void Viewer::gotoPage(int n) { if (pages.empty()) return; n = std::clamp(n, 1, int(pages.size())); verticalScrollBar()->setValue(pages[n - 1]->geometry().top() - PAD); cur = n - 1; emit pageChanged(n, int(pages.size())); wantTimer.start(); }
void Viewer::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) { const double k = std::exp(e->angleDelta().y() / 600.0); zoomAt(e->position(), sc * k); e->accept(); return; }
    QScrollArea::wheelEvent(e);
}
void Viewer::keyPressEvent(QKeyEvent* e) {
    if (edit.session) {
        if (formPage >= 0 && formPage < int(pages.size())) {
            EditSession* s = edit.session; int k = 0;
            switch (e->key()) { case Qt::Key_Backspace: k = FWL_VKEY_Back; break; case Qt::Key_Delete: k = FWL_VKEY_Delete; break; case Qt::Key_Left: k = FWL_VKEY_Left; break; case Qt::Key_Right: k = FWL_VKEY_Right; break; case Qt::Key_Home: k = FWL_VKEY_Home; break; case Qt::Key_End: k = FWL_VKEY_End; break; case Qt::Key_Tab: k = FWL_VKEY_Tab; break; case Qt::Key_Return: case Qt::Key_Enter: k = FWL_VKEY_Return; break; case Qt::Key_Escape: s->killFocus(); formPage = -1; refreshAllPages(); return; default: break; }
            if (k) { s->formKey(formPage, k, 0); if (k == FWL_VKEY_Back || k == FWL_VKEY_Delete) { /* pdfium handles via OnKeyDown */ } }
            else if (!e->text().isEmpty() && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) { for (QChar c : e->text()) s->formChar(formPage, c.unicode()); }
            else { QScrollArea::keyPressEvent(e); return; }
            refreshPage(formPage); return;
        }
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) { deleteSelected(); return; }
        if (e->key() == Qt::Key_Escape) { for (auto* p : pages) { p->selected = -1; p->update(); } emit selectionChanged(); return; }
    }
    if (e->matches(QKeySequence::ZoomIn) || ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Equal)) { zoomStep(1); return; }
    if (e->matches(QKeySequence::ZoomOut)) { zoomStep(-1); return; }
    if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_0) { fitWidth(); return; }
    QScrollArea::keyPressEvent(e);
}
void Viewer::resizeEvent(QResizeEvent* e) { QScrollArea::resizeEvent(e); if (fit) relayout(); else wantTimer.start(); }
void Viewer::scrollContentsBy(int dx, int dy) { QScrollArea::scrollContentsBy(dx, dy); wantTimer.start(); }
