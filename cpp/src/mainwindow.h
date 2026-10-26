#pragma once
#include <QtWidgets>
#include "doc.h"
#include "viewer.h"
#include "sidebar.h"
#include "panels.h"
#include "home.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openPaths(const QStringList& paths, const QString& action = "open");
protected:
    void dragEnterEvent(QDragEnterEvent* e) override; void dropEvent(QDropEvent* e) override; void closeEvent(QCloseEvent* e) override;
private:
    Document* active() { return activeIdx >= 0 && activeIdx < int(docs.size()) ? &docs[activeIdx] : nullptr; }
    bool openOne(const QString& path);
    void setActive(int idx); void closeDoc(int idx); void refreshAll(); void refreshFiles(); void setBusy(const QString& text);
    void runOps(const QJsonArray& ops, const QString& nextPassword = QString(), bool keepPassword = true);
    void reloadActive(const QString& newWorkPath, const QString& password, bool modified);
    void openProduced(const QString& path, const QString& name, const QString& password = QString());
    void showHome(); void saveAs(); void undo(); void tool(const QString& key); void retranslate(); void showSettings(); void buildToolbox();
    QList<int> pagesOrCurrent();
    void applyLabel(); void removeLabel(); void batchLabel(); void printDoc(); void emailDoc();
    void toast(const QString& text, const QString& kind = "ok");

    std::vector<Document> docs; int activeIdx = -1;
    Viewer* viewer; Sidebar* sidebar; QToolBar* toolbox; QList<QAction*> toolActs; ClassifyPanel* classify; InfoPanel* infoPanel; QTabWidget* rightTabs; QDockWidget *leftDock = nullptr, *rightDock = nullptr;
    QToolBar* tb; QAction *homeAct, *openAct, *saveAct, *undoAct, *settingsAct, *zoomInAct, *zoomOutAct, *fitWAct, *fitPAct, *prevAct, *nextAct; QLabel* zoomLbl; QLineEdit* pageEdit; QLabel* pageTotal; QLabel* modifiedBadge; QLabel* busyLbl; QComboBox* langBox; HomeView* home; QStackedWidget* center;
    QProgressBar* progress; bool homeMode = true;
};
