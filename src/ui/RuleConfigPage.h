#pragma once

#include "../core/RuleTemplateLoader.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTreeWidget>
#include <QWidget>

#include <map>

namespace gisqc {

class RuleConfigPage : public QWidget {
    Q_OBJECT
public:
    explicit RuleConfigPage(QWidget* parent = nullptr);
    void setDatasetPath(const QString& path);

private slots:
    void newProject();
    void deleteProject();
    void onProjectChanged();
    void newScheme();
    void saveScheme();
    void deleteScheme();
    void loadSelectedScheme();
    void resetToDefault();
    void addSelectedRules();
    void removeSelectedRules();
    void filterByCategory();
    void updateDetailPanel();
    void applyParamEdits();
    void browseDataSource();
    void reloadLayerNames();

private:
    void loadMasterRules();
    void loadScheme();
    void refreshAvailableTable();
    void refreshSchemeTable();
    void refreshProjectList();
    void refreshSchemeLibrary();
    void updateCategoryCounts();
    bool schemeContainsRule(const std::string& code) const;
    static QString categoryTextForCode(const std::string& code);
    static QString cleanRuleName(const std::string& code);
    static QString formatParameters(const std::map<std::string, std::string>& parameters);
    static std::map<std::string, std::string> parseParameters(const QString& text);
    static QString projectsBaseDir();
    QString currentProjectDir() const;

    std::vector<RuleDefinition> masterRules_;
    RuleTemplate scheme_;

    QComboBox* projectCombo_{};
    QComboBox* schemeCombo_{};
    QTreeWidget* categoryTree_{};
    QTableWidget* availableTable_{};
    QTableWidget* schemeTable_{};
    QLineEdit* toleranceEdit_{};
    QComboBox* dataSourceCombo_{};
    QStringList cachedLayerNames_;
    QLabel* detailLabel_{};
    QWidget* paramPanel_{};
    QFormLayout* paramForm_{};
    QLabel* schemeCountLabel_{};
};

} // namespace gisqc
