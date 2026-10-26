#include "sidebar.h"
#include "icons.h"
#include "i18n.h"
#include <QtConcurrent>

class FileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        QStyleOptionViewItem o(opt); o.state &= ~QStyle::State_Selected; o.state &= ~QStyle::State_HasFocus;
        const QRectF r = QRectF(opt.rect).adjusted(5, 2, -5, -2);
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        if (opt.state & QStyle::State_Selected) {
            QLinearGradient g(r.topLeft(), r.topRight()); g.setColorAt(0, QColor("#ff5f6d")); g.setColorAt(0.25, QColor("#ffc371")); g.setColorAt(0.5, QColor("#34c77b")); g.setColorAt(0.75, QColor("#4f8cff")); g.setColorAt(1, QColor("#7b5cff"));
            p->setPen(QPen(QBrush(g), 2)); p->setBrush(QColor("#f3f7ff")); p->drawRoundedRect(r, 8, 8);
        } else if (opt.state & QStyle::State_MouseOver) { p->setPen(Qt::NoPen); p->setBrush(QColor("#f1f4f9")); p->drawRoundedRect(r, 8, 8); }
        p->restore();
        o.rect = opt.rect.adjusted(8, 0, -8, 0);
        QStyledItemDelegate::paint(p, o, idx);
    }
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override { QSize s = QStyledItemDelegate::sizeHint(opt, idx); s.setHeight(38); return s; }
};

// Chrome-style page thumbnails: floating page with shadow, number below, RGB gradient border when selected.
class ThumbDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        p->save(); p->setRenderHint(QPainter::Antialiasing); p->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect r = opt.rect.adjusted(6, 6, -6, -6);
        const bool selected = opt.state & QStyle::State_Selected, current = idx.data(Qt::UserRole + 1).toBool(), hover = opt.state & QStyle::State_MouseOver;
        if (selected) {
            QLinearGradient g(r.topLeft(), r.bottomRight()); g.setColorAt(0, QColor("#ff5f6d")); g.setColorAt(0.25, QColor("#ffc371")); g.setColorAt(0.5, QColor("#34c77b")); g.setColorAt(0.75, QColor("#4f8cff")); g.setColorAt(1, QColor("#7b5cff"));
            p->setPen(QPen(QBrush(g), 2.5)); p->setBrush(QColor(255, 255, 255, 160)); p->drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 10, 10);
        } else if (current) { p->setPen(QPen(QColor("#4f8cff"), 1.5)); p->setBrush(QColor("#f3f7ff")); p->drawRoundedRect(QRectF(r).adjusted(1, 1, -1, -1), 10, 10); }
        else if (hover) { p->setPen(Qt::NoPen); p->setBrush(QColor("#eef2f8")); p->drawRoundedRect(r, 10, 10); }
        const QPixmap pm = idx.data(Qt::DecorationRole).value<QIcon>().pixmap(QSize(150, 212));
        const QRect imgArea(r.left() + 10, r.top() + 10, r.width() - 20, r.height() - 36);
        if (!pm.isNull()) {
            QSize sz = pm.size() / pm.devicePixelRatio(); sz.scale(imgArea.size(), Qt::KeepAspectRatio);
            const QRect ir(imgArea.center().x() - sz.width() / 2, imgArea.top() + (imgArea.height() - sz.height()) / 2, sz.width(), sz.height());
            p->setPen(Qt::NoPen); p->setBrush(QColor(0, 0, 0, 28)); p->drawRoundedRect(ir.translated(0, 2).adjusted(-1, -1, 1, 3), 3, 3); // shadow
            p->drawPixmap(ir, pm);
            p->setPen(QPen(QColor(0, 0, 0, 25), 1)); p->setBrush(Qt::NoBrush); p->drawRect(ir);
        }
        QFont f = opt.font; f.setPixelSize(12); f.setWeight(selected || current ? QFont::DemiBold : QFont::Normal); p->setFont(f);
        p->setPen(selected || current ? QColor("#1f2733") : QColor("#6f7a89"));
        p->drawText(QRect(r.left(), r.bottom() - 24, r.width(), 20), Qt::AlignCenter, idx.data(Qt::DisplayRole).toString());
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(196, 262); }
};

