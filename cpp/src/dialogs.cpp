#include "dialogs.h"
#include "i18n.h"
#include "icons.h"
#include "stamps.h"
#include "core.h"

namespace Dialogs {
QDialog* make(QWidget* parent, const QString& title, int width) {
    auto* d = new QDialog(parent); d->setWindowTitle(title); d->setMinimumWidth(width); d->setModal(true);
    d->setAttribute(Qt::WA_DeleteOnClose, false);
    auto* v = new QVBoxLayout(d); v->setContentsMargins(18, 16, 18, 16); v->setSpacing(10);
    return d;
}
QPushButton* primary(const QString& text) { auto* b = new QPushButton(text); b->setObjectName("primary"); b->setDefault(true); return b; }
static QVBoxLayout* body(QDialog* d) { return static_cast<QVBoxLayout*>(d->layout()); }
static QLabel* hint(const QString& t) { auto* l = new QLabel(t); l->setObjectName("hint"); l->setWordWrap(true); return l; }
static QHBoxLayout* footer(QDialog* d, QPushButton* btn) { auto* h = new QHBoxLayout; h->addStretch(); h->addWidget(btn); body(d)->addLayout(h); QObject::connect(btn, &QPushButton::clicked, d, &QDialog::accept); return h; }
static QJsonObject stampOp(const QList<int>& pages, const Stamps::Png& png, double x, double y, double w, double opacity, double rot, const char* anchor) {
    QJsonArray pg; for (int p : pages) pg.append(p);
    return QJsonObject{{"op", "stamp"}, {"pages", pg}, {"png_base64", png.base64}, {"x", x}, {"y", y}, {"w", w}, {"opacity", opacity}, {"rotate_deg", rot}, {"anchor", anchor}};
}
static QJsonArray toArr(const QList<int>& l) { QJsonArray a; for (int v : l) a.append(v); return a; }

QString password(QWidget* parent, const QString& fileName, bool wrong) {
    auto* d = make(parent, TXT("password"), 420);
    body(d)->addWidget(new QLabel(TXT("passwordNeeded"))); body(d)->addWidget(hint(fileName));
    auto* e = new QLineEdit; e->setEchoMode(QLineEdit::Password); body(d)->addWidget(e);
    if (wrong) { auto* b = new QLabel(TXT("wrongPassword")); b->setObjectName("badgeDanger"); body(d)->addWidget(b, 0, Qt::AlignLeft); }
    footer(d, primary(TXT("unlock")));
    QObject::connect(e, &QLineEdit::returnPressed, d, &QDialog::accept);
    const QString r = d->exec() == QDialog::Accepted ? e->text() : QString(); d->deleteLater(); return r.isNull() ? QString() : r;
}

EncryptResult encrypt(QWidget* parent) {
    EncryptResult r; auto* d = make(parent, TXT("encrypt"));
    auto* form = new QFormLayout; auto* user = new QLineEdit; user->setEchoMode(QLineEdit::Password); auto* owner = new QLineEdit; owner->setEchoMode(QLineEdit::Password); owner->setPlaceholderText(TXT("optional"));
    form->addRow(TXT("userPassword"), user); form->addRow(TXT("ownerPassword"), owner); body(d)->addLayout(form); body(d)->addWidget(hint(TXT("encryptHint")));
    auto* cp = new QCheckBox(TXT("perm_print")); cp->setChecked(true); auto* cc = new QCheckBox(TXT("perm_copy")); auto* cm = new QCheckBox(TXT("perm_modify")); auto* ca = new QCheckBox(TXT("perm_annotate"));
    auto* g = new QGridLayout; g->addWidget(cp, 0, 0); g->addWidget(cc, 0, 1); g->addWidget(cm, 1, 0); g->addWidget(ca, 1, 1); body(d)->addLayout(g);
    footer(d, primary(TXT("apply")));
    if (d->exec() == QDialog::Accepted && (!user->text().isEmpty() || !owner->text().isEmpty())) {
        r.ok = true; r.nextPassword = user->text().isEmpty() ? owner->text() : user->text();
        r.ops.append(QJsonObject{{"op", "encrypt"}, {"user_password", user->text()}, {"owner_password", owner->text()}, {"allow_print", cp->isChecked()}, {"allow_copy", cc->isChecked()}, {"allow_modify", cm->isChecked()}, {"allow_annotate", ca->isChecked()}});
    }
    d->deleteLater(); return r;
}
bool decrypt(QWidget* parent, const Document& doc, QJsonArray& ops) {
    auto* d = make(parent, TXT("decrypt"), 420);
    body(d)->addWidget(new QLabel(doc.info.value("encrypted").toBool() || !doc.password.isEmpty() ? TXT("decryptHint") : TXT("notEncrypted")));
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted; d->deleteLater();
    if (ok) ops.append(QJsonObject{{"op", "decrypt"}}); return ok;
}
bool split(QWidget* parent, const Document& doc, QString& mode, QString& spec, unsigned& every, QString& outDir) {
    auto* d = make(parent, TXT("split"));
    auto* combo = new QComboBox; combo->addItems({TXT("splitRanges"), TXT("splitEach"), TXT("splitEvery")});
    auto* specEdit = new QLineEdit("1-1"); specEdit->setPlaceholderText("1-3, 4-6, 7-"); auto* everySpin = new QSpinBox; everySpin->setRange(1, 9999); everySpin->setValue(1);
    auto* form = new QFormLayout; form->addRow(TXT("format"), combo); form->addRow(QString("%1 (1-%2)").arg(TXT("splitRangesHint")).arg(doc.pages()), specEdit); form->addRow(TXT("pagesPerFile"), everySpin); body(d)->addLayout(form);
    auto* btn = primary(TXT("chooseFolderAndSplit")); btn->setIcon(FA::icon(FA::FolderOpen, Qt::white, 16)); footer(d, btn);
    if (d->exec() != QDialog::Accepted) { d->deleteLater(); return false; }
    d->deleteLater();
    outDir = QFileDialog::getExistingDirectory(parent, TXT("chooseFolder")); if (outDir.isEmpty()) return false;
    mode = combo->currentIndex() == 0 ? "ranges" : combo->currentIndex() == 1 ? "each" : "every"; spec = specEdit->text(); every = unsigned(everySpin->value()); return true;
}
bool merge(QWidget* parent, QStringList& paths, const QStringList& openNames, const QStringList& openPaths) {
    auto* d = make(parent, TXT("merge"), 560); body(d)->addWidget(hint(TXT("mergeHint")));
    auto* list = new QListWidget; list->setDragDropMode(QAbstractItemView::InternalMove); list->setMinimumHeight(180);
    for (int i = 0; i < openPaths.size(); i++) { auto* it = new QListWidgetItem(FA::icon(FA::FilePdf, QColor("#e5484d"), 16), openNames[i]); it->setData(Qt::UserRole, openPaths[i]); list->addItem(it); }
    body(d)->addWidget(list);
    auto* row = new QHBoxLayout; auto* add = new QPushButton(TXT("addFiles")); add->setIcon(FA::icon(FA::Plus, QColor("#4f8cff"), 14)); auto* rm = new QPushButton(TXT("deletePages").left(0) + "✕"); auto* up = new QPushButton("↑"); auto* dn = new QPushButton("↓");
    row->addWidget(add); row->addStretch(); row->addWidget(up); row->addWidget(dn); row->addWidget(rm); body(d)->addLayout(row);
    QObject::connect(add, &QPushButton::clicked, d, [=]() { for (const QString& f : QFileDialog::getOpenFileNames(d, TXT("addFiles"), QString(), "PDF (*.pdf)")) { auto* it = new QListWidgetItem(FA::icon(FA::FilePdf, QColor("#e5484d"), 16), QFileInfo(f).fileName()); it->setData(Qt::UserRole, f); list->addItem(it); } });
    QObject::connect(rm, &QPushButton::clicked, d, [=]() { qDeleteAll(list->selectedItems()); });
    QObject::connect(up, &QPushButton::clicked, d, [=]() { const int r = list->currentRow(); if (r > 0) { auto* it = list->takeItem(r); list->insertItem(r - 1, it); list->setCurrentItem(it); } });
    QObject::connect(dn, &QPushButton::clicked, d, [=]() { const int r = list->currentRow(); if (r >= 0 && r < list->count() - 1) { auto* it = list->takeItem(r); list->insertItem(r + 1, it); list->setCurrentItem(it); } });
    footer(d, primary(TXT("merge")));
    const bool ok = d->exec() == QDialog::Accepted;
    paths.clear(); for (int i = 0; i < list->count(); i++) paths << list->item(i)->data(Qt::UserRole).toString();
    d->deleteLater(); return ok && !paths.isEmpty();
}
bool insertPages(QWidget* parent, const Document& doc, int currentPage, QString& file, int& afterPage) {
    auto* d = make(parent, TXT("insert"));
    auto* fileLbl = new QLabel; fileLbl->setObjectName("hint"); auto* pick = new QPushButton(TXT("chooseFile")); pick->setIcon(FA::icon(FA::FolderOpen, QColor("#4f8cff"), 14));
    auto* row = new QHBoxLayout; row->addWidget(pick); row->addWidget(fileLbl, 1); body(d)->addLayout(row);
    auto* spin = new QSpinBox; spin->setRange(0, doc.pages()); spin->setValue(currentPage); auto* form = new QFormLayout; form->addRow(TXT("insertAfter"), spin); body(d)->addLayout(form);
    QObject::connect(pick, &QPushButton::clicked, d, [=, &file]() { const QString f = QFileDialog::getOpenFileName(d, TXT("chooseFile"), QString(), "PDF (*.pdf)"); if (!f.isEmpty()) { file = f; fileLbl->setText(QFileInfo(f).fileName()); } });
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted && !file.isEmpty(); afterPage = spin->value(); d->deleteLater(); return ok;
}
bool watermark(QWidget* parent, const QList<int>& selected, QJsonArray& ops) {
    auto* d = make(parent, TXT("watermark"));
    auto* text = new QLineEdit("CONFIDENTIAL"); auto* color = new QLineEdit("#d12d2d"); auto* imgLbl = new QLabel; imgLbl->setObjectName("hint"); QString imgPath;
    auto* pickImg = new QPushButton(TXT("image")); pickImg->setIcon(FA::icon(FA::Images, QColor("#9b5cff"), 14));
    auto* opacity = new QSlider(Qt::Horizontal); opacity->setRange(5, 100); opacity->setValue(22); auto* size = new QSlider(Qt::Horizontal); size->setRange(10, 100); size->setValue(70); auto* angle = new QSlider(Qt::Horizontal); angle->setRange(-90, 90); angle->setValue(45);
    auto* scope = new QComboBox; scope->addItems({TXT("allPages"), selected.isEmpty() ? TXT("selectedPages") : QString("%1 (%2)").arg(TXT("selectedPages")).arg(selected.size())});
    auto* form = new QFormLayout; form->addRow(TXT("text"), text); form->addRow(TXT("color"), color); auto* ir = new QHBoxLayout; ir->addWidget(pickImg); ir->addWidget(imgLbl, 1); form->addRow(TXT("image"), ir);
    form->addRow(TXT("opacity"), opacity); form->addRow(TXT("sizePct"), size); form->addRow(TXT("angle"), angle); form->addRow(TXT("pages"), scope); body(d)->addLayout(form);
    QObject::connect(pickImg, &QPushButton::clicked, d, [=, &imgPath]() { const QString f = QFileDialog::getOpenFileName(d, TXT("image"), QString(), "Images (*.png *.jpg *.jpeg *.webp)"); if (!f.isEmpty()) { imgPath = f; imgLbl->setText(QFileInfo(f).fileName()); } });
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) {
        const Stamps::Png png = imgPath.isEmpty() ? Stamps::watermark(text->text(), QColor(color->text())) : Stamps::fromImageFile(imgPath);
        ops.append(stampOp(scope->currentIndex() == 0 ? QList<int>{} : selected, png, 0.5, 0.5, size->value() / 100.0, opacity->value() / 100.0, angle->value(), "center"));
    }
    d->deleteLater(); return ok;
}
bool pageNumbers(QWidget* parent, const Document& doc, QJsonArray& ops) {
    auto* d = make(parent, TXT("pageNumbers"));
    auto* fmt = new QLineEdit("{n} / {total}"); auto* start = new QSpinBox; start->setRange(1, 99999); auto* pos = new QComboBox; const char* keys[] = {"pos_bc", "pos_br", "pos_bl", "pos_tc", "pos_tr", "pos_tl"}; for (auto k : keys) pos->addItem(TXT(k));
    auto* header = new QLineEdit; auto* footerTxt = new QLineEdit;
    auto* form = new QFormLayout; form->addRow(TXT("format"), fmt); form->addRow(TXT("startAt"), start); form->addRow(TXT("position"), pos); form->addRow(TXT("headerText"), header); form->addRow(TXT("footerText"), footerTxt); body(d)->addLayout(form);
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) {
        const int pi = pos->currentIndex(); const bool bottom = pi < 3; const int align = pi % 3; // 0 c, 1 r, 2 l
        for (int p = 1; p <= doc.pages(); p++) {
            const QString txt = QString(fmt->text()).replace("{n}", QString::number(p + start->value() - 1)).replace("{total}", QString::number(doc.pages() + start->value() - 1));
            const Stamps::Png png = Stamps::text(txt, QColor("#222222"), 36);
            const double w = std::min(0.3, 0.012 * txt.size() + 0.03), h = w * png.aspect;
            const double x = align == 0 ? 0.5 - w / 2 : align == 1 ? 0.97 - w : 0.03; const double y = bottom ? 0.985 - h : 0.015;
            ops.append(stampOp({p}, png, x, y, w, 1.0, 0, "topleft"));
        }
        if (!header->text().isEmpty()) { const auto png = Stamps::text(header->text(), QColor("#333333"), 32); const double w = std::min(0.6, 0.011 * header->text().size() + 0.03); ops.append(stampOp({}, png, 0.5 - w / 2, 0.012, w, 1.0, 0, "topleft")); }
        if (!footerTxt->text().isEmpty()) { const auto png = Stamps::text(footerTxt->text(), QColor("#333333"), 32); const double w = std::min(0.6, 0.011 * footerTxt->text().size() + 0.03); ops.append(stampOp({}, png, 0.5 - w / 2, 0.975 - w * png.aspect, w, 1.0, 0, "topleft")); }
    }
    d->deleteLater(); return ok;
}
bool crop(QWidget* parent, const QList<int>& selected, QJsonArray& ops) {
    auto* d = make(parent, TXT("crop")); body(d)->addWidget(hint(TXT("cropHint")));
    QSpinBox* s[4]; const char* keys[] = {"side_left", "side_top", "side_right", "side_bottom"}; auto* g = new QGridLayout;
    for (int i = 0; i < 4; i++) { s[i] = new QSpinBox; s[i]->setRange(0, 45); s[i]->setValue(5); s[i]->setSuffix(" %"); g->addWidget(new QLabel(TXT(keys[i])), i / 2, (i % 2) * 2); g->addWidget(s[i], i / 2, (i % 2) * 2 + 1); }
    body(d)->addLayout(g);
    auto* scope = new QComboBox; scope->addItems({TXT("allPages"), QString("%1 (%2)").arg(TXT("selectedPages")).arg(selected.size())}); body(d)->addWidget(scope);
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) ops.append(QJsonObject{{"op", "crop"}, {"pages", scope->currentIndex() == 0 ? QJsonArray() : toArr(selected)}, {"left", s[0]->value() / 100.0}, {"top", s[1]->value() / 100.0}, {"right", s[2]->value() / 100.0}, {"bottom", s[3]->value() / 100.0}});
    d->deleteLater(); return ok;
}
bool exportImages(QWidget* parent, const Document& doc, const QList<int>& selected, QString& dir, QString& fmt, int& dpi, QList<int>& pages) {
    auto* d = make(parent, TXT("exportImages"));
    auto* f = new QComboBox; f->addItems({"PNG", "JPEG"}); auto* dp = new QComboBox; for (int v : {72, 110, 150, 200, 300}) dp->addItem(QString::number(v), v); dp->setCurrentIndex(2);
    auto* scope = new QComboBox; scope->addItems({TXT("allPages"), QString("%1 (%2)").arg(TXT("selectedPages")).arg(selected.size())});
    auto* form = new QFormLayout; form->addRow(TXT("format"), f); form->addRow("DPI", dp); form->addRow(TXT("pages"), scope); body(d)->addLayout(form);
    auto* btn = primary(TXT("chooseFolder")); btn->setIcon(FA::icon(FA::FolderOpen, Qt::white, 16)); footer(d, btn);
    if (d->exec() != QDialog::Accepted) { d->deleteLater(); return false; }
    d->deleteLater();
    dir = QFileDialog::getExistingDirectory(parent, TXT("chooseFolder")); if (dir.isEmpty()) return false;
    fmt = f->currentIndex() == 0 ? "png" : "jpg"; dpi = dp->currentData().toInt();
    pages.clear(); if (scope->currentIndex() == 0) for (int p = 1; p <= doc.pages(); p++) pages << p; else pages = selected;
    return true;
}
bool imagesToPdf(QWidget* parent, QStringList& files, bool& fitA4) {
    auto* d = make(parent, TXT("images2pdf"), 520);
    auto* list = new QListWidget; list->setDragDropMode(QAbstractItemView::InternalMove); list->setMinimumHeight(160); body(d)->addWidget(list);
    auto* row = new QHBoxLayout; auto* add = new QPushButton(TXT("addImages")); add->setIcon(FA::icon(FA::Plus, QColor("#4f8cff"), 14)); auto* a4 = new QCheckBox(TXT("fitA4")); a4->setChecked(true); row->addWidget(add); row->addStretch(); row->addWidget(a4); body(d)->addLayout(row);
    QObject::connect(add, &QPushButton::clicked, d, [=]() { for (const QString& f : QFileDialog::getOpenFileNames(d, TXT("addImages"), QString(), "Images (*.png *.jpg *.jpeg *.webp)")) { auto* it = new QListWidgetItem(QFileInfo(f).fileName()); it->setData(Qt::UserRole, f); list->addItem(it); } });
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted;
    files.clear(); for (int i = 0; i < list->count(); i++) files << list->item(i)->data(Qt::UserRole).toString(); fitA4 = a4->isChecked();
    d->deleteLater(); return ok && !files.isEmpty();
}
bool compress(QWidget* parent, const Document& doc, QJsonArray& ops) {
    auto* d = make(parent, TXT("compress"));
    const qint64 size = QFileInfo(doc.workPath).size();
    body(d)->addWidget(hint(QString("%1: %2").arg(TXT("currentSize"), QLocale().formattedDataSize(size))));
    auto* a = new QRadioButton(TXT("compressLossless")); a->setChecked(true); auto* b = new QRadioButton(TXT("compressRaster")); body(d)->addWidget(a); body(d)->addWidget(b);
    auto* dp = new QComboBox; for (int v : {72, 96, 110, 150}) dp->addItem(QString::number(v) + " dpi", v); dp->setCurrentIndex(2); body(d)->addWidget(dp);
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok && b->isChecked()) {
        const int dpi = dp->currentData().toInt();
        for (int p = 1; p <= doc.pages(); p++) {
            const QSizeF s = doc.pdf->sizes[p - 1];
            QImage img = doc.pdf->render(p - 1, int(s.width() * dpi / 72.0));
            QByteArray bytes; QBuffer buf(&bytes); buf.open(QIODevice::WriteOnly); img.convertToFormat(QImage::Format_RGB32).save(&buf, "JPEG", 80);
            ops.append(QJsonObject{{"op", "redact"}, {"page", p}, {"jpeg_base64", QString::fromLatin1(bytes.toBase64())}, {"width_pt", s.width()}, {"height_pt", s.height()}});
        }
    }
    d->deleteLater(); return ok;
}
bool sanitize(QWidget* parent, QJsonArray& ops) {
    auto* d = make(parent, TXT("sanitize")); body(d)->addWidget(hint(TXT("sanitizeHint"))); auto* keep = new QCheckBox(TXT("keepAnnotations")); body(d)->addWidget(keep);
    footer(d, primary(TXT("apply")));
    const bool ok = d->exec() == QDialog::Accepted; if (ok) ops.append(QJsonObject{{"op", "sanitize"}, {"keep_annotations", keep->isChecked()}}); d->deleteLater(); return ok;
}
bool properties(QWidget* parent, const Document& doc, QJsonArray& ops) {
    auto* d = make(parent, TXT("properties"));
    const QJsonObject m = doc.info.value("metadata").toObject();
    auto* title = new QLineEdit(m.value("Title").toString()); auto* author = new QLineEdit(m.value("Author").toString()); auto* subject = new QLineEdit(m.value("Subject").toString()); auto* kw = new QLineEdit(m.value("Keywords").toString());
    auto* form = new QFormLayout; form->addRow(TXT("title"), title); form->addRow(TXT("author"), author); form->addRow(TXT("subject"), subject); form->addRow(TXT("keywords"), kw); body(d)->addLayout(form);
    footer(d, primary(TXT("ok")));
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) ops.append(QJsonObject{{"op", "metadata"}, {"title", title->text()}, {"subject", subject->text()}, {"keywords", kw->text()}, {"extra", QJsonObject{{"Author", author->text()}}}});
    d->deleteLater(); return ok;
}

