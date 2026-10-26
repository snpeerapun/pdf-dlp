#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include "mainwindow.h"
#include "core.h"
#include "dialogs.h"
#include "icons.h"
#include "i18n.h"
#include "stamps.h"
#include "present.h"
#include "dialogs.h"

static const QColor kBlue("#4f8cff");

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("PDF DLP"); resize(1380, 880); setAcceptDrops(true); setDockOptions(QMainWindow::AnimatedDocks);
    // --- toolbar ---
    { auto* top = addToolBar("rgbtop"); top->setObjectName("rgbtop"); top->setMovable(false); top->setFixedHeight(3); auto* f = new QFrame; f->setObjectName("rgbbar"); f->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); top->addWidget(f); addToolBarBreak(); }
    tb = addToolBar("main"); tb->setMovable(false); tb->setIconSize(QSize(18, 18)); tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* brand = new QWidget; auto* bl = new QHBoxLayout(brand); bl->setContentsMargins(4, 0, 10, 0); auto* logo = FA::label(FA::ShieldHalved, QColor("#7b5cff"), 18); auto* bt = new QLabel("<b>PDF DLP</b>"); bl->addWidget(logo); bl->addWidget(bt); tb->addWidget(brand);
    homeAct = tb->addAction(FA::icon(FA::House, QColor("#7b5cff")), ""); openAct = tb->addAction(FA::icon(FA::FolderOpen, kBlue), ""); saveAct = tb->addAction(FA::icon(FA::Save, Qt::white), ""); undoAct = tb->addAction(FA::icon(FA::Undo, QColor("#6f7a89")), "");
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(saveAct))) b->setObjectName("primary");
    modifiedBadge = new QLabel; modifiedBadge->setObjectName("badgeWarn"); modifiedBadge->hide(); tb->addWidget(modifiedBadge);
    busyLbl = new QLabel; busyLbl->setObjectName("badgeInfo"); busyLbl->hide(); tb->addWidget(busyLbl);
    auto* sp = new QWidget; sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tb->addWidget(sp);
    langBox = new QComboBox; langBox->addItems({"ไทย", "EN"}); langBox->setCurrentIndex(I18n::lang() == "en" ? 1 : 0); tb->addWidget(langBox);
    settingsAct = tb->addAction(FA::icon(FA::Gear, QColor("#6f7a89")), "");
    // --- center ---
    center = new QStackedWidget; viewer = new Viewer; auto* wrap = new QWidget; auto* wv = new QVBoxLayout(wrap); wv->setContentsMargins(0, 0, 0, 0); wv->setSpacing(0);
    auto* vbar = new QToolBar; vbar->setObjectName("viewbar"); vbar->setMovable(false); vbar->setIconSize(QSize(16, 16)); vbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto* leftTog = vbar->addAction(FA::icon(FA::AnglesLeft, QColor("#a3adbb"), 13), ""); leftTog->setCheckable(true); leftTog->setChecked(true); leftTog->setToolTip(TXT("sidebarLeft") + " (⌘1)");
    connect(leftTog, &QAction::toggled, this, [this, leftTog](bool on) { if (leftDock) leftDock->setVisible(on); leftTog->setIcon(FA::icon(on ? FA::AnglesLeft : FA::AnglesRight, QColor("#a3adbb"), 13)); });
    auto* vsp = new QWidget; vsp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); vbar->addWidget(vsp);
    prevAct = vbar->addAction(FA::icon(FA::ChevronUp, QColor("#6f7a89"), 16), ""); pageEdit = new QLineEdit; pageEdit->setFixedWidth(46); pageEdit->setAlignment(Qt::AlignCenter); vbar->addWidget(pageEdit); pageTotal = new QLabel(" / 0 "); vbar->addWidget(pageTotal); nextAct = vbar->addAction(FA::icon(FA::ChevronDown, QColor("#6f7a89"), 16), "");
    vbar->addSeparator();
    zoomOutAct = vbar->addAction(FA::icon(FA::Minus, QColor("#6f7a89"), 16), ""); zoomLbl = new QLabel(" 100% "); zoomLbl->setFixedWidth(52); zoomLbl->setAlignment(Qt::AlignCenter); vbar->addWidget(zoomLbl); zoomInAct = vbar->addAction(FA::icon(FA::Plus, QColor("#6f7a89"), 16), "");
    fitWAct = vbar->addAction(FA::icon(FA::ArrowsLR, QColor("#6f7a89"), 16), ""); fitPAct = vbar->addAction(FA::icon(FA::Expand, QColor("#6f7a89"), 16), "");
    auto* vsp2 = new QWidget; vsp2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); vbar->addWidget(vsp2);
    auto* rightTog = vbar->addAction(FA::icon(FA::AnglesRight, QColor("#a3adbb"), 13), ""); rightTog->setCheckable(true); rightTog->setChecked(true); rightTog->setToolTip(TXT("sidebarRight") + " (⌘2)");
    connect(rightTog, &QAction::toggled, this, [this, rightTog](bool on) { if (rightDock) rightDock->setVisible(on); rightTog->setIcon(FA::icon(on ? FA::AnglesRight : FA::AnglesLeft, QColor("#a3adbb"), 13)); });
    auto* l1 = new QShortcut(QKeySequence("Ctrl+1"), this); connect(l1, &QShortcut::activated, leftTog, &QAction::toggle);
    auto* r2 = new QShortcut(QKeySequence("Ctrl+2"), this); connect(r2, &QShortcut::activated, rightTog, &QAction::toggle);
    auto* f5 = new QShortcut(QKeySequence(Qt::Key_F5), this); connect(f5, &QShortcut::activated, this, &MainWindow::present);
    wv->addWidget(vbar);
    buildEditBar(); wv->addWidget(editbar); editbar->hide();
    home = new HomeView;
    center->addWidget(home); center->addWidget(viewer); wv->addWidget(center, 1); setCentralWidget(wrap);
    // --- docks ---
    sidebar = new Sidebar; leftDock = new QDockWidget; leftDock->setWidget(sidebar); leftDock->setFeatures(QDockWidget::NoDockWidgetFeatures); leftDock->setFixedWidth(230); leftDock->setTitleBarWidget(new QWidget); addDockWidget(Qt::LeftDockWidgetArea, leftDock);
    buildToolbox();
    rightTabs = new QTabWidget; classify = new ClassifyPanel; infoPanel = new InfoPanel; rightTabs->addTab(classify, ""); rightTabs->addTab(infoPanel, ""); rightTabs->setDocumentMode(true); rightTabs->tabBar()->setExpanding(true); rightTabs->tabBar()->setDrawBase(false);
    rightDock = new QDockWidget; rightDock->setWidget(rightTabs); rightDock->setFeatures(QDockWidget::NoDockWidgetFeatures); rightDock->setFixedWidth(330); rightDock->setTitleBarWidget(new QWidget); addDockWidget(Qt::RightDockWidgetArea, rightDock);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea); setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    statusBar()->showMessage("");
    progress = new QProgressBar; progress->setFixedWidth(160); progress->setTextVisible(false); progress->hide(); statusBar()->addPermanentWidget(progress);
    // --- wiring ---
    connect(openAct, &QAction::triggered, this, [this]() { openPaths(QFileDialog::getOpenFileNames(this, TXT("open"), QString(), "PDF (*.pdf)")); });
    connect(homeAct, &QAction::triggered, this, &MainWindow::showHome);
    // menu bar (native on macOS)
    auto* fileMenu = menuBar()->addMenu(TXT("files")); fileMenu->setObjectName("fileMenu");
    fileMenu->addAction(homeAct); fileMenu->addAction(openAct); fileMenu->addAction(saveAct); fileMenu->addSeparator(); fileMenu->addAction(undoAct); fileMenu->addSeparator(); fileMenu->addAction(settingsAct);
    homeAct->setShortcut(QKeySequence("Ctrl+Shift+H")); openAct->setShortcut(QKeySequence::Open); saveAct->setShortcut(QKeySequence::SaveAs); undoAct->setShortcut(QKeySequence::Undo);
    connect(home, &HomeView::openRequested, openAct, &QAction::trigger); connect(home, &HomeView::fileRequested, this, [this](const QString& p) { openPaths({p}); });
    connect(saveAct, &QAction::triggered, this, &MainWindow::saveAs); connect(undoAct, &QAction::triggered, this, &MainWindow::undo); connect(settingsAct, &QAction::triggered, this, &MainWindow::showSettings);
    connect(zoomInAct, &QAction::triggered, this, [this]() { viewer->zoomStep(1); }); connect(zoomOutAct, &QAction::triggered, this, [this]() { viewer->zoomStep(-1); }); connect(fitWAct, &QAction::triggered, this, [this]() { viewer->fitWidth(); }); connect(fitPAct, &QAction::triggered, this, [this]() { viewer->fitPage(); });
    connect(prevAct, &QAction::triggered, this, [this]() { viewer->gotoPage(viewer->currentPage()); }); connect(nextAct, &QAction::triggered, this, [this]() { viewer->gotoPage(viewer->currentPage() + 2); });
    connect(pageEdit, &QLineEdit::returnPressed, this, [this]() { viewer->gotoPage(pageEdit->text().toInt()); });
    connect(viewer, &Viewer::zoomChanged, this, [this](double s) { zoomLbl->setText(QString("%1%").arg(int(s * 100 + 0.5))); });
    connect(viewer, &Viewer::pageChanged, this, [this](int p, int n) { pageEdit->setText(QString::number(p)); pageTotal->setText(QString(" / %1 ").arg(n)); sidebar->setCurrentPage(p); });
    connect(sidebar, &Sidebar::fileActivated, this, [this](int i) { homeMode = false; setActive(i); });
    connect(sidebar, &Sidebar::pageActivated, this, [this](int) { if (homeMode && active()) { homeMode = false; refreshAll(); } }); connect(sidebar, &Sidebar::fileClosed, this, &MainWindow::closeDoc);
    connect(sidebar, &Sidebar::fileChecked, this, [this](int i, bool on) { if (i >= 0 && i < int(docs.size())) docs[i].checked = on; });
    connect(sidebar, &Sidebar::pageActivated, this, [this](int p) { viewer->gotoPage(p); });
    connect(sidebar, &Sidebar::fileContextMenu, this, [this](int idx, QPoint pos) {
        QMenu m; auto* cp = m.addAction(FA::icon(FA::Copy, QColor("#6f7a89"), 14), TXT("copyPath")); auto* rv = m.addAction(FA::icon(FA::FolderOpen, QColor("#6f7a89"), 14), TXT("revealInFolder")); m.addSeparator(); auto* cl = m.addAction(FA::icon(FA::Xmark, QColor("#e5484d"), 14), TXT("closeFile"));
        auto* r = m.exec(pos); if (!r || idx >= int(docs.size())) return;
        if (r == cp) QApplication::clipboard()->setText(docs[idx].path); else if (r == rv) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(docs[idx].path).absolutePath())); else if (r == cl) closeDoc(idx);
    });
    connect(sidebar, &Sidebar::pageContextMenu, this, [this](QList<int>, QPoint pos) {
        QMenu m; auto* rl = m.addAction(FA::icon(FA::RotateLeft, kBlue, 14), TXT("rotateL")); auto* rr = m.addAction(FA::icon(FA::RotateRight, kBlue, 14), TXT("rotateR")); auto* ex = m.addAction(FA::icon(FA::FileExport, kBlue, 14), TXT("extract")); m.addSeparator(); auto* del = m.addAction(FA::icon(FA::Trash, QColor("#e5484d"), 14), TXT("deletePages"));
        auto* r = m.exec(pos); if (r == rl) tool("rotateL"); else if (r == rr) tool("rotate"); else if (r == ex) tool("extract"); else if (r == del) tool("delete");
    });
    connect(classify, &ClassifyPanel::applyRequested, this, &MainWindow::applyLabel); connect(classify, &ClassifyPanel::removeRequested, this, &MainWindow::removeLabel); connect(classify, &ClassifyPanel::batchRequested, this, &MainWindow::batchLabel);
    connect(langBox, &QComboBox::currentIndexChanged, this, [this](int i) { I18n::setLang(i == 1 ? "en" : "th"); retranslate(); });
    retranslate(); refreshAll();
}