static QThreadPool& thumbPool() { static QThreadPool p; p.setMaxThreadCount(1); return p; }

Sidebar::Sidebar(QWidget* parent) : QTabWidget(parent) {
    files = new QListWidget; files->setObjectName("files"); files->setContextMenuPolicy(Qt::CustomContextMenu); files->setIconSize(QSize(18, 18)); files->setTextElideMode(Qt::ElideMiddle); files->setItemDelegate(new FileDelegate(files)); files->setMouseTracking(true); files->setWordWrap(false); files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); files->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* filesTab = new QWidget; auto* fv = new QVBoxLayout(filesTab); fv->setContentsMargins(0, 0, 0, 0); fv->setSpacing(0);
    auto* fbar = new QWidget; auto* fh = new QVBoxLayout(fbar); fh->setContentsMargins(12, 6, 8, 4); fh->setSpacing(2); allFiles = new QCheckBox; allFiles->setTristate(false); filesLabel = new QLabel; filesLabel->setObjectName("hint"); filesLabel->setWordWrap(true); fh->addWidget(allFiles); fh->addWidget(filesLabel);
    fv->addWidget(fbar); fv->addWidget(files, 1);
    pagesTab = new QWidget; auto* v = new QVBoxLayout(pagesTab); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    auto* bar = new QWidget; auto* h = new QHBoxLayout(bar); h->setContentsMargins(10, 6, 8, 6);
    selLabel = new QLabel; selLabel->setObjectName("hint"); selLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); selAllBtn = new QPushButton; selAllBtn->setFlat(true); selAllBtn->setIcon(FA::icon(FA::SquareCheck, QColor("#4f8cff"), 14)); selAllBtn->setFixedSize(30, 28); selClearBtn = new QPushButton; selClearBtn->setFlat(true); selClearBtn->setIcon(FA::icon(FA::Xmark, QColor("#6f7a89"), 14)); selClearBtn->setFixedSize(30, 28); selClearBtn->hide();
    h->addWidget(selLabel, 1); h->addWidget(selAllBtn); h->addWidget(selClearBtn);
    thumbs = new QListWidget; thumbs->setObjectName("thumbs"); thumbs->setViewMode(QListView::IconMode); thumbs->setIconSize(QSize(140, 198)); thumbs->setResizeMode(QListView::Adjust); thumbs->setMovement(QListView::Static);
    thumbs->setSelectionMode(QAbstractItemView::ExtendedSelection); thumbs->setContextMenuPolicy(Qt::CustomContextMenu); thumbs->setSpacing(2); thumbs->setUniformItemSizes(true); thumbs->setWordWrap(false); thumbs->setItemDelegate(new ThumbDelegate(thumbs)); thumbs->setMouseTracking(true); thumbs->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    v->addWidget(bar); v->addWidget(thumbs, 1);
    addTab(filesTab, ""); addTab(pagesTab, ""); setDocumentMode(true); tabBar()->setExpanding(true); tabBar()->setDrawBase(false);
    retranslate();
    connect(files, &QListWidget::currentRowChanged, this, [this](int r) { if (r >= 0) emit fileActivated(r); });
    connect(files, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) { const int n = checkedFiles().size(); filesLabel->setText(n ? QString("%1 %2").arg(n).arg(TXT("filesChecked")) : TXT("checkHint")); allFiles->blockSignals(true); allFiles->setChecked(n > 0 && n == files->count()); allFiles->blockSignals(false); emit fileChecked(files->row(it), it->checkState() == Qt::Checked); });
    connect(allFiles, &QCheckBox::toggled, this, [this](bool on) { for (int i = 0; i < files->count(); i++) files->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked); });
    connect(files, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) { auto* it = files->itemAt(p); if (it) emit fileContextMenu(files->row(it), files->viewport()->mapToGlobal(p)); });
    connect(thumbs, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { emit pageActivated(thumbs->row(it) + 1); });
    connect(thumbs, &QListWidget::itemSelectionChanged, this, [this]() { const int n = thumbs->selectedItems().size(); selLabel->setText(n ? QString("%1 %2").arg(n).arg(TXT("selected")) : TXT("selectPagesShort")); selClearBtn->setVisible(n > 0); emit selectionChanged(); });
    connect(thumbs, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) { auto* it = thumbs->itemAt(p); if (!it) return; if (!it->isSelected()) { thumbs->clearSelection(); it->setSelected(true); } emit pageContextMenu(selectedPages(), thumbs->viewport()->mapToGlobal(p)); });
    connect(selAllBtn, &QPushButton::clicked, this, [this]() { thumbs->selectAll(); });
    connect(selClearBtn, &QPushButton::clicked, this, [this]() { thumbs->clearSelection(); });
}
void Sidebar::retranslate() { setTabText(0, TXT("files")); setTabText(1, TXT("pages")); selAllBtn->setToolTip(TXT("selectAll")); selClearBtn->setToolTip(TXT("clearSel")); selLabel->setText(TXT("selectPages")); allFiles->setText(TXT("selectAll")); filesLabel->setText(TXT("checkHint")); }
void Sidebar::setFiles(const QStringList& names, const QStringList& paths, const QList<bool>& modified, const QList<bool>& checked, int active) {
    files->blockSignals(true); files->clear();
    for (int i = 0; i < names.size(); i++) { auto* it = new QListWidgetItem(FA::icon(FA::FilePdf, QColor("#e5484d"), 18), names[i] + (modified.value(i) ? "  •" : "")); it->setFlags(it->flags() | Qt::ItemIsUserCheckable); it->setCheckState(checked.value(i) ? Qt::Checked : Qt::Unchecked); it->setToolTip(paths.value(i, names[i])); files->addItem(it); }
    if (active >= 0 && active < names.size()) files->setCurrentRow(active);
    files->blockSignals(false);
    const int n = checkedFiles().size(); filesLabel->setText(n ? QString("%1 %2").arg(n).arg(TXT("filesChecked")) : TXT("checkHint"));
    allFiles->blockSignals(true); allFiles->setChecked(n > 0 && n == names.size()); allFiles->setEnabled(!names.isEmpty()); allFiles->blockSignals(false);
    setTabText(0, QString("%1 (%2)").arg(TXT("files")).arg(names.size()));
}
QList<int> Sidebar::checkedFiles() const { QList<int> out; for (int i = 0; i < files->count(); i++) if (files->item(i)->checkState() == Qt::Checked) out << i; return out; }
void Sidebar::setDocument(std::shared_ptr<PdfDoc> d, const QString& key) {
    if (key == docKey && d == doc) return;
    doc = d; docKey = key; const int myGen = ++gen;
    thumbs->clear();
    if (!d) { setTabText(1, TXT("pages")); return; }
    setTabText(1, QString("%1 (%2)").arg(TXT("pages")).arg(d->pages()));
    QPixmap ph(140, 198); ph.fill(QColor("#eef2f8"));
    for (int i = 0; i < d->pages(); i++) { auto* it = new QListWidgetItem(QIcon(ph), QString::number(i + 1)); it->setTextAlignment(Qt::AlignHCenter); it->setSizeHint(QSize(196, 262)); thumbs->addItem(it); }
    QPointer<Sidebar> self(this);
    for (int i = 0; i < d->pages(); i++) {
        thumbPool().start(QRunnable::create([=]() {
            if (!self || self->gen != myGen) return;
            QImage img = d->render(i, 140, 2.0);
            QMetaObject::invokeMethod(qApp, [=]() { if (!self || self->gen != myGen || i >= self->thumbs->count()) return; QPixmap pm = QPixmap::fromImage(img); self->thumbs->item(i)->setIcon(QIcon(pm)); });
        }));
    }
}
void Sidebar::setCurrentPage(int oneBased) {
    if (oneBased < 1 || oneBased > thumbs->count()) return;
    for (int i = 0; i < thumbs->count(); i++) { const bool on = i == oneBased - 1; if (thumbs->item(i)->data(Qt::UserRole + 1).toBool() != on) thumbs->item(i)->setData(Qt::UserRole + 1, on); }
    thumbs->selectionModel()->setCurrentIndex(thumbs->model()->index(oneBased - 1, 0), QItemSelectionModel::NoUpdate);
    thumbs->scrollToItem(thumbs->item(oneBased - 1));
}
QList<int> Sidebar::selectedPages() const { QList<int> out; for (auto* it : thumbs->selectedItems()) out << thumbs->row(it) + 1; std::sort(out.begin(), out.end()); return out; }
