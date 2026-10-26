// PDF DLP — native desktop app (Qt6 Widgets + PDFium renderer + Rust core via C ABI).
#include "mainwindow.h"
#include "i18n.h"
#include <fpdfview.h>
#include <QtNetwork>

class App : public QApplication {
public:
    MainWindow* win = nullptr;
    App(int& argc, char** argv) : QApplication(argc, argv) {}
protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::FileOpen && win) { win->openPaths({static_cast<QFileOpenEvent*>(e)->file()}); return true; }
        return QApplication::event(e);
    }
};

static void parseArgs(const QStringList& args, QString& action, QStringList& paths) {
    action = "open";
    for (int i = 0; i < args.size(); i++) { if (args[i] == "--action" && i + 1 < args.size()) action = args[++i]; else if (!args[i].startsWith("--") && args[i].endsWith(".pdf", Qt::CaseInsensitive)) paths << args[i]; }
}

#include "core.h"
#include "stamps.h"
// Headless end-to-end check of the C++ ↔ Rust core ↔ PDFium chain: --selftest <fixture.pdf> <outdir>
static int selfTest(const QString& fixture, const QString& outDir) {
    int fails = 0; auto step = [&](const QString& name, std::function<QString()> fn) { QElapsedTimer t; t.start(); QString d; bool ok = true; try { d = fn(); } catch (const std::exception& e) { ok = false; d = e.what(); } if (d.startsWith("FAIL")) ok = false; if (!ok) fails++; fprintf(stdout, "%s %s (%lld ms) %s\n", ok ? "OK  " : "FAIL", qPrintable(name), (long long)t.elapsed(), qPrintable(d)); };
    QString work = fixture, pw;
    auto req = [](const QJsonObject& r) { if (!r.value("ok").toBool()) throw std::runtime_error(r.value("error").toString().toStdString()); return r; };
    step("info", [&]() { auto r = req(Core::info(work)); return QString("pages=%1").arg(r.value("pages").toInt()); });
    step("pdfium render p1", [&]() { bool n; auto d = PdfDoc::open(work, "", &n); if (!d) return QString("FAIL open"); QElapsedTimer t; t.start(); QImage img = d->render(0, 1200); img.save(outDir + "/selftest-p1.png"); return QString("%1x%2 in %3 ms").arg(img.width()).arg(img.height()).arg(t.elapsed()); });
    step("rotate+delete", [&]() { auto r = req(Core::apply(work, pw, {QJsonObject{{"op", "rotate"}, {"pages", QJsonArray{1}}, {"angle", 90}}, QJsonObject{{"op", "delete"}, {"pages", QJsonArray{3}}}})); work = r.value("path").toString(); return r.value("pages").toInt() == 2 ? QString("pages=2") : QString("FAIL pages=%1").arg(r.value("pages").toInt()); });
    step("classify badge+watermark+msip", [&]() {
        const Stamps::Png b = Stamps::badge("CONFIDENTIAL", QColor("#f4b63f")); const Stamps::Png wm = Stamps::watermark("CONFIDENTIAL", QColor("#f4b63f")); const double w = 0.3;
        QJsonObject label{{"id", "11111111-2222-3333-4444-555555555555"}, {"name", "Confidential"}, {"order", 2}}; const QJsonObject extra = req(Core::msipMetadata(label, "", 7, "", {})).value("metadata").toObject();
        auto r = req(Core::apply(work, pw, {QJsonObject{{"op", "remove_marks"}}, QJsonObject{{"op", "stamp"}, {"pages", QJsonArray()}, {"png_base64", b.base64}, {"x", 0.5 - w / 2}, {"y", 0.012}, {"w", w}, {"opacity", 1.0}, {"rotate_deg", 0}, {"anchor", "topleft"}}, QJsonObject{{"op", "stamp"}, {"pages", QJsonArray()}, {"png_base64", wm.base64}, {"x", 0.5}, {"y", 0.5}, {"w", 0.75}, {"opacity", 0.18}, {"rotate_deg", 45}, {"anchor", "center"}}, QJsonObject{{"op", "metadata"}, {"classification", "Confidential"}, {"keywords", "Classification: Confidential"}, {"extra", extra}}}));
        work = r.value("path").toString(); auto i = req(Core::info(work)); if (i.value("classification").toString() != "Confidential" || !i.value("has_marks").toBool() || i.value("msip").toObject().value("name").toString() != "Confidential") return QString("FAIL ") + QJsonDocument(i).toJson(QJsonDocument::Compact);
        bool n; auto d = PdfDoc::open(work, "", &n); d->render(0, 900).save(outDir + "/selftest-classified-p1.png"); return QString("msip=%1").arg(i.value("msip").toObject().value("id").toString()); });
    step("encrypt AES-256 + wrong password rejected", [&]() { auto r = req(Core::apply(work, pw, {QJsonObject{{"op", "encrypt"}, {"user_password", "secret"}, {"owner_password", "owner"}, {"allow_print", true}, {"allow_copy", false}, {"allow_modify", false}, {"allow_annotate", false}}})); work = r.value("path").toString(); pw = "secret"; auto i = req(Core::info(work, "secret")); if (!i.value("encrypted").toBool()) return QString("FAIL not encrypted"); auto bad = Core::info(work, "nope"); if (!bad.value("needs_password").toBool()) return QString("FAIL wrong pw accepted"); bool n = false; auto d = PdfDoc::open(work, "nope", &n); if (d || !n) return QString("FAIL pdfium accepted wrong pw"); d = PdfDoc::open(work, "secret", &n); return d ? QString("pdfium opened pages=%1").arg(d->pages()) : QString("FAIL pdfium open"); });
    step("remove label + decrypt", [&]() { QJsonObject extra; for (const QString& k : req(Core::info(work, pw)).value("metadata").toObject().keys()) if (k.startsWith("MSIP_")) extra[k] = ""; auto r = req(Core::apply(work, pw, {QJsonObject{{"op", "remove_marks"}}, QJsonObject{{"op", "metadata"}, {"classification", ""}, {"keywords", ""}, {"extra", extra}}, QJsonObject{{"op", "decrypt"}}})); work = r.value("path").toString(); pw = ""; auto i = req(Core::info(work)); return (!i.value("encrypted").toBool() && !i.value("has_marks").toBool() && i.value("classification").toString().isEmpty() && i.value("msip").isNull()) ? QString("clean") : QString("FAIL ") + QJsonDocument(i).toJson(QJsonDocument::Compact); });
    step("merge", [&]() { auto r = req(Core::merge(QJsonArray{QJsonObject{{"path", work}}, QJsonObject{{"path", fixture}, {"pages", QJsonArray{1}}}})); return r.value("pages").toInt() == 3 ? QString("pages=3") : QString("FAIL pages=%1").arg(r.value("pages").toInt()); });
    step("split each", [&]() { auto r = req(Core::split(fixture, "", "each", "", 1, outDir)); return QString("files=%1").arg(r.value("files").toArray().size()); });
    step("images->pdf", [&]() { QImage img(300, 200, QImage::Format_RGB32); img.fill(QColor("#4f8cff")); img.save(outDir + "/st.png"); auto r = req(Core::imagesToPdf({outDir + "/st.png"}, true)); return QString("pages=%1").arg(r.value("pages").toInt()); });
    step("save copy", [&]() { QFile::remove(outDir + "/selftest-final.pdf"); return QFile::copy(work, outDir + "/selftest-final.pdf") ? QString("ok") : QString("FAIL copy"); });
    fprintf(stdout, "RESULT %s (%d fail)\n", fails ? "FAIL" : "PASS", fails); return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc >= 4 && QString(argv[1]) == "--selftest") { QApplication app(argc, argv); for (const QString& f : {"NotoSansThai-Regular.ttf", "NotoSansThai-Bold.ttf"}) QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/../Resources/fonts/" + f); FPDF_LIBRARY_CONFIG cfg{}; cfg.version = 2; FPDF_InitLibraryWithConfig(&cfg); QDir().mkpath(argv[3]); const int rc = selfTest(QString::fromUtf8(argv[2]), QString::fromUtf8(argv[3])); FPDF_DestroyLibrary(); return rc; }
    QCoreApplication::setOrganizationName("oneable"); QCoreApplication::setOrganizationDomain("oneable.co.th"); QCoreApplication::setApplicationName("PDF DLP");
    App app(argc, argv);
    QString action; QStringList paths; parseArgs(app.arguments().mid(1), action, paths);
    // single instance: forward files to the running app
    const QString sock = "pdfdlp-" + QString::number(qHash(QDir::homePath()));
    { QLocalSocket s; s.connectToServer(sock); if (s.waitForConnected(300)) { s.write(QJsonDocument(QJsonObject{{"action", action}, {"paths", QJsonArray::fromStringList(paths)}}).toJson(QJsonDocument::Compact)); s.waitForBytesWritten(500); return 0; } }
    QLocalServer::removeServer(sock); QLocalServer server; server.listen(sock);

    for (const QString& f : {"fa-solid-900.otf", "fa-regular-400.otf", "fa-brands-400.otf", "NotoSansThai-Regular.ttf", "NotoSansThai-Medium.ttf", "NotoSansThai-SemiBold.ttf", "NotoSansThai-Bold.ttf"})
        QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/../Resources/fonts/" + f) >= 0 || QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/fonts/" + f);
    QFont base("Noto Sans Thai"); base.setPixelSize(13); app.setFont(base);
    { QFile f(QCoreApplication::applicationDirPath() + "/../Resources/theme.qss"); if (!f.exists()) f.setFileName(QCoreApplication::applicationDirPath() + "/theme.qss"); if (f.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(f.readAll())); }
    I18n::setLang(QSettings().value("lang", "th").toString());

    FPDF_LIBRARY_CONFIG cfg{}; cfg.version = 2; FPDF_InitLibraryWithConfig(&cfg);
    MainWindow win; app.win = &win;
    if (QSettings().contains("geometry")) win.restoreGeometry(QSettings().value("geometry").toByteArray());
    win.show();
    QObject::connect(&server, &QLocalServer::newConnection, &win, [&]() { auto* c = server.nextPendingConnection(); QObject::connect(c, &QLocalSocket::readyRead, &win, [c, &win]() { const QJsonObject o = QJsonDocument::fromJson(c->readAll()).object(); QStringList ps; for (const auto& v : o.value("paths").toArray()) ps << v.toString(); win.openPaths(ps, o.value("action").toString("open")); c->deleteLater(); }); });
    if (!paths.isEmpty()) QTimer::singleShot(0, &win, [&]() { win.openPaths(paths, action); });
    const int rc = app.exec();
    FPDF_DestroyLibrary();
    return rc;
}
