#include "home.h"
#include "icons.h"
#include "i18n.h"

static QString thumbDir() { const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recent"; QDir().mkpath(d); return d; }
static QString thumbPath(const QString& p) { return thumbDir() + "/" + QString::fromLatin1(QCryptographicHash::hash(p.toUtf8(), QCryptographicHash::Md5).toHex()) + ".png"; }

QList<RecentEntry> HomeView::recents() {
    QList<RecentEntry> out;
    for (const auto& v : QJsonDocument::fromJson(QSettings().value("recent").toByteArray()).array()) {
        const QJsonObject o = v.toObject(); RecentEntry e; e.path = o.value("path").toString(); e.name = QFileInfo(e.path).fileName(); e.pages = o.value("pages").toInt(); e.openedAt = QDateTime::fromString(o.value("at").toString(), Qt::ISODate); e.thumb = thumbPath(e.path); e.pinned = o.value("pinned").toBool();
        if (QFileInfo::exists(e.path)) out << e;
    }
    return out;
}
static void saveRecents(const QList<RecentEntry>& l) { QJsonArray a; for (const auto& e : l) a.append(QJsonObject{{"path", e.path}, {"pages", e.pages}, {"at", e.openedAt.toString(Qt::ISODate)}, {"pinned", e.pinned}}); QSettings().setValue("recent", QJsonDocument(a).toJson(QJsonDocument::Compact)); }
void HomeView::remember(const QString& path, int pages, const QImage& firstPage) {
    QList<RecentEntry> l = recents(); bool pinned = false; for (const auto& e : l) if (e.path == path) pinned = e.pinned;
    l.erase(std::remove_if(l.begin(), l.end(), [&](const RecentEntry& e) { return e.path == path; }), l.end());
    RecentEntry e; e.path = path; e.name = QFileInfo(path).fileName(); e.pages = pages; e.openedAt = QDateTime::currentDateTime(); e.pinned = pinned; l.prepend(e);
    for (int i = l.size() - 1; i >= 0 && l.size() > 24; i--) if (!l[i].pinned) { QFile::remove(thumbPath(l[i].path)); l.removeAt(i); }
    if (!firstPage.isNull()) firstPage.save(thumbPath(path), "PNG");
    saveRecents(l);
}
void HomeView::setPinned(const QString& path, bool on) { QList<RecentEntry> l = recents(); for (auto& e : l) if (e.path == path) e.pinned = on; saveRecents(l); }
void HomeView::forget(const QString& path) { QList<RecentEntry> l = recents(); l.erase(std::remove_if(l.begin(), l.end(), [&](const RecentEntry& e) { return e.path == path; }), l.end()); QFile::remove(thumbPath(path)); saveRecents(l); }

class Card : public QFrame {
public:
    RecentEntry e;
    Card(const RecentEntry& entry, HomeView* home) : e(entry) {
        setObjectName("card"); setCursor(Qt::PointingHandCursor); setFixedSize(210, 262);
        setStyleSheet("QFrame#card{background:#fff;border:1px solid #e3e8ef;border-radius:12px} QFrame#card:hover{border-color:#4f8cff;background:#f7faff} QLabel{border:0;background:transparent}");
        auto* v = new QVBoxLayout(this); v->setContentsMargins(12, 12, 12, 10); v->setSpacing(6);
        auto* img = new QLabel; img->setFixedSize(186, 170); img->setAlignment(Qt::AlignCenter); img->setStyleSheet("background:#eef2f8;border-radius:8px;");
        QPixmap pm(e.thumb); if (!pm.isNull()) { pm.setDevicePixelRatio(2.0); img->setPixmap(pm.scaled(QSize(186, 170) * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation)); } else { img->setText(QString(QChar(FA::FilePdf))); img->setStyleSheet(QString("background:#eef2f8;border-radius:8px;font-family:\"%1\";font-weight:900;font-size:40px;color:#c5cfdc").arg(FA::font())); }
        auto* name = new QLabel(e.name); name->setStyleSheet("font-weight:600;"); name->setToolTip(e.path); QFontMetrics fm(name->font()); name->setText(fm.elidedText(e.name, Qt::ElideMiddle, 184));
        auto* meta = new QLabel(QString("%1 %2 · %3").arg(e.pages).arg(TXT("pages")).arg(e.openedAt.toString("d MMM HH:mm"))); meta->setStyleSheet("color:#6f7a89;font-size:11.5px;");
        v->addWidget(img); v->addWidget(name); v->addWidget(meta);
        setContextMenuPolicy(Qt::CustomContextMenu);
        auto* pin = new QPushButton(this); pin->setFlat(true); pin->setFixedSize(26, 26); pin->setCursor(Qt::PointingHandCursor); pin->setToolTip(e.pinned ? TXT("unpin") : TXT("pin"));
        pin->setIcon(FA::icon(FA::Star, e.pinned ? QColor("#f4b63f") : QColor("#c5cfdc"), 15)); pin->move(174, 10); pin->setStyleSheet("background:rgba(255,255,255,.85);border-radius:13px;");
        connect(pin, &QPushButton::clicked, this, [this, home]() { HomeView::setPinned(e.path, !e.pinned); home->refresh(); });
        connect(this, &QWidget::customContextMenuRequested, this, [this, home](const QPoint& p) { QMenu m; auto* pn = m.addAction(FA::icon(FA::Star, QColor("#f4b63f"), 14), e.pinned ? TXT("unpin") : TXT("pin")); auto* rv = m.addAction(FA::icon(FA::FolderOpen, QColor("#6f7a89"), 14), TXT("revealInFolder")); auto* rm = m.addAction(FA::icon(FA::Xmark, QColor("#e5484d"), 14), TXT("removeRecent")); auto* r = m.exec(mapToGlobal(p)); if (r == pn) { HomeView::setPinned(e.path, !e.pinned); home->refresh(); } else if (r == rv) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(e.path).absolutePath())); else if (r == rm) { HomeView::forget(e.path); home->refresh(); } });
    }
