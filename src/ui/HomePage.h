#pragma once

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace gisqc {

class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget* parent = nullptr);

signals:
    void requestNewTask();
    void requestRuleConfig();
    void requestResults();
    void requestTemplateManage();

private:
    void renderStats();
    void renderRecentTasks();

    QTableWidget* recentTable_{};
    QLabel* taskCountLabel_{};
    QLabel* issueCountLabel_{};
    QLabel* passRateLabel_{};
};

} // namespace gisqc
