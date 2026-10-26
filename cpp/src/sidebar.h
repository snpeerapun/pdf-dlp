#pragma once
#include <QtWidgets>
#include "doc.h"
// Left dock: file list + page thumbnails (multi-select, right-click menu).
class Sidebar : public QTabWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);
    void setFiles(const QStringList& names, const QList<bool>& modified, int active);
    void setDocument(std::shared_ptr<PdfDoc> d, const QString& key);
    void setCurrentPage(int oneBased);
    QList<int> selectedPages() const; // 1-based, sorted
    void clearSelection() { thumbs->clearSelection(); }
    void selectAll() { thumbs->selectAll(); }
    void retranslate();
signals:
    void fileActivated(int index); void fileClosed(int index); void fileContextMenu(int index, QPoint globalPos);
    void pageActivated(int oneBased); void pageContextMenu(QList<int> pages, QPoint globalPos); void selectionChanged();
private:
    QListWidget* files; QListWidget* thumbs; QLabel* selLabel; QPushButton* selAllBtn; QPushButton* selClearBtn; QWidget* pagesTab;
    std::shared_ptr<PdfDoc> doc; QString docKey; int gen = 0;
};
