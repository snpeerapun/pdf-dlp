#pragma once
// Bridge to the Rust core (C ABI, JSON in/out). Calls are synchronous; run them off the UI thread via Core::async().
#include <QtCore>
#include <functional>

namespace Core {
QJsonObject info(const QString& path, const QString& password = {});
QJsonObject apply(const QString& path, const QString& password, const QJsonArray& ops, const QString& outPath = {});
QJsonObject merge(const QJsonArray& inputs, const QString& outPath = {});
QJsonObject split(const QString& path, const QString& password, const QString& mode, const QString& spec, unsigned every, const QString& outDir);
QJsonObject imagesToPdf(const QStringList& paths, bool fitA4);
QJsonObject parseRanges(const QString& spec, unsigned total);
QJsonObject discoverLabels();
QJsonObject labelsFromFile(const QString& path, const QString& password = {});
QJsonObject msipMetadata(const QJsonObject& label, const QString& siteId, unsigned bits, const QString& owner, const QJsonObject& previous);
QString newGuid();
QJsonObject shell(const QString& action, const QJsonArray& items = {});
/// Run `fn` on a worker thread; `done` is invoked on the UI thread.
void async(std::function<QJsonObject()> fn, std::function<void(QJsonObject)> done);
}