void MainWindow::buildToolbox() {
    addToolBarBreak();
    toolbox = addToolBar("tools"); toolbox->setObjectName("toolbox"); toolbox->setMovable(false); toolbox->setIconSize(QSize(18, 18)); toolbox->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addToolBarBreak();
    auto* strip = addToolBar("rgb"); strip->setObjectName("rgbstrip"); strip->setMovable(false); strip->setFixedHeight(3); strip->setContentsMargins(0, 0, 0, 0);
    auto* rgb = new QFrame; rgb->setObjectName("rgbbar"); rgb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); strip->addWidget(rgb);
    int lastGroup = -1;
    for (const ToolDef& d : toolDefs()) {
        if (lastGroup >= 0 && d.group != lastGroup) toolbox->addSeparator();
        lastGroup = d.group;
        QAction* a = toolbox->addAction(FA::icon(d.icon, QColor(d.color), 18), TXT(d.label)); const QString key = d.key;
        connect(a, &QAction::triggered, this, [this, key]() { tool(key); });
        toolActs << a;
    }
}

void MainWindow::showHome() { homeMode = true; home->refresh(); center->setCurrentWidget(home); homeAct->setEnabled(false); }
void MainWindow::retranslate() {
    homeAct->setText(TXT("home")); if (auto* fm = findChild<QMenu*>("fileMenu")) fm->setTitle(TXT("files"));
    openAct->setText(TXT("open")); saveAct->setText(TXT("saveAs")); undoAct->setText(TXT("undo")); undoAct->setToolTip("⌘Z"); settingsAct->setToolTip(TXT("settings")); zoomInAct->setToolTip(TXT("zoomIn")); zoomOutAct->setToolTip(TXT("zoomOut")); fitWAct->setToolTip(TXT("fitWidth")); fitPAct->setToolTip(TXT("fitPage"));
    modifiedBadge->setText(TXT("modified")); rightTabs->setTabText(0, TXT("classify")); rightTabs->setTabText(1, TXT("info"));
    rightTabs->setTabIcon(0, FA::icon(FA::Tag, QColor("#f4a33f"), 14)); rightTabs->setTabIcon(1, FA::icon(FA::Info, QColor("#34c77b"), 14));
    for (int i = 0; i < toolActs.size(); i++) toolActs[i]->setText(TXT(toolDefs()[i].label));
    { const char* keys[] = {"etSelect", "etText", "etImage", "etLine", "etRect", "etField", "etCheckbox"}; for (int i = 0; i < editToolActs.size() && i < 7; i++) editToolActs[i]->setText(TXT(keys[i])); if (delAct) delAct->setText(TXT("deleteObject")); if (fillChk) fillChk->setText(TXT("fill")); if (auto* b = editbar ? editbar->findChild<QPushButton*>("editCancel") : nullptr) b->setText(TXT("cancelEdit")); if (auto* b = editbar ? editbar->findChild<QPushButton*>("primary") : nullptr) b->setText(TXT("finishEdit")); }
    home->retranslate();
    sidebar->retranslate(); classify->retranslate(); refreshAll();
}
void MainWindow::setBusy(const QString& text) { busyLbl->setText(text); busyLbl->setVisible(!text.isEmpty()); progress->setVisible(!text.isEmpty()); progress->setRange(0, text.isEmpty() ? 1 : 0); QApplication::processEvents(); }
void MainWindow::toast(const QString& text, const QString& kind) { statusBar()->setStyleSheet(kind == "err" ? "color:#c62828" : kind == "warn" ? "color:#9a6400" : "color:#1b7f44"); statusBar()->showMessage(text, kind == "err" ? 9000 : 4000); }

