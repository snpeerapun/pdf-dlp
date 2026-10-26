#include "sidebar.h"
#include "icons.h"
#include "i18n.h"
#include <QtConcurrent>

static QThreadPool& thumbPool() { static QThreadPool p; p.setMaxThreadCount(1); return p; }

Sidebar::Sidebar(QWidget* parent) : QTabWidget(parent) {
    files = new QListWidget; files->setObjectName("files"); files->setContextMenuPolicy(Qt::CustomContextMenu); files->setIconSize(QSize(18, 18));
    auto* filesTab = new QWidget; auto* fv = new QVBoxLayout(filesTab); fv->setContentsMargins(0, 0, 0, 0); fv->setSpacing(0);
    auto* fbar = new QWidget; auto* fh = new QHBoxLayout(fbar); fh->setContentsMargins(12, 6, 8, 6); allFiles = new QCheckBox; allFiles->setTristate(false); filesLabel = new QLabel; filesLabel->setObjectName("hint"); fh->addWidget(allFiles); fh->addWidget(filesLabel, 1);
    fv->addWidget(fbar); fv->addWidget(files, 1);
    pagesTab = new QWidget; auto* v = new QVBoxLayout(pagesTab); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    auto* bar = new QWidget; auto* h = new QHBoxLayout(bar); h->setContentsMargins(10, 6, 8, 6);
    selLabel = new QLabel; selLabel->setObjectName("hint"); selAllBtn = new QPushButton; selAllBtn->setFlat(true); selClearBtn = new QPushButton; selClearBtn->setFlat(true); selClearBtn->setIcon(FA::icon(FA::Xmark, QColor("#6f7a89"), 14)); selClearBtn->hide();
    h->addWidget(selLabel, 1); h->addWidget(selAllBtn); h->addWidget(selClearBtn);
    thumbs = new QListWidget; thumbs->setObjectName("thumbs"); thumbs->setViewMode(QListView::IconMode); thumbs->setIconSize(QSize(140, 198)); thumbs->setResizeMode(QListView::Adjust); thumbs->setMovement(QListView::Static);
    thumbs->setSelectionMode(QAbstractItemView::ExtendedSelection); thumbs->setContextMenuPolicy(Qt::CustomContextMenu); thumbs->setSpacing(4); thumbs->setUniformItemSizes(true); thumbs->setWordWrap(false);
    v->addWidget(bar); v->addWidget(thumbs, 1);
    addTab(filesTab, ""); addTab(pagesTab, "");
    retranslate();
    connect(files, &QListWidget::currentRowChanged, this, [this](int r) { if (r >= 0) emit fileActivated(r); });
    connect(files, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) { const int n = checkedFiles().size(); filesLabel->setText(n ? QString("%1 %2").arg(n).arg(TXT("filesChecked")) : TXT("checkHint")); allFiles->blockSignals(true); allFiles->setChecked(n > 0 && n == files->count()); allFiles->blockSignals(false); emit fileChecked(files->row(it), it->checkState() == Qt::Checked); });
    connect(allFiles, &QCheckBox::toggled, this, [this](bool on) { for (int i = 0; i < files->count(); i++) files->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked); });
    connect(files, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) { auto* it = files->itemAt(p); if (it) emit fileContextMenu(files->row(it), files->viewport()->mapToGlobal(p)); });
    connect(thumbs, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { emit pageActivated(thumbs->row(it) + 1); });
    connect(thumbs, &QListWidget::itemSelectionChanged, this, [this]() { const int n = thumbs->selectedItems().size(); selLabel->setText(n ? QString("%1 %2").arg(n).arg(TXT("selected")) : TXT("selectPages")); selClearBtn->setVisible(n > 0); emit selectionChanged(); });
    connect(thumbs, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) { auto* it = thumbs->itemAt(p); if (!it) return; if (!it->isSelected()) { thumbs->clearSelection(); it->setSelected(true); } emit pageContextMenu(selectedPages(), thumbs->viewport()->mapToGlobal(p)); });
    connect(selAllBtn, &QPushButton::clicked, this, [this]() { thumbs->selectAll(); });
    connect(selClearBtn, &QPushButton::clicked, this, [this]() { thumbs->clearSelection(); });
}
void Sidebar::retranslate() { setTabText(0, TXT("files")); setTabText(1, TXT("pages")); selAllBtn->setText(TXT("selectAll")); selLabel->setText(TXT("selectPages")); allFiles->setText(TXT("selectAll")); filesLabel->setText(TXT("checkHint")); }
void Sidebar::setFiles(const QStringList& names, const QList<bool>& modified, const QList<bool>& checked, int active) {
    files->blockSignals(true); files->clear();
    for (int i = 0; i < names.size(); i++) { auto* it = new QListWidgetItem(FA::icon(FA::FilePdf, QColor("#e5484d"), 18), names[i] + (modified.value(i) ? "  •" : "")); it->setFlags(it->flags() | Qt::ItemIsUserCheckable); it->setCheckState(checked.value(i) ? Qt::Checked : Qt::Unchecked); files->addItem(it); }
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
    for (int i = 0; i < d->pages(); i++) { auto* it = new QListWidgetItem(QIcon(ph), QString::number(i + 1)); it->setTextAlignment(Qt::AlignHCenter); it->setSizeHint(QSize(190, 226)); thumbs->addItem(it); }
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
    thumbs->selectionModel()->setCurrentIndex(thumbs->model()->index(oneBased - 1, 0), QItemSelectionModel::NoUpdate);
    thumbs->scrollToItem(thumbs->item(oneBased - 1));
}
QList<int> Sidebar::selectedPages() const { QList<int> out; for (auto* it : thumbs->selectedItems()) out << thumbs->row(it) + 1; std::sort(out.begin(), out.end()); return out; }
