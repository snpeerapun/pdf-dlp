#include "panels.h"
#include "icons.h"
#include "i18n.h"
#include "stamps.h"
#include "core.h"
#include "dialogs.h"

static QWidget* scrollBody(QScrollArea* sa) { auto* w = new QWidget; auto* v = new QVBoxLayout(w); v->setContentsMargins(10, 10, 10, 10); v->setSpacing(10); v->setAlignment(Qt::AlignTop); sa->setWidget(w); sa->setWidgetResizable(true); sa->setFrameShape(QFrame::NoFrame); sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); w->setStyleSheet("background:#f5f7fb;"); w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); return w; }

const QList<ToolDef>& toolDefs() {
    static const QList<ToolDef> d = {
        {"merge", "merge", FA::ObjectGroup, "#7b5cff", 0}, {"split", "split", FA::Scissors, "#7b5cff", 0}, {"insert", "insert", FA::FileCirclePlus, "#7b5cff", 0},
        {"rotateL", "rotateL", FA::RotateLeft, "#4f8cff", 1}, {"rotate", "rotateR", FA::RotateRight, "#4f8cff", 1}, {"delete", "deletePages", FA::Trash, "#e5484d", 1}, {"extract", "extract", FA::FileExport, "#4f8cff", 1}, {"crop", "crop", FA::Crop, "#4f8cff", 1},
        {"encrypt", "encrypt", FA::Lock, "#34c77b", 2}, {"decrypt", "decrypt", FA::LockOpen, "#34c77b", 2}, {"redact", "redact", FA::Marker, "#e5484d", 2}, {"sanitize", "sanitize", FA::Broom, "#34c77b", 2},
        {"watermark", "watermark", FA::Droplet, "#f4a33f", 3}, {"pagenum", "pageNumbers", FA::ListOl, "#f4a33f", 3}, {"props", "properties", FA::PenToSquare, "#f4a33f", 3},
        {"images", "exportImages", FA::Images, "#14b8a6", 4}, {"img2pdf", "images2pdf", FA::FileImage, "#14b8a6", 4}, {"compress", "compress", FA::Compress, "#14b8a6", 4}, {"print", "print", FA::Print, "#6f7a89", 4},
    };
    return d;
}

