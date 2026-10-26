#pragma once
#include <QtWidgets>
#include "doc.h"
// Right dock: Tools grid · Classification · File info
class ToolsPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget* parent = nullptr);
    void setHasDoc(bool has);
    void retranslate();
signals:
    void tool(const QString& key);
private:
    struct Btn { QPushButton* b; const char* key; const char* label; ushort icon; const char* color; };
    QList<Btn> btns;
};

class ClassifyPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ClassifyPanel(QWidget* parent = nullptr);
    void setDocument(const Document* d);
    void retranslate();
    QJsonArray labels() const { return m_labels; }
    QJsonObject selectedLabel() const;
    /// Build ops for applying `label` with current marking prefs. encryptPassword used only when "encrypt" checked.
    QJsonArray buildOps(const QJsonObject& label, const QJsonObject& previousMeta, QString* nextPassword) const;
    QJsonArray buildRemoveOps(const QJsonObject& previousMeta) const;
signals:
    void applyRequested(); void removeRequested(); void batchRequested();
private:
    void load(); void save(); void refreshList(); void mergeLabels(const QJsonArray& add);
    QJsonArray m_labels; QListWidget* list; QLabel* current; QLabel* msipBadge; QLabel* marksBadge; QPushButton* removeBtn; QPushButton* applyBtn; QPushButton* batchBtn;
    QCheckBox *cbHeader, *cbFooter, *cbWatermark, *cbMsip, *cbEncrypt; QLineEdit* siteId; QLineEdit* encPw; QLabel* notes; QPushButton *readWinBtn, *learnBtn, *addBtn;
    QGroupBox *gCur, *gChoose, *gMark;
};

class InfoPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget* parent = nullptr);
    void setDocument(const Document* d);
private:
    QLabel* content;
};
