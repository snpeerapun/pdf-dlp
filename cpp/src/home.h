#pragma once
#include <QtWidgets>
// Start screen: open/drop box + recent files as cards (thumbnail cached per file).
struct RecentEntry { QString path, name; int pages = 0; QDateTime openedAt; QString thumb; bool pinned = false; };
class HomeView : public QScrollArea {
    Q_OBJECT
public:
    explicit HomeView(QWidget* parent = nullptr);
    void refresh();
    void retranslate();
    static void remember(const QString& path, int pages, const QImage& firstPage);
    static QList<RecentEntry> recents();
    static void forget(const QString& path);
    static void setPinned(const QString& path, bool on);
signals:
    void openRequested(); void fileRequested(const QString& path);
protected:
    void resizeEvent(QResizeEvent* e) override;
private:
    void relayout();
    QWidget* body; QGridLayout* grid; QLabel *title, *hint, *recentTitle, *pinTitle; QGridLayout* pinGrid; QWidget* pinHost; QPushButton* openBtn; QList<RecentEntry> items; QWidget* dropBox; QWidget* cardsHost;
};
