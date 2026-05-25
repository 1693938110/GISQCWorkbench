#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>

namespace gisqc {

struct TaskSessionReport;
class HomePage;
class DashboardPage;
class ExecutionPage;
class ResultsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupMenuBar();
    QWidget* createSidebar();
    QWidget* createHeader();
    QWidget* createLogDock();
    void switchPage(int index);
    void navigateToPage(int index);
    void updateHeaderForPage(int index);
    void rememberDatasetPath(const QString& path);
    void openRuleConfigFromDashboard();
    void openExecutionFromDashboard();
    void appendGlobalLog(const QString& message);
    void handleTaskReport(const TaskSessionReport& report);
    void toggleLogDock();

    QListWidget* navList_{};
    QStackedWidget* pages_{};
    HomePage* homePage_{};
    ExecutionPage* executionPage_{};
    ResultsPage* resultsPage_{};
    QLabel* pageTitle_{};
    QLabel* pageSubtitle_{};
    QString currentDatasetPath_;
    QTextEdit* logView_{};
    QDockWidget* logDock_{};
    QLabel* statusLabel_{};
};

} // namespace gisqc
