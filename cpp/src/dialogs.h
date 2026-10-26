#pragma once
#include <QtWidgets>
#include "doc.h"
// Tool dialogs. Each returns the ops (QJsonArray) to apply, or performs its own action. Close = window ✕ only.
namespace Dialogs {
struct EncryptResult { bool ok = false; QJsonArray ops; QString nextPassword; };
EncryptResult encrypt(QWidget* parent);
bool decrypt(QWidget* parent, const Document& d, QJsonArray& ops);
bool split(QWidget* parent, const Document& d, QString& mode, QString& spec, unsigned& every, QString& outDir);
bool merge(QWidget* parent, QStringList& paths, const QStringList& openNames, const QStringList& openPaths);
bool insertPages(QWidget* parent, const Document& d, int currentPage, QString& file, int& afterPage);
bool watermark(QWidget* parent, const QList<int>& selected, QJsonArray& ops);
bool pageNumbers(QWidget* parent, const Document& d, QJsonArray& ops);
bool crop(QWidget* parent, const QList<int>& selected, QJsonArray& ops);
bool exportImages(QWidget* parent, const Document& d, const QList<int>& selected, QString& dir, QString& fmt, int& dpi, QList<int>& pages);
bool imagesToPdf(QWidget* parent, QStringList& files, bool& fitA4);
bool compress(QWidget* parent, const Document& d, QJsonArray& ops);
bool sanitize(QWidget* parent, QJsonArray& ops);
bool properties(QWidget* parent, const Document& d, QJsonArray& ops);
bool redact(QWidget* parent, const Document& d, int page, QJsonArray& ops);
QString password(QWidget* parent, const QString& fileName, bool wrong);
bool addLabel(QWidget* parent, QJsonObject& label);
QDialog* make(QWidget* parent, const QString& title, int width = 520);
QPushButton* primary(const QString& text);
}
