#pragma once

#include "../core/RuleTemplateLoader.h"

#include <QLabel>
#include <QTableWidget>
#include <QWidget>

namespace gisqc {

class TemplateManagePage : public QWidget {
    Q_OBJECT
public:
    explicit TemplateManagePage(QWidget* parent = nullptr);

private slots:
    void reloadTemplate();
    void resetToDefault();
    void exportTemplate();
    void importTemplate();

private:
    void renderTemplateInfo();
    void renderRuleSummaryTable();

    RuleTemplate templ_;
    QLabel* templateInfoLabel_{};
    QLabel* sourceLabel_{};
    QTableWidget* summaryTable_{};
};

} // namespace gisqc
