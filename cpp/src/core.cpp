#include "core.h"
#include "pdfcore.h"
#include <QtConcurrent>

static QJsonObject parse(char* raw) {
    QJsonObject o;
    if (!raw) { o["ok"] = false; o["error"] = "core returned null"; return o; }
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(raw));
    pdfcore_free(raw);
    o = d.object();
    if (!o.contains("ok")) { o["ok"] = false; o["error"] = "bad core json"; }
    return o;
}
static QByteArray u8(const QString& s) { return s.toUtf8(); }
static QByteArray js(const QJsonValue& v) { return v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact) : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact); }

namespace Core {
QJsonObject info(const QString& path, const QString& password) { return parse(pdfcore_info(u8(path).constData(), u8(password).constData())); }
QJsonObject apply(const QString& path, const QString& password, const QJsonArray& ops, const QString& outPath) {
    const QByteArray o = js(ops); const QByteArray out = u8(outPath);
    return parse(pdfcore_apply(u8(path).constData(), u8(password).constData(), o.constData(), outPath.isEmpty() ? nullptr : out.constData()));
}
QJsonObject merge(const QJsonArray& inputs, const QString& outPath) { const QByteArray i = js(inputs); const QByteArray out = u8(outPath); return parse(pdfcore_merge(i.constData(), outPath.isEmpty() ? nullptr : out.constData())); }
QJsonObject split(const QString& path, const QString& password, const QString& mode, const QString& spec, unsigned every, const QString& outDir) {
    return parse(pdfcore_split(u8(path).constData(), u8(password).constData(), u8(mode).constData(), u8(spec).constData(), every, u8(outDir).constData()));
}
QJsonObject imagesToPdf(const QStringList& paths, bool fitA4) { const QByteArray p = js(QJsonArray::fromStringList(paths)); return parse(pdfcore_images_to_pdf(p.constData(), fitA4 ? 1 : 0, nullptr)); }
QJsonObject parseRanges(const QString& spec, unsigned total) { return parse(pdfcore_parse_ranges(u8(spec).constData(), total)); }
QJsonObject discoverLabels() { return parse(pdfcore_discover_labels()); }
QJsonObject labelsFromFile(const QString& path, const QString& password) { return parse(pdfcore_labels_from_file(u8(path).constData(), u8(password).constData())); }
QJsonObject msipMetadata(const QJsonObject& label, const QString& siteId, unsigned bits, const QString& owner, const QJsonObject& previous) {
    const QByteArray l = js(label), p = js(previous);
    return parse(pdfcore_msip_metadata(l.constData(), u8(siteId).constData(), bits, u8(owner).constData(), p.constData()));
}
QJsonObject prepareExport(const QString& path, const QString& password) { return parse(pdfcore_prepare_export(u8(path).constData(), u8(password).constData())); }
QJsonObject writeDocx(const QJsonObject& doc, const QString& outPath) { const QByteArray j = js(doc); return parse(pdfcore_write_docx(j.constData(), u8(outPath).constData())); }
QString newGuid() { return parse(pdfcore_new_guid()).value("guid").toString(); }
QJsonObject shell(const QString& action, const QJsonArray& items) { const QByteArray i = js(items); return parse(pdfcore_shell(u8(action).constData(), i.constData())); }

static QThreadPool& pool() { static QThreadPool p; p.setMaxThreadCount(1); return p; } // core ops are serialised

void async(std::function<QJsonObject()> fn, std::function<void(QJsonObject)> done) {
    auto future = QtConcurrent::run(&pool(), std::move(fn));
    auto* watcher = new QFutureWatcher<QJsonObject>(qApp);
    QObject::connect(watcher, &QFutureWatcher<QJsonObject>::finished, qApp, [watcher, done = std::move(done)]() { done(watcher->result()); watcher->deleteLater(); });
    watcher->setFuture(future);
}
}