class RedactCanvas : public QLabel {
public:
    QList<QRectF> boxes; QPointF start; bool dragging = false; QRectF live;
    explicit RedactCanvas(const QPixmap& pm) { setPixmap(pm); setCursor(Qt::CrossCursor); setFixedSize(pm.size() / pm.devicePixelRatio()); }
    QPointF norm(const QPoint& p) const { return QPointF(p.x() / double(width()), p.y() / double(height())); }
protected:
    void mousePressEvent(QMouseEvent* e) override { dragging = true; start = norm(e->pos()); live = QRectF(start, start); update(); }
    void mouseMoveEvent(QMouseEvent* e) override { if (dragging) { live = QRectF(start, norm(e->pos())).normalized(); update(); } }
    void mouseReleaseEvent(QMouseEvent*) override { dragging = false; if (live.width() > 0.005 && live.height() > 0.005) boxes << live; live = QRectF(); update(); }
    void paintEvent(QPaintEvent* e) override {
        QLabel::paintEvent(e); QPainter p(this); p.setBrush(QColor(0, 0, 0, 220)); p.setPen(QPen(QColor("#ef5b5b"), 1));
        for (const QRectF& b : boxes) p.drawRect(QRectF(b.x() * width(), b.y() * height(), b.width() * width(), b.height() * height()));
        if (dragging) p.drawRect(QRectF(live.x() * width(), live.y() * height(), live.width() * width(), live.height() * height()));
    }
};
bool redact(QWidget* parent, const Document& doc, int page, QJsonArray& ops) {
    auto* d = make(parent, QString("%1 — %2 %3").arg(TXT("redact"), TXT("page")).arg(page), 760);
    body(d)->addWidget(hint(TXT("redactHint")));
    QImage preview = doc.pdf->render(page - 1, 680, 2.0); QPixmap pm = QPixmap::fromImage(preview); pm.setDevicePixelRatio(2.0);
    auto* canvas = new RedactCanvas(pm); auto* wrap = new QHBoxLayout; wrap->addStretch(); wrap->addWidget(canvas); wrap->addStretch(); body(d)->addLayout(wrap);
    auto* row = new QHBoxLayout; auto* dp = new QComboBox; for (int v : {110, 150, 200, 300}) dp->addItem(QString::number(v) + " dpi", v); dp->setCurrentIndex(2);
    auto* clearBtn = new QPushButton(TXT("clearBoxes")); auto* apply = new QPushButton(TXT("applyRedact")); apply->setObjectName("danger"); apply->setIcon(FA::icon(FA::Marker, QColor("#c62828"), 14));
    row->addWidget(dp); row->addStretch(); row->addWidget(clearBtn); row->addWidget(apply); body(d)->addLayout(row);
    QObject::connect(clearBtn, &QPushButton::clicked, d, [=]() { canvas->boxes.clear(); canvas->update(); });
    QObject::connect(apply, &QPushButton::clicked, d, &QDialog::accept);
    const bool ok = d->exec() == QDialog::Accepted && !canvas->boxes.isEmpty();
    if (ok) {
        const QSizeF s = doc.pdf->sizes[page - 1]; const int dpi = dp->currentData().toInt();
        QImage img = doc.pdf->render(page - 1, int(s.width() * dpi / 72.0)).convertToFormat(QImage::Format_RGB32);
        QPainter p(&img); p.setBrush(Qt::black); p.setPen(Qt::NoPen); for (const QRectF& b : canvas->boxes) p.drawRect(QRectF(b.x() * img.width(), b.y() * img.height(), b.width() * img.width(), b.height() * img.height())); p.end();
        QByteArray bytes; QBuffer buf(&bytes); buf.open(QIODevice::WriteOnly); img.save(&buf, "JPEG", 92);
        ops.append(QJsonObject{{"op", "redact"}, {"page", page}, {"jpeg_base64", QString::fromLatin1(bytes.toBase64())}, {"width_pt", s.width()}, {"height_pt", s.height()}});
    }
    d->deleteLater(); return ok;
}
bool addLabel(QWidget* parent, QJsonObject& label) {
    auto* d = make(parent, TXT("addManual"), 460);
    auto* name = new QLineEdit; name->setPlaceholderText("Confidential"); auto* guid = new QLineEdit(Core::newGuid()); auto* gen = new QPushButton(TXT("generate")); auto* color = new QLineEdit("#f4b63f");
    auto* gr = new QHBoxLayout; gr->addWidget(guid, 1); gr->addWidget(gen);
    auto* form = new QFormLayout; form->addRow(TXT("labelName"), name); form->addRow(TXT("labelGuid"), gr); form->addRow(TXT("labelColor"), color); body(d)->addLayout(form);
    QObject::connect(gen, &QPushButton::clicked, d, [=]() { guid->setText(Core::newGuid()); });
    footer(d, primary(TXT("ok")));
    const bool ok = d->exec() == QDialog::Accepted && !name->text().trimmed().isEmpty();
    if (ok) label = QJsonObject{{"id", guid->text().trimmed().toLower()}, {"name", name->text().trimmed()}, {"tooltip", ""}, {"color", color->text()}, {"parent", ""}, {"order", 0}, {"source", "manual"}};
    d->deleteLater(); return ok;
}
}
