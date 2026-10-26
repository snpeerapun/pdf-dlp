// PDF DLP — native C++ prototype (Qt6 Widgets + PDFium). Purpose: compare rendering smoothness with the Tauri build.
#include "viewer.h"
#include <fpdfview.h>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PDF DLP Native");
    FPDF_LIBRARY_CONFIG cfg{}; cfg.version = 2; FPDF_InitLibraryWithConfig(&cfg);

    QMainWindow win; win.setWindowTitle("PDF DLP (native C++)"); win.resize(1380, 880);
    auto* viewer = new Viewer;
    auto* tb = win.addToolBar("main"); tb->setMovable(false);
    auto* openAct = tb->addAction(QIcon::fromTheme("document-open"), "เปิดไฟล์");
    tb->addSeparator();
    auto* pageLbl = new QLabel(" หน้า - / - "); tb->addWidget(pageLbl);
    tb->addSeparator();
    auto* zoomOut = tb->addAction("−"); auto* zoomLbl = new QLabel(" 100% "); tb->addWidget(zoomLbl); auto* zoomIn = tb->addAction("+"); auto* fitAct = tb->addAction("พอดีความกว้าง");
    win.setCentralWidget(viewer);
    win.setStyleSheet("QMainWindow,QToolBar{background:#151a21;color:#e6eaf0;border:0} QLabel{color:#e6eaf0} QToolButton{color:#e6eaf0;padding:6px 10px}");
    QObject::connect(openAct, &QAction::triggered, [&]() { const QString f = QFileDialog::getOpenFileName(&win, "เปิด PDF", QString(), "PDF (*.pdf)"); if (!f.isEmpty()) viewer->open(f); });
    QObject::connect(zoomIn, &QAction::triggered, [&]() { viewer->zoomStep(1); });
    QObject::connect(zoomOut, &QAction::triggered, [&]() { viewer->zoomStep(-1); });
    QObject::connect(fitAct, &QAction::triggered, [&]() { viewer->fitWidth(); });
    QObject::connect(viewer, &Viewer::zoomChanged, [&](double s) { zoomLbl->setText(QString(" %1% ").arg(int(s * 100))); });
    QObject::connect(viewer, &Viewer::pageChanged, [&](int p, int n) { pageLbl->setText(QString(" หน้า %1 / %2 ").arg(p).arg(n)); });
    auto* zi = new QShortcut(QKeySequence::ZoomIn, &win); QObject::connect(zi, &QShortcut::activated, [&]() { viewer->zoomStep(1); });
    auto* zo = new QShortcut(QKeySequence::ZoomOut, &win); QObject::connect(zo, &QShortcut::activated, [&]() { viewer->zoomStep(-1); });
    win.show();
    if (argc > 1) viewer->open(QString::fromUtf8(argv[1]));
    const int rc = app.exec();
    FPDF_DestroyLibrary();
    return rc;
}