bool MainWindow::openOne(const QString& path) {
    for (int i = 0; i < int(docs.size()); i++) if (docs[i].path == path) { homeMode = false; setActive(i); return true; }
    setBusy(TXT("loading"));
    QString pw; bool wrong = false; std::shared_ptr<PdfDoc> pdf;
    for (int tries = 0; tries < 5 && !pdf; tries++) {
        bool needs = false; pdf = PdfDoc::open(path, pw, &needs);
        if (!pdf) {
            if (!needs) { setBusy(""); toast(QString("%1: %2").arg(TXT("error"), QFileInfo(path).fileName()), "err"); return false; }
            const QString p = Dialogs::password(this, QFileInfo(path).fileName(), wrong); if (p.isEmpty()) { setBusy(""); return false; } pw = p; wrong = true;
        }
    }
    if (!pdf) { setBusy(""); return false; }
    Document d; d.name = QFileInfo(path).fileName(); d.path = path; d.workPath = path; d.password = pw; d.pdf = pdf; d.info = Core::info(path, pw);
    HomeView::remember(path, pdf->pages(), pdf->render(0, 186, 2.0));
    docs.push_back(std::move(d)); setActive(int(docs.size()) - 1); setBusy(""); return true;
}
void MainWindow::openPaths(const QStringList& paths, const QString& action) {
    for (const QString& p : paths) if (p.endsWith(".pdf", Qt::CaseInsensitive) && QFileInfo::exists(p)) openOne(p);
    if (!paths.isEmpty() && active()) { if (action == "classify") rightTabs->setCurrentIndex(1); else if (action == "encrypt") tool("encrypt"); }
    raise(); activateWindow();
}
void MainWindow::setActive(int idx) {
    if (idx < 0 || idx >= int(docs.size())) { activeIdx = -1; } else { activeIdx = idx; homeMode = false; }
    refreshAll();
}
void MainWindow::closeDoc(int idx) { if (idx < 0 || idx >= int(docs.size())) return; docs.erase(docs.begin() + idx); if (activeIdx >= int(docs.size())) activeIdx = int(docs.size()) - 1; refreshAll(); }
void MainWindow::refreshFiles() { QStringList names, paths; QList<bool> mods, chk; for (auto& d : docs) { names << d.name; paths << d.path; mods << d.modified; chk << d.checked; } sidebar->setFiles(names, paths, mods, chk, activeIdx); }
void MainWindow::refreshAll() {
    refreshFiles();
    Document* d = active();
    if (d && !homeMode) { center->setCurrentWidget(viewer); viewer->setDocument(d->pdf); sidebar->setDocument(d->pdf, d->workPath); setWindowTitle(QString("%1 — PDF DLP").arg(d->name)); }
    else { homeMode = true; home->refresh(); center->setCurrentWidget(home); viewer->clear(); sidebar->setDocument(nullptr, ""); setWindowTitle("PDF DLP"); pageEdit->clear(); pageTotal->setText(" / 0 "); }
    homeAct->setEnabled(!homeMode);
    saveAct->setEnabled(d != nullptr); undoAct->setEnabled(d && !d->history.isEmpty()); modifiedBadge->setVisible(d && d->modified);
    for (int i = 0; i < toolActs.size(); i++) { const QString k = toolDefs()[i].key; toolActs[i]->setEnabled(d || k == "merge" || k == "img2pdf"); } classify->setDocument(d); infoPanel->setDocument(d);
}
void MainWindow::reloadActive(const QString& newWorkPath, const QString& password, bool modified) {
    Document* d = active(); if (!d) return;
    bool needs = false; auto pdf = PdfDoc::open(newWorkPath, password, &needs);
    if (!pdf) { toast(TXT("error") + ": reload", "err"); return; }
    d->history << d->workPath; while (d->history.size() > 20) d->history.removeFirst();
    d->workPath = newWorkPath; d->password = password; d->pdf = pdf; d->modified = modified; d->info = Core::info(newWorkPath, password);
    refreshAll();
}
void MainWindow::runOps(const QJsonArray& ops, const QString& nextPassword, bool keepPassword, std::function<void()> after) {
    Document* d = active(); if (!d) return;
    const int idx = activeIdx; const QString path = d->workPath, pw = d->password; const QString newPw = nextPassword.isNull() ? (keepPassword ? pw : QString()) : nextPassword;
    setBusy(TXT("loading"));
    Core::async([=]() { return Core::apply(path, pw, ops); }, [this, idx, newPw, after](QJsonObject r) {
        setBusy("");
        if (idx != activeIdx) return;
        if (!r.value("ok").toBool()) { toast(QString("%1: %2").arg(TXT("error"), r.value("error").toString()), "err"); return; }
        reloadActive(r.value("path").toString(), newPw, true);
        if (after) after();
    });
}
/// Write the current work copy back over the original file (keeps a backup in the app data folder).
bool MainWindow::writeBack(Document& d) {
    if (d.path.isEmpty() || !QFileInfo::exists(d.path) || d.path == d.workPath) return false;
    const QString bdir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/backup"; QDir().mkpath(bdir);
    const QString backup = bdir + "/" + QFileInfo(d.path).completeBaseName() + "-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".pdf";
    QFile::copy(d.path, backup);
    QFile::remove(d.path);
    if (!QFile::copy(d.workPath, d.path)) { toast(TXT("error") + ": write back", "err"); return false; }
    d.modified = false; HomeView::remember(d.path, d.pages(), d.pdf->render(0, 186, 2.0)); return true;
}
void MainWindow::openProduced(const QString& path, const QString& name, const QString& password) {
    bool needs = false; auto pdf = PdfDoc::open(path, password, &needs); if (!pdf) { toast(TXT("error"), "err"); return; }
    Document d; d.name = name; d.path = name; d.workPath = path; d.password = password; d.pdf = pdf; d.info = Core::info(path, password); d.modified = true;
    docs.push_back(std::move(d)); setActive(int(docs.size()) - 1);
}
void MainWindow::undo() {
    Document* d = active(); if (!d || d->history.isEmpty()) return;
    const QString prev = d->history.takeLast();
    bool needs = false; auto pdf = PdfDoc::open(prev, d->password, &needs); if (!pdf) { toast(TXT("error"), "err"); return; }
    d->workPath = prev; d->pdf = pdf; d->modified = prev != d->path; d->info = Core::info(prev, d->password); refreshAll();
}
void MainWindow::saveAs() {
    Document* d = active(); if (!d) return;
    const QString dest = QFileDialog::getSaveFileName(this, TXT("saveAs"), d->path.contains('/') ? d->path : QDir::homePath() + "/" + d->name, "PDF (*.pdf)");
    if (dest.isEmpty()) return;
    if (QFileInfo::exists(dest)) QFile::remove(dest);
    if (!QFile::copy(d->workPath, dest)) { toast(TXT("error") + ": save", "err"); return; }
    d->path = dest; d->name = QFileInfo(dest).fileName(); d->modified = false; HomeView::remember(dest, d->pages(), d->pdf->render(0, 186, 2.0)); refreshAll(); toast(QString("%1: %2").arg(TXT("saved"), d->name));
}
QList<int> MainWindow::pagesOrCurrent() { QList<int> s = sidebar->selectedPages(); if (s.isEmpty()) s << viewer->currentPage() + 1; return s; }
static QJsonArray arr(const QList<int>& l) { QJsonArray a; for (int v : l) a.append(v); return a; }

