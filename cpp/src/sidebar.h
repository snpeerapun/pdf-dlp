#pragma once
#include <QtWidgets>
#include "doc.h"
// Left dock: file list + page thumbnails (multi-select, right-click menu).
class Sidebar : public QTabWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);
    void setFiles(const QStringList& names, const QStringList& paths, const QList<bool>& modified, const QList<bool>& checked, int active);
    QList<int> checkedFiles() const;
    void setDocument(std::shared_ptr<PdfDoc> d, const QString& key);
    void setCurrentPage(int oneBased);
    QList<int> selectedPages() const; // 1-based, sorted
    void clearSelection() { thumbs->clearSelection(); }
    void selectAll() { thumbs->selectAll(); }
    void retranslate();
signals:
    void fileActivated(int index); void fileClosed(int index); void fileContextMenu(int index, QPoint globalPos); void fileChecked(int index, bool on);
    void pageActivated(int oneBased); void pageContextMenu(QList<int> pages, QPoint globalPos); void selectionChanged();
private:
    QListWidget* files; QListWidget* thumbs; QCheckBox* allFiles; QLabel* filesLabel; QLabel* selLabel; QPushButton* selAllBtn; QPushButton* selClearBtn; QWidget* pagesTab;
    std::shared_ptr<PdfDoc> doc; QString docKey; int gen = 0;
};