protected:
    void mouseReleaseEvent(QMouseEvent* ev) override { if (ev->button() == Qt::LeftButton && rect().contains(ev->pos())) emit static_cast<HomeView*>(window()->findChild<HomeView*>())->fileRequested(e.path); }
};

HomeView::HomeView(QWidget* parent) : QScrollArea(parent) {
    setFrameShape(QFrame::NoFrame); setWidgetResizable(true); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body = new QWidget; body->setStyleSheet("background:#eef2f7;"); setWidget(body);
    auto* v = new QVBoxLayout(body); v->setContentsMargins(36, 28, 36, 28); v->setSpacing(18); v->setAlignment(Qt::AlignTop);
    dropBox = new QFrame; dropBox->setStyleSheet("QFrame{background:#fff;border:2px dashed #c5cfdc;border-radius:16px;} QLabel{border:0;background:transparent}"); auto* dv = new QHBoxLayout(dropBox); dv->setContentsMargins(28, 22, 28, 22); dv->setSpacing(18);
    auto* ic = FA::label(FA::FilePdf, QColor("#4f8cff"), 40); auto* tv = new QVBoxLayout; title = new QLabel; title->setStyleSheet("font-size:18px;font-weight:600;"); hint = new QLabel; hint->setStyleSheet("color:#6f7a89"); hint->setWordWrap(true); tv->addWidget(title); tv->addWidget(hint);
    openBtn = new QPushButton; openBtn->setObjectName("primary"); openBtn->setIcon(FA::icon(FA::FolderOpen, Qt::white, 16)); openBtn->setMinimumHeight(38);
    dv->addWidget(ic); dv->addLayout(tv, 1); dv->addWidget(openBtn); v->addWidget(dropBox);
    pinTitle = new QLabel; pinTitle->setStyleSheet("font-size:15px;font-weight:600;"); v->addWidget(pinTitle);
    pinHost = new QWidget; pinHost->setStyleSheet("background:transparent;"); pinGrid = new QGridLayout(pinHost); pinGrid->setContentsMargins(0, 0, 0, 0); pinGrid->setSpacing(16); pinGrid->setAlignment(Qt::AlignLeft | Qt::AlignTop); v->addWidget(pinHost);
    recentTitle = new QLabel; recentTitle->setStyleSheet("font-size:15px;font-weight:600;"); v->addWidget(recentTitle);
    cardsHost = new QWidget; cardsHost->setStyleSheet("background:transparent;"); grid = new QGridLayout(cardsHost); grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(16); grid->setAlignment(Qt::AlignLeft | Qt::AlignTop); v->addWidget(cardsHost);
    connect(openBtn, &QPushButton::clicked, this, &HomeView::openRequested);
    retranslate(); refresh();
}
void HomeView::retranslate() { title->setText(TXT("dropTitle")); hint->setText(TXT("dropHint")); openBtn->setText(TXT("open")); recentTitle->setText(TXT("recentFiles")); pinTitle->setText(TXT("pinnedFiles")); refresh(); }
void HomeView::refresh() { items = recents(); relayout(); }
void HomeView::relayout() {
    while (auto* it = grid->takeAt(0)) { delete it->widget(); delete it; }
    while (auto* it = pinGrid->takeAt(0)) { delete it->widget(); delete it; }
    const int cols = std::max(1, (viewport()->width() - 72 + 16) / (210 + 16));
    int np = 0, nr = 0;
    for (const RecentEntry& e : items) { if (e.pinned) { pinGrid->addWidget(new Card(e, this), np / cols, np % cols); np++; } else { grid->addWidget(new Card(e, this), nr / cols, nr % cols); nr++; } }
    pinTitle->setVisible(np > 0); pinHost->setVisible(np > 0); recentTitle->setVisible(nr > 0);
}
void HomeView::resizeEvent(QResizeEvent* e) { QScrollArea::resizeEvent(e); const int cols = std::max(1, (viewport()->width() - 72 + 16) / (210 + 16)); if (grid->count() && grid->columnCount() != cols) relayout(); }