void MainWindow::tool(const QString& key) {
    Document* d = active();
    if (!d && key != "merge" && key != "img2pdf") return;
    QJsonArray ops;
    if (key == "rotate") runOps({QJsonObject{{"op", "rotate"}, {"pages", arr(pagesOrCurrent())}, {"angle", 90}}});
    else if (key == "rotateL") runOps({QJsonObject{{"op", "rotate"}, {"pages", arr(pagesOrCurrent())}, {"angle", -90}}});
    else if (key == "delete") { const auto ps = pagesOrCurrent(); if (ps.size() >= d->pages()) { toast(TXT("cannotDeleteAll"), "warn"); return; } if (QMessageBox::question(this, TXT("deletePages"), QString("%1 (%2)").arg(TXT("confirmDelete")).arg(ps.size())) == QMessageBox::Yes) runOps({QJsonObject{{"op", "delete"}, {"pages", arr(ps)}}}); }
    else if (key == "extract") { const auto ps = pagesOrCurrent(); const QJsonObject r = Core::apply(d->workPath, d->password, {QJsonObject{{"op", "extract"}, {"pages", arr(ps)}}}); if (!r.value("ok").toBool()) { toast(r.value("error").toString(), "err"); return; } openProduced(r.value("path").toString(), QFileInfo(d->name).completeBaseName() + QString("-p%1%2.pdf").arg(ps.first()).arg(ps.size() > 1 ? QString("-%1").arg(ps.last()) : ""), d->password); }
    else if (key == "encrypt") { auto r = Dialogs::encrypt(this); if (r.ok) runOps(r.ops, r.nextPassword); }
    else if (key == "decrypt") { if (Dialogs::decrypt(this, *d, ops)) runOps(ops, QString(""), false); }
    else if (key == "split") { QString mode, spec, dir; unsigned every = 1; if (Dialogs::split(this, *d, mode, spec, every, dir)) { setBusy(TXT("split")); const QString p = d->workPath, pw = d->password; Core::async([=]() { return Core::split(p, pw, mode, spec, every, dir); }, [this, dir](QJsonObject r) { setBusy(""); if (r.value("ok").toBool()) toast(QString("%1: %2 %3 → %4").arg(TXT("split")).arg(r.value("files").toArray().size()).arg(TXT("filesUnit"), dir)); else toast(r.value("error").toString(), "err"); }); } }
    else if (key == "merge") { QStringList paths, names, open; const bool anyChecked = std::any_of(docs.begin(), docs.end(), [](const Document& x) { return x.checked; }); for (auto& x : docs) if (!anyChecked || x.checked) { names << x.name; open << x.workPath; } if (Dialogs::merge(this, paths, names, open)) { QJsonArray inputs; for (const QString& p : paths) { QJsonObject o{{"path", p}}; for (auto& x : docs) if (x.workPath == p) o["password"] = x.password; inputs.append(o); } setBusy(TXT("merge")); Core::async([=]() { return Core::merge(inputs); }, [this](QJsonObject r) { setBusy(""); if (r.value("ok").toBool()) openProduced(r.value("path").toString(), "merged.pdf"); else toast(r.value("error").toString(), "err"); }); } }
    else if (key == "insert") { QString file; int after = 0; if (Dialogs::insertPages(this, *d, viewer->currentPage() + 1, file, after)) { QJsonArray inputs; QJsonArray before, afterArr; for (int i = 1; i <= after; i++) before.append(i); for (int i = after + 1; i <= d->pages(); i++) afterArr.append(i); if (!before.isEmpty()) inputs.append(QJsonObject{{"path", d->workPath}, {"password", d->password}, {"pages", before}}); inputs.append(QJsonObject{{"path", file}}); if (!afterArr.isEmpty()) inputs.append(QJsonObject{{"path", d->workPath}, {"password", d->password}, {"pages", afterArr}}); setBusy(TXT("insert")); const int idx = activeIdx; Core::async([=]() { return Core::merge(inputs); }, [this, idx](QJsonObject r) { setBusy(""); if (idx != activeIdx) return; if (r.value("ok").toBool()) reloadActive(r.value("path").toString(), "", true); else toast(r.value("error").toString(), "err"); }); } }
    else if (key == "watermark") { if (Dialogs::watermark(this, sidebar->selectedPages(), ops)) runOps(ops); }
    else if (key == "pagenum") { if (Dialogs::pageNumbers(this, *d, ops)) runOps(ops); }
    else if (key == "crop") { if (Dialogs::crop(this, pagesOrCurrent(), ops)) runOps(ops); }
    else if (key == "images") { QString dir, fmt; int dpi = 150; QList<int> pages; if (Dialogs::exportImages(this, *d, pagesOrCurrent(), dir, fmt, dpi, pages)) { setBusy(TXT("exportImages")); int n = 0; for (int p : pages) { QImage img = d->pdf->render(p - 1, int(d->pdf->sizes[p - 1].width() * dpi / 72.0)); if (img.save(QString("%1/%2-p%3.%4").arg(dir, QFileInfo(d->name).completeBaseName()).arg(p, QString::number(d->pages()).size(), 10, QChar('0')).arg(fmt), fmt == "png" ? "PNG" : "JPEG", 92)) n++; } setBusy(""); toast(QString("%1: %2 → %3").arg(TXT("exportImages")).arg(n).arg(dir)); } }
    else if (key == "img2pdf") { QStringList files; bool a4 = true; if (Dialogs::imagesToPdf(this, files, a4)) { const QJsonObject r = Core::imagesToPdf(files, a4); if (r.value("ok").toBool()) openProduced(r.value("path").toString(), "images.pdf"); else toast(r.value("error").toString(), "err"); } }
    else if (key == "compress") { const qint64 before = QFileInfo(d->workPath).size(); if (Dialogs::compress(this, *d, ops)) { const int idx = activeIdx; const QString p = d->workPath, pw = d->password; setBusy(TXT("compress")); Core::async([=]() { return Core::apply(p, pw, ops); }, [this, idx, before](QJsonObject r) { setBusy(""); if (idx != activeIdx) return; if (!r.value("ok").toBool()) { toast(r.value("error").toString(), "err"); return; } reloadActive(r.value("path").toString(), docs[idx].password, true); toast(QString("%1: %2 → %3").arg(TXT("compress"), QLocale().formattedDataSize(before), QLocale().formattedDataSize(QFileInfo(r.value("path").toString()).size()))); }); } }
    else if (key == "sanitize") { if (Dialogs::sanitize(this, ops)) runOps(ops); }
    else if (key == "props") { if (Dialogs::properties(this, *d, ops)) runOps(ops); }
    else if (key == "redact") { if (Dialogs::redact(this, *d, viewer->currentPage() + 1, ops)) runOps(ops); }
    else if (key == "print") printDoc();
    else if (key == "email") emailDoc();
    else if (key == "edit") enterEdit();
    else if (key == "present") present();
}