/* ---------------- classification ---------------- */
ClassifyPanel::ClassifyPanel(QWidget* parent) : QScrollArea(parent) {
    auto* body = scrollBody(this); auto* v = static_cast<QVBoxLayout*>(body->layout());
    gCur = new QGroupBox; gCur->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); auto* cv = new QHBoxLayout(gCur); current = new QLabel; current->setObjectName("badge"); current->setWordWrap(true); current->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); current->setMaximumWidth(220); msipBadge = new QLabel("MSIP"); msipBadge->setObjectName("badgeInfo"); marksBadge = new QLabel; marksBadge->setObjectName("badge");
    removeBtn = new QPushButton; removeBtn->setIcon(FA::icon(FA::Eraser, QColor("#6f7a89"), 14)); removeBtn->setFlat(true);
    auto* cvv = new QVBoxLayout; auto* cr = new QHBoxLayout; cr->addWidget(current); cr->addWidget(msipBadge); cr->addWidget(marksBadge); cr->addStretch(); cvv->addLayout(cr); cvv->addWidget(removeBtn, 0, Qt::AlignLeft); cv->addLayout(cvv); v->addWidget(gCur);
    gChoose = new QGroupBox; gChoose->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); auto* lv = new QVBoxLayout(gChoose);
    list = new QListWidget; list->setMinimumHeight(120); list->setMaximumHeight(200); list->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); list->setIconSize(QSize(14, 14)); list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); lv->addWidget(list);
    readWinBtn = new QPushButton; readWinBtn->setIcon(FA::icon(FA::Windows, QColor("#4f8cff"), 14, true)); learnBtn = new QPushButton; learnBtn->setIcon(FA::icon(FA::Graduation, QColor("#7b5cff"), 14)); addBtn = new QPushButton; addBtn->setIcon(FA::icon(FA::Plus, QColor("#34c77b"), 14)); addBtn->setFlat(true); for (auto* b : {readWinBtn, learnBtn, addBtn}) b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    lv->addWidget(readWinBtn); lv->addWidget(learnBtn); lv->addWidget(addBtn, 0, Qt::AlignLeft);
    notes = new QLabel; notes->setObjectName("hint"); notes->setWordWrap(true); notes->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); lv->addWidget(notes); v->addWidget(gChoose);
    gMark = new QGroupBox; gMark->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); auto* mv = new QVBoxLayout(gMark);
    cbHeader = new QCheckBox; cbFooter = new QCheckBox; cbWatermark = new QCheckBox; cbMsip = new QCheckBox; cbEncrypt = new QCheckBox; for (auto* c : {cbHeader, cbFooter, cbWatermark, cbMsip, cbEncrypt}) c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); siteId = new QLineEdit; siteId->setPlaceholderText("Tenant ID"); siteId->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); encPw = new QLineEdit; encPw->setEchoMode(QLineEdit::Password); encPw->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (auto* c : {cbHeader, cbFooter, cbWatermark, cbMsip, cbEncrypt}) mv->addWidget(c);
    mv->addWidget(siteId); mv->addWidget(encPw);
    applyBtn = new QPushButton; applyBtn->setObjectName("primary"); applyBtn->setIcon(FA::icon(FA::Tag, Qt::white, 14)); applyBtn->setMinimumHeight(34); applyBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); batchBtn = new QPushButton; batchBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); batchBtn->setIcon(FA::icon(FA::Layers, QColor("#4f8cff"), 14)); mv->addWidget(applyBtn); mv->addWidget(batchBtn);
    v->addWidget(gMark);
    load(); refreshList(); retranslate();
    QSettings s; cbHeader->setChecked(s.value("mark/header", true).toBool()); cbFooter->setChecked(s.value("mark/footer", true).toBool()); cbWatermark->setChecked(s.value("mark/watermark", false).toBool()); cbMsip->setChecked(s.value("mark/msip", true).toBool()); cbEncrypt->setChecked(s.value("mark/encrypt", false).toBool()); siteId->setText(s.value("mark/siteId").toString());
    siteId->setVisible(cbMsip->isChecked()); encPw->setVisible(cbEncrypt->isChecked());
    auto persist = [this]() { QSettings s; s.setValue("mark/header", cbHeader->isChecked()); s.setValue("mark/footer", cbFooter->isChecked()); s.setValue("mark/watermark", cbWatermark->isChecked()); s.setValue("mark/msip", cbMsip->isChecked()); s.setValue("mark/encrypt", cbEncrypt->isChecked()); s.setValue("mark/siteId", siteId->text()); siteId->setVisible(cbMsip->isChecked()); encPw->setVisible(cbEncrypt->isChecked()); };
    for (auto* c : {cbHeader, cbFooter, cbWatermark, cbMsip, cbEncrypt}) connect(c, &QCheckBox::toggled, this, persist);
    connect(siteId, &QLineEdit::textChanged, this, persist);
    connect(applyBtn, &QPushButton::clicked, this, &ClassifyPanel::applyRequested);
    connect(removeBtn, &QPushButton::clicked, this, &ClassifyPanel::removeRequested);
    connect(batchBtn, &QPushButton::clicked, this, &ClassifyPanel::batchRequested);
    connect(list, &QListWidget::itemSelectionChanged, this, [this]() { applyBtn->setEnabled(list->currentItem() && gCur->isEnabled()); batchBtn->setEnabled(list->currentItem() != nullptr); });
    connect(readWinBtn, &QPushButton::clicked, this, [this]() {
        readWinBtn->setEnabled(false);
        Core::async([]() { return Core::discoverLabels(); }, [this](QJsonObject r) {
            readWinBtn->setEnabled(true);
            const QJsonArray found = r.value("labels").toArray(); mergeLabels(found);
            QStringList n; n << QString("%1: %2 · %3: %4").arg(TXT("files")).arg(r.value("scanned").toArray().size()).arg(TXT("found")).arg(found.size());
            for (const auto& x : r.value("notes").toArray()) n << x.toString();
            if (!r.value("ok").toBool()) n << r.value("error").toString();
            notes->setText(n.join("\n"));
        });
    });
    connect(learnBtn, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(this, TXT("learnFromFile"), QString(), "Documents (*.pdf *.docx *.xlsx *.pptx)");
        int n = 0; for (const QString& f : files) { const QJsonObject r = Core::labelsFromFile(f); const QJsonArray l = r.value("labels").toArray(); n += l.size(); mergeLabels(l); }
        notes->setText(QString("%1 %2 label").arg(TXT("found")).arg(n));
    });
    connect(addBtn, &QPushButton::clicked, this, [this]() { QJsonObject l; if (Dialogs::addLabel(this, l)) mergeLabels(QJsonArray{l}); });
    connect(list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        auto* it = list->itemAt(p); if (!it) return; QMenu m; auto* del = m.addAction(FA::icon(FA::Trash, QColor("#e5484d"), 14), TXT("deleteLabel"));
        if (m.exec(list->viewport()->mapToGlobal(p)) == del) { const QString id = it->data(Qt::UserRole).toString(); QJsonArray n; for (const auto& x : m_labels) if (x.toObject().value("id").toString() != id) n.append(x); m_labels = n; save(); refreshList(); }
    });
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    setDocument(nullptr);
}
void ClassifyPanel::retranslate() {
    gCur->setTitle(TXT("currentLabel")); gChoose->setTitle(TXT("chooseLabel")); gMark->setTitle(TXT("marking")); removeBtn->setText(TXT("removeLabel")); readWinBtn->setText(TXT("readFromWindows")); learnBtn->setText(TXT("learnFromFile")); addBtn->setText(TXT("addManual"));
    cbHeader->setText(TXT("markHeader")); cbFooter->setText(TXT("markFooter")); cbWatermark->setText(TXT("markWatermark")); cbMsip->setText(TXT("msipWrite")); cbEncrypt->setText(TXT("markEncrypt")); siteId->setToolTip(TXT("siteId")); encPw->setPlaceholderText(TXT("password"));
    applyBtn->setText(TXT("applyLabel")); batchBtn->setText(TXT("batch")); batchBtn->setToolTip(TXT("batchHint")); marksBadge->setText(TXT("marks"));
    if (m_labels.isEmpty()) notes->setText(TXT("noLabels"));
}
void ClassifyPanel::load() { QSettings s; m_labels = QJsonDocument::fromJson(s.value("labels").toByteArray()).array(); }
void ClassifyPanel::save() { QSettings s; s.setValue("labels", QJsonDocument(m_labels).toJson(QJsonDocument::Compact)); }
void ClassifyPanel::mergeLabels(const QJsonArray& add) {
    static const char* colors[] = {"#2ea043", "#4f8cff", "#f4b63f", "#ef5b5b", "#9b5cff"};
    for (const auto& av : add) {
        QJsonObject a = av.toObject(); bool found = false;
        for (int i = 0; i < m_labels.size(); i++) { QJsonObject c = m_labels[i].toObject(); if (c.value("id") == a.value("id")) { if (!a.value("name").toString().isEmpty()) c["name"] = a.value("name"); if (c.value("color").toString().isEmpty()) c["color"] = a.value("color"); m_labels[i] = c; found = true; break; } }
        if (!found) { if (a.value("color").toString().isEmpty()) a["color"] = colors[m_labels.size() % 5]; a["order"] = m_labels.size(); m_labels.append(a); }
    }
    save(); refreshList();
}
void ClassifyPanel::refreshList() {
    list->clear();
    for (const auto& v : m_labels) { const QJsonObject l = v.toObject(); auto* it = new QListWidgetItem(FA::icon(FA::Circle, QColor(l.value("color").toString("#888")), 14), l.value("name").toString() + (l.value("id").toString().size() == 36 ? "   · MSIP" : "")); it->setData(Qt::UserRole, l.value("id").toString()); it->setToolTip(l.value("source").toString()); list->addItem(it); }
    if (m_labels.isEmpty()) notes->setText(TXT("noLabels")); else if (notes->text() == TXT("noLabels")) notes->clear();
    applyBtn->setEnabled(false); batchBtn->setEnabled(false);
}
QJsonObject ClassifyPanel::selectedLabel() const { auto* it = list->currentItem(); if (!it) return {}; const QString id = it->data(Qt::UserRole).toString(); for (const auto& v : m_labels) if (v.toObject().value("id").toString() == id) return v.toObject(); return {}; }
void ClassifyPanel::setDocument(const Document* d) {
    gCur->setEnabled(d != nullptr); gMark->setEnabled(true);
    const QString c = d ? d->classification() : QString();
    current->setText(c.isEmpty() ? TXT("noLabel") : c); current->setObjectName(c.isEmpty() ? "badge" : "badgeWarn"); current->style()->unpolish(current); current->style()->polish(current);
    msipBadge->setVisible(d && !d->info.value("msip").isNull() && d->info.value("msip").isObject());
    marksBadge->setVisible(d && d->info.value("has_marks").toBool());
    removeBtn->setVisible(d && (!c.isEmpty() || d->info.value("has_marks").toBool()));
    applyBtn->setEnabled(d && list->currentItem());
}
QJsonArray ClassifyPanel::buildOps(const QJsonObject& label, const QJsonObject& previousMeta, QString* nextPassword) const {
    QJsonArray ops; ops.append(QJsonObject{{"op", "remove_marks"}});
    const QString name = label.value("name").toString(); const QColor color(label.value("color").toString("#f4b63f"));
    const double w = std::min(0.42, 0.012 * name.size() + 0.06);
    if (cbHeader->isChecked() || cbFooter->isChecked()) {
        const Stamps::Png b = Stamps::badge(name.toUpper(), color);
        if (cbHeader->isChecked()) ops.append(QJsonObject{{"op", "stamp"}, {"pages", QJsonArray()}, {"png_base64", b.base64}, {"x", 0.5 - w / 2}, {"y", 0.012}, {"w", w}, {"opacity", 1.0}, {"rotate_deg", 0}, {"anchor", "topleft"}});
        if (cbFooter->isChecked()) ops.append(QJsonObject{{"op", "stamp"}, {"pages", QJsonArray()}, {"png_base64", b.base64}, {"x", 0.5 - w / 2}, {"y", 0.988 - w * b.aspect}, {"w", w}, {"opacity", 1.0}, {"rotate_deg", 0}, {"anchor", "topleft"}});
    }
    if (cbWatermark->isChecked()) { const Stamps::Png wm = Stamps::watermark(name.toUpper(), color); ops.append(QJsonObject{{"op", "stamp"}, {"pages", QJsonArray()}, {"png_base64", wm.base64}, {"x", 0.5}, {"y", 0.5}, {"w", 0.75}, {"opacity", 0.18}, {"rotate_deg", 45}, {"anchor", "center"}}); }
    QJsonObject extra;
    if (cbMsip->isChecked()) { const unsigned bits = (cbHeader->isChecked() ? 1 : 0) | (cbFooter->isChecked() ? 2 : 0) | (cbWatermark->isChecked() ? 4 : 0) | (cbEncrypt->isChecked() ? 8 : 0); extra = Core::msipMetadata(label, siteId->text().trimmed(), bits, QString(), previousMeta).value("metadata").toObject(); }
    extra["ClassificationLevel"] = QString::number(label.value("order").toInt() + 1); extra["ClassifiedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    ops.append(QJsonObject{{"op", "metadata"}, {"classification", name}, {"keywords", "Classification: " + name}, {"extra", extra}});
    if (cbEncrypt->isChecked() && !encPw->text().isEmpty()) { ops.append(QJsonObject{{"op", "encrypt"}, {"user_password", encPw->text()}, {"owner_password", encPw->text()}, {"allow_print", true}, {"allow_copy", false}, {"allow_modify", false}, {"allow_annotate", false}}); if (nextPassword) *nextPassword = encPw->text(); }
    return ops;
}
QJsonArray ClassifyPanel::buildRemoveOps(const QJsonObject& previousMeta) const {
    QJsonObject extra; for (const QString& k : previousMeta.keys()) if (k.startsWith("MSIP_Label_") || k == "ClassificationLevel" || k == "ClassifiedAt") extra[k] = "";
    return QJsonArray{QJsonObject{{"op", "remove_marks"}}, QJsonObject{{"op", "metadata"}, {"classification", ""}, {"keywords", ""}, {"extra", extra}}};
}

/* ---------------- info ---------------- */
InfoPanel::InfoPanel(QWidget* parent) : QScrollArea(parent) { auto* body = scrollBody(this); content = new QTextBrowser; content->setFrameShape(QFrame::NoFrame); content->setOpenExternalLinks(false); content->setLineWrapMode(QTextEdit::WidgetWidth); content->setWordWrapMode(QTextOption::WrapAnywhere); content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); content->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); content->setStyleSheet("background:transparent;border:0;"); content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); static_cast<QVBoxLayout*>(body->layout())->addWidget(content, 1); }
static QString esc(const QString& s) { return s.toHtmlEscaped(); }
static QString fmtDate(const QString& v) { static const QRegularExpression re("D:(\\d{4})(\\d{2})(\\d{2})(\\d{2})?(\\d{2})?"); auto m = re.match(v); return m.hasMatch() ? QString("%1-%2-%3 %4:%5").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4).isEmpty() ? "00" : m.captured(4), m.captured(5).isEmpty() ? "00" : m.captured(5)) : v; }
void InfoPanel::setDocument(const Document* d) {
    if (!d) { content->setHtml(QString("<span style='color:#6f7a89'>%1</span>").arg(TXT("dropHint"))); return; }
    const QJsonObject m = d->info.value("metadata").toObject(); const QJsonObject msip = d->info.value("msip").toObject();
    auto row = [](const QString& k, const QString& v) { return QString("<tr><td style='color:#6f7a89;padding:2px 10px 2px 0;white-space:nowrap'>%1</td><td style='padding:2px 0'>%2</td></tr>").arg(esc(k), v); };
    QString h = "<div style='background:#fff;border:1px solid #e3e8ef;border-radius:10px;padding:10px'>";
    const QString cls = d->classification();
    h += QString("<b>%1</b><br/>").arg(TXT("classification")) + (cls.isEmpty() ? QString("<span style='color:#6f7a89'>%1</span>").arg(TXT("noLabel")) : QString("<span style='background:#fff3d6;color:#9a6400;padding:2px 8px;border-radius:8px;font-weight:600'>%1</span>").arg(esc(cls)));
    if (!msip.isEmpty()) h += "<table style='margin-top:6px'>" + row(TXT("msipLabel"), esc(msip.value("name").toString())) + row("GUID", "<code>" + esc(msip.value("id").toString()) + "</code>") + row("Set date", esc(msip.value("set_date").toString())) + (msip.value("site_id").toString().isEmpty() ? "" : row("Tenant", "<code>" + esc(msip.value("site_id").toString()) + "</code>")) + "</table>";
    h += "</div><br/><div style='background:#fff;border:1px solid #e3e8ef;border-radius:10px;padding:10px'><b>" + TXT("properties") + "</b><table style='margin-top:6px'>";
    h += row(TXT("title"), esc(m.value("Title").toString())) + row(TXT("author"), esc(m.value("Author").toString())) + row(TXT("subject"), esc(m.value("Subject").toString())) + row(TXT("keywords"), esc(m.value("Keywords").toString())) + row(TXT("producer"), esc(m.value("Producer").toString().isEmpty() ? m.value("Creator").toString() : m.value("Producer").toString()));
    h += row(TXT("created"), esc(fmtDate(m.value("CreationDate").toString()))) + row(TXT("modifiedAt"), esc(fmtDate(m.value("ModDate").toString()))) + row(TXT("version"), esc(d->info.value("version").toString())) + row(TXT("pages"), QString::number(d->pages()));
    h += row(TXT("encrypted"), d->info.value("encrypted").toBool() ? QString("<span style='color:#c62828;font-weight:600'>%1</span>").arg(TXT("yes")) : QString("<span style='color:#1b7f44;font-weight:600'>%1</span>").arg(TXT("no")));
    h += row(TXT("path"), "<span style='font-size:11px'>" + esc(d->path) + "</span>") + "</table></div>";
    QStringList extra; for (const QString& k : m.keys()) if (k.startsWith("MSIP_") || k == "Classification" || k == "ClassificationLevel") extra << row(esc(QString(k).replace(QRegularExpression("^MSIP_Label_[0-9a-f-]+_"), "MSIP…_")), "<code>" + esc(m.value(k).toString()) + "</code>");
    if (!extra.isEmpty()) h += "<br/><div style='background:#fff;border:1px solid #e3e8ef;border-radius:10px;padding:10px'><b>Metadata</b><table style='margin-top:6px;font-size:11px'>" + extra.join("") + "</table></div>";
    content->setHtml(h);
}