void MainWindow::applyLabel() {
    Document* d = active(); const QJsonObject label = classify->selectedLabel(); if (!d || label.isEmpty()) return;
    QString next; const QJsonArray ops = classify->buildOps(label, d->info.value("metadata").toObject(), &next);
    const QString lname = label.value("name").toString(); const bool autosave = classify->autoSave();
    runOps(ops, next.isEmpty() ? QString() : next, true, [this, lname, autosave]() { Document* x = active(); if (x && autosave && writeBack(*x)) { refreshAll(); toast(QString("%1: %2 · %3").arg(TXT("labelApplied"), lname, TXT("savedToFile"))); } else toast(QString("%1: %2").arg(TXT("labelApplied"), lname)); });
}
void MainWindow::removeLabel() { Document* d = active(); if (!d) return; const bool autosave = classify->autoSave(); runOps(classify->buildRemoveOps(d->info.value("metadata").toObject()), QString(), true, [this, autosave]() { Document* x = active(); if (x && autosave && writeBack(*x)) { refreshAll(); toast(TXT("labelRemoved") + " · " + TXT("savedToFile")); } else toast(TXT("labelRemoved")); }); }
void MainWindow::batchLabel() {
    const QJsonObject label = classify->selectedLabel(); if (label.isEmpty()) return;
    // checked open files → apply in place (undoable, saved later); none checked → pick files + output folder
    std::vector<int> checkedIdx; for (int i = 0; i < int(docs.size()); i++) if (docs[i].checked) checkedIdx.push_back(i);
    int ok = 0;
    if (!checkedIdx.empty()) {
        for (size_t k = 0; k < checkedIdx.size(); k++) {
            Document& d = docs[checkedIdx[k]]; setBusy(QString("%1 %2/%3").arg(TXT("batch")).arg(k + 1).arg(checkedIdx.size()));
            QString next; const QJsonArray ops = classify->buildOps(label, d.info.value("metadata").toObject(), &next);
            const QJsonObject r = Core::apply(d.workPath, d.password, ops);
            if (!r.value("ok").toBool()) { toast(QString("%1: %2").arg(d.name, r.value("error").toString()), "err"); continue; }
            const QString newPw = next.isEmpty() ? d.password : next; bool needs = false; auto pdf = PdfDoc::open(r.value("path").toString(), newPw, &needs); if (!pdf) continue;
            d.history << d.workPath; d.workPath = r.value("path").toString(); d.password = newPw; d.pdf = pdf; d.modified = true; d.info = Core::info(d.workPath, newPw); ok++;
            if (classify->autoSave()) writeBack(d);
        }
        setBusy(""); refreshAll(); toast(QString("%1 %2: %3/%4%5").arg(TXT("batch"), TXT("done")).arg(ok).arg(checkedIdx.size()).arg(classify->autoSave() ? " · " + TXT("savedToFile") : "")); return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, TXT("batch"), QString(), "PDF (*.pdf)"); if (files.isEmpty()) return;
    const QString dir = QFileDialog::getExistingDirectory(this, TXT("chooseFolder")); if (dir.isEmpty()) return;
    for (int i = 0; i < files.size(); i++) {
        setBusy(QString("%1 %2/%3").arg(TXT("batch")).arg(i + 1).arg(files.size()));
        QString next; const QJsonArray ops = classify->buildOps(label, {}, &next);
        const QJsonObject r = Core::apply(files[i], "", ops, dir + "/" + QFileInfo(files[i]).fileName());
        if (r.value("ok").toBool()) ok++; else toast(QString("%1: %2").arg(QFileInfo(files[i]).fileName(), r.value("error").toString()), "err");
    }
    setBusy(""); toast(QString("%1 %2: %3/%4 → %5").arg(TXT("batch"), TXT("done")).arg(ok).arg(files.size()).arg(dir));
}
void MainWindow::printDoc() {
    Document* d = active(); if (!d) return;
    QPrinter printer(QPrinter::HighResolution); QPrintDialog dlg(&printer, this); if (dlg.exec() != QDialog::Accepted) return;
    QPainter p(&printer);
    for (int i = 0; i < d->pages(); i++) {
        if (i) printer.newPage();
        const QRect page = printer.pageRect(QPrinter::DevicePixel).toRect(); QImage img = d->pdf->render(i, page.width());
        const QSize sz = img.size().scaled(page.size(), Qt::KeepAspectRatio); p.drawImage(QRect(page.topLeft(), sz), img);
    }
}
QList<QPair<QString, QByteArray>> MainWindow::editFonts() {
    QList<QPair<QString, QByteArray>> out;
    QStringList dirs = {QCoreApplication::applicationDirPath() + "/../Resources/fonts/edit", QCoreApplication::applicationDirPath() + "/fonts/edit"};
    QStringList order = {"NotoSansThai-Regular", "NotoSansThai-Bold", "Sarabun-Regular", "Sarabun-Bold", "Kanit-Regular", "Kanit-Bold", "Prompt-Regular", "Prompt-Bold", "Mitr-Regular", "BaiJamjuree-Regular", "BaiJamjuree-Bold", "IBMPlexSansThai-Regular", "IBMPlexSansThai-Bold", "ChakraPetch-Regular", "Pridi-Regular", "Taviraj-Regular", "Athiti-Regular", "Lato-Regular", "Lato-Bold", "Poppins-Regular", "Poppins-Bold", "PT_Sans-Web-Regular", "Outfit-Regular", "Outfit-Bold", "CourierPrime-Regular"};
    for (const QString& d : dirs) {
        if (!QDir(d).exists()) continue;
        QStringList files = QDir(d).entryList({"*.ttf"}, QDir::Files);
        std::sort(files.begin(), files.end(), [&](const QString& a, const QString& b) { int ia = order.indexOf(QFileInfo(a).completeBaseName()), ib = order.indexOf(QFileInfo(b).completeBaseName()); if (ia < 0) ia = 999; if (ib < 0) ib = 999; return ia != ib ? ia < ib : a < b; });
        for (const QString& f : files) { QFile file(d + "/" + f); if (file.open(QIODevice::ReadOnly)) { QString name = QFileInfo(f).completeBaseName().replace("PT_Sans-Web", "PT Sans").replace("NotoSansThai", "Noto Sans Thai").replace("IBMPlexSansThai", "IBM Plex Sans Thai").replace("BaiJamjuree", "Bai Jamjuree").replace("ChakraPetch", "Chakra Petch").replace("CourierPrime", "Courier Prime").replace("-Regular", "").replace("-", " "); out << qMakePair(name, file.readAll()); } }
        break;
    }
    return out;
}
void MainWindow::buildEditBar() {
    editbar = new QToolBar; editbar->setObjectName("editbar"); editbar->setMovable(false); editbar->setIconSize(QSize(15, 15)); editbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    editTools = new QActionGroup(this); editTools->setExclusive(true);
    struct T { EditTool t; ushort icon; const char* key; };
    const T defs[] = {{EditTool::Select, FA::ArrowPointer, "etSelect"}, {EditTool::Text, FA::Font, "etText"}, {EditTool::Image, FA::Image, "etImage"}, {EditTool::Line, FA::Slash, "etLine"}, {EditTool::Rect, FA::Square, "etRect"}, {EditTool::TextField, FA::ICursor, "etField"}, {EditTool::Checkbox, FA::SquareCheck, "etCheckbox"}};
    for (const T& d : defs) { QAction* a = editbar->addAction(FA::icon(d.icon, QColor("#c62828"), 15), TXT(d.key)); a->setCheckable(true); a->setData(int(d.t)); editTools->addAction(a); editToolActs << a; const EditTool t = d.t; connect(a, &QAction::triggered, this, [this, t]() { viewer->edit.tool = t; }); }
    editToolActs.first()->setChecked(true);
    editbar->addSeparator();
    fontBox = new QComboBox; fontBox->setMinimumWidth(150); editbar->addWidget(fontBox); connect(fontBox, &QComboBox::currentTextChanged, this, [this](const QString& f) { viewer->edit.font = f; });
    sizeBox = new QDoubleSpinBox; sizeBox->setRange(4, 200); sizeBox->setValue(14); sizeBox->setDecimals(0); sizeBox->setSuffix(" pt"); sizeBox->setFixedWidth(70); editbar->addWidget(sizeBox); connect(sizeBox, &QDoubleSpinBox::valueChanged, this, [this](double v) { viewer->edit.fontSize = v; });
    colorBtn = new QPushButton; colorBtn->setFixedSize(26, 26); colorBtn->setToolTip(TXT("color")); editbar->addWidget(colorBtn);
    auto paintBtn = [](QPushButton* b, const QColor& c) { b->setStyleSheet(QString("background:%1;border:1px solid #c5cfdc;border-radius:6px;").arg(c.name())); };
    paintBtn(colorBtn, viewer->edit.color);
    connect(colorBtn, &QPushButton::clicked, this, [this, paintBtn]() { const QColor c = QColorDialog::getColor(viewer->edit.color, this); if (c.isValid()) { viewer->edit.color = c; viewer->edit.strokeColor = c; paintBtn(colorBtn, c); } });
    strokeBox = new QDoubleSpinBox; strokeBox->setRange(0.2, 30); strokeBox->setValue(1.5); strokeBox->setDecimals(1); strokeBox->setSuffix(" px"); strokeBox->setFixedWidth(70); strokeBox->setToolTip(TXT("strokeWidth")); editbar->addWidget(strokeBox); connect(strokeBox, &QDoubleSpinBox::valueChanged, this, [this](double v) { viewer->edit.stroke = v; });
    fillChk = new QCheckBox(TXT("fill")); editbar->addWidget(fillChk); connect(fillChk, &QCheckBox::toggled, this, [this](bool on) { viewer->edit.filled = on; });
    fillBtn = new QPushButton; fillBtn->setFixedSize(26, 26); fillBtn->setToolTip(TXT("fillColor")); editbar->addWidget(fillBtn); paintBtn(fillBtn, viewer->edit.fill);
    connect(fillBtn, &QPushButton::clicked, this, [this, paintBtn]() { const QColor c = QColorDialog::getColor(viewer->edit.fill, this, QString(), QColorDialog::ShowAlphaChannel); if (c.isValid()) { viewer->edit.fill = c; paintBtn(fillBtn, c); } });
    editbar->addSeparator();
    delAct = editbar->addAction(FA::icon(FA::TrashCan, QColor("#e5484d"), 15), TXT("deleteObject")); delAct->setEnabled(false); connect(delAct, &QAction::triggered, this, [this]() { viewer->deleteSelected(); });
    auto* sp = new QWidget; sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); editbar->addWidget(sp);
    auto* cancel = new QPushButton(TXT("cancelEdit")); cancel->setObjectName("editCancel"); editbar->addWidget(cancel); connect(cancel, &QPushButton::clicked, this, [this]() { finishEdit(false); });
    auto* done = new QPushButton(TXT("finishEdit")); done->setObjectName("primary"); done->setIcon(FA::icon(FA::CheckCircle, Qt::white, 14)); editbar->addWidget(done); connect(done, &QPushButton::clicked, this, [this]() { finishEdit(true); });
    connect(viewer, &Viewer::selectionChanged, this, [this]() { delAct->setEnabled(viewer->hasSelection()); });
    connect(viewer, &Viewer::textRequested, this, [this](int page, QPointF pt) {
        if (!session) return;
        auto* d = Dialogs::make(this, TXT("etText"), 460); auto* te = new QTextEdit; te->setMinimumHeight(90); static_cast<QVBoxLayout*>(d->layout())->addWidget(te); auto* ok = Dialogs::primary(TXT("ok")); auto* row = new QHBoxLayout; row->addStretch(); row->addWidget(ok); static_cast<QVBoxLayout*>(d->layout())->addLayout(row); connect(ok, &QPushButton::clicked, d, &QDialog::accept); te->setFocus();
        if (d->exec() == QDialog::Accepted && !te->toPlainText().trimmed().isEmpty()) { session->addText(page, pt, te->toPlainText(), viewer->edit.fontSize, viewer->edit.color, viewer->edit.font); viewer->refreshPage(page); }
        d->deleteLater();
    });
    connect(viewer, &Viewer::editTextRequested, this, [this](int page, int obj, QString cur) {
        if (!session) return;
        auto* d = Dialogs::make(this, TXT("editText"), 460); auto* te = new QTextEdit(cur); te->setMinimumHeight(90); static_cast<QVBoxLayout*>(d->layout())->addWidget(te); auto* hint = new QLabel(TXT("editTextHint")); hint->setObjectName("hint"); hint->setWordWrap(true); static_cast<QVBoxLayout*>(d->layout())->addWidget(hint); auto* ok = Dialogs::primary(TXT("ok")); auto* row = new QHBoxLayout; row->addStretch(); row->addWidget(ok); static_cast<QVBoxLayout*>(d->layout())->addLayout(row); connect(ok, &QPushButton::clicked, d, &QDialog::accept); te->setFocus(); te->selectAll();
        if (d->exec() == QDialog::Accepted && te->toPlainText() != cur) { session->setText(page, obj, te->toPlainText(), viewer->edit.font); viewer->refreshPage(page); }
        d->deleteLater();
    });
    connect(viewer, &Viewer::imageRectDrawn, this, [this](int page, QRectF r) {
        if (!session) return; const QString f = QFileDialog::getOpenFileName(this, TXT("image"), QString(), "Images (*.png *.jpg *.jpeg *.webp)"); if (f.isEmpty()) return;
        QImage img(f); if (img.isNull()) return; const double ar = double(img.height()) / img.width(); QRectF rr(r.topLeft(), QSizeF(r.width(), r.width() * ar)); session->addImage(page, rr, img); viewer->refreshPage(page);
    });
    connect(viewer, &Viewer::fieldRectDrawn, this, [this](int page, QRectF r, bool checkbox) {
        if (!session) return;
        const QSizeF s = session->size(page); const QString name = QInputDialog::getText(this, checkbox ? TXT("etCheckbox") : TXT("etField"), TXT("fieldName"), QLineEdit::Normal, QString("%1%2").arg(checkbox ? "check" : "field").arg(pendingFieldOps.size() + 1)); if (name.isEmpty()) return;
        pendingFieldOps.append(QJsonObject{{"op", "add_field"}, {"page", page + 1}, {"kind", checkbox ? "checkbox" : "text"}, {"name", name}, {"x", r.x() / s.width()}, {"y", r.y() / s.height()}, {"w", r.width() / s.width()}, {"h", r.height() / s.height()}, {"value", ""}, {"font_size", 0}});
        // preview: draw the field frame into the session so the user sees it now
        session->addRect(page, r, QColor("#8c99ad"), 0.8, QColor("#f7f9ff"), true); viewer->refreshPage(page);
        toast(QString("%1: %2").arg(checkbox ? TXT("etCheckbox") : TXT("etField"), name));
    });
}
void MainWindow::enterEdit() {
    Document* d = active(); if (!d || session) return;
    session = std::make_unique<EditSession>(d->workPath, d->password, editFonts());
    if (!session->ok()) { toast(TXT("error") + ": " + session->error, "err"); session.reset(); return; }
    pendingFieldOps = QJsonArray();
    fontBox->blockSignals(true); fontBox->clear(); fontBox->addItems(session->fontNames()); fontBox->blockSignals(false); viewer->edit.font = fontBox->currentText();
    viewer->edit.tool = EditTool::Select; editToolActs.first()->setChecked(true);
    viewer->setEditSession(session.get()); editbar->show(); toolbox->setEnabled(false); sidebar->setEnabled(false);
    viewer->setFocus();
}
void MainWindow::finishEdit(bool save) {
    if (!session) return;
    Document* d = active();
    if (save && d && (session->dirty || !pendingFieldOps.isEmpty())) {
        const QString tmp = QDir::tempPath() + "/pdfdlp/edit-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz") + ".pdf"; QDir().mkpath(QFileInfo(tmp).absolutePath());
        if (!session->save(tmp)) { toast(TXT("error") + ": " + session->error, "err"); }
        else {
            QString out = tmp; QString pw = d->password;
            if (!pendingFieldOps.isEmpty()) { const QJsonObject r = Core::apply(tmp, pw, pendingFieldOps); if (r.value("ok").toBool()) out = r.value("path").toString(); else { const QJsonObject r2 = Core::apply(tmp, "", pendingFieldOps); if (r2.value("ok").toBool()) { out = r2.value("path").toString(); pw = ""; } else toast(r.value("error").toString(), "err"); } }
            viewer->setEditSession(nullptr); session.reset();
            bool needs = false; auto test = PdfDoc::open(out, pw, &needs); if (!test && needs) pw = "";
            reloadActive(out, pw, true); toast(TXT("editSaved"));
        }
    }
    viewer->setEditSession(nullptr); session.reset(); pendingFieldOps = QJsonArray();
    editbar->hide(); toolbox->setEnabled(true); sidebar->setEnabled(true); viewer->refreshAllPages();
}
void MainWindow::present() {
    Document* d = active(); if (!d) return;
    auto* w = new PresentWidget(d->pdf, viewer->currentPage()); w->showFullScreen(); w->setFocus();
}
/// Open the default mail client (Outlook preferred) with the current version of the document attached.
void MainWindow::emailDoc() {
    Document* d = active(); if (!d) return;
    // attach a copy carrying the real file name (work copies live in temp with a timestamp suffix)
    const QString dir = QDir::tempPath() + "/pdfdlp/mail"; QDir().mkpath(dir);
    const QString att = dir + "/" + d->name; QFile::remove(att);
    if (!QFile::copy(d->workPath, att)) { toast(TXT("error") + ": copy", "err"); return; }
    const QString subject = QFileInfo(d->name).completeBaseName() + (d->classification().isEmpty() ? QString() : QString(" [%1]").arg(d->classification()));
#ifdef Q_OS_MAC
    const bool hasOutlook = QFileInfo::exists("/Applications/Microsoft Outlook.app");
    QString script = hasOutlook
        ? QString("tell application \"Microsoft Outlook\"\nactivate\nset m to make new outgoing message with properties {subject:\"%1\"}\nmake new attachment at m with properties {file:POSIX file \"%2\"}\nopen m\nend tell").arg(subject, att)
        : QString("tell application \"Mail\"\nactivate\nset m to make new outgoing message with properties {subject:\"%1\", visible:true}\ntell m to make new attachment with properties {file name:POSIX file \"%2\"} at after the last paragraph\nend tell").arg(subject, att);
    QProcess::startDetached("osascript", {"-e", script});
    toast(hasOutlook ? "Outlook" : "Mail");
#elif defined(Q_OS_WIN)
    const QString ps = QString("$o=New-Object -ComObject Outlook.Application; $m=$o.CreateItem(0); $m.Subject='%1'; $m.Attachments.Add('%2') | Out-Null; $m.Display()").arg(QString(subject).replace("'", "''"), QString(att).replace("'", "''"));
    QProcess::startDetached("powershell", {"-NoProfile", "-WindowStyle", "Hidden", "-Command", ps});
    toast("Outlook");
#else
    QDesktopServices::openUrl(QUrl("mailto:?subject=" + QUrl::toPercentEncoding(subject) + "&attach=" + QUrl::toPercentEncoding(att)));
#endif
}
void MainWindow::showSettings() {
    auto* dlg = Dialogs::make(this, TXT("settings"), 480);
    auto* g = new QGroupBox(TXT("contextMenu")); auto* v = new QVBoxLayout(g); v->addWidget(new QLabel(TXT("menuOpen") + " · " + TXT("menuClassify") + " · " + TXT("menuEncrypt")));
    const QJsonObject st = Core::shell("status"); auto* badge = new QLabel(st.value("supported").toBool() ? (st.value("registered").toBool() ? TXT("registered") : TXT("notRegistered")) : TXT("winOnly")); badge->setObjectName("badge");
    auto* row = new QHBoxLayout; auto* reg = new QPushButton(TXT("register")); reg->setEnabled(st.value("supported").toBool()); auto* unreg = new QPushButton(TXT("unregister")); unreg->setEnabled(st.value("supported").toBool() && st.value("registered").toBool());
    row->addWidget(badge); row->addStretch(); row->addWidget(reg); row->addWidget(unreg); v->addLayout(row);
    connect(reg, &QPushButton::clicked, dlg, [=]() { const QJsonObject r = Core::shell("register", QJsonArray{QJsonObject{{"action", "open"}, {"label", TXT("menuOpen")}}, QJsonObject{{"action", "classify"}, {"label", TXT("menuClassify")}}, QJsonObject{{"action", "encrypt"}, {"label", TXT("menuEncrypt")}}}); badge->setText(r.value("ok").toBool() ? TXT("registered") : r.value("error").toString()); unreg->setEnabled(r.value("ok").toBool()); });
    connect(unreg, &QPushButton::clicked, dlg, [=]() { Core::shell("unregister"); badge->setText(TXT("notRegistered")); unreg->setEnabled(false); });
    static_cast<QVBoxLayout*>(dlg->layout())->addWidget(g); dlg->exec(); dlg->deleteLater();
}
void MainWindow::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent* e) { QStringList ps; for (const QUrl& u : e->mimeData()->urls()) ps << u.toLocalFile(); openPaths(ps); }
void MainWindow::closeEvent(QCloseEvent* e) { QSettings().setValue("geometry", saveGeometry()); e->accept(); }
