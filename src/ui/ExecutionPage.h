#pragma once

#include "../core/TaskSession.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QWidget>

namespace gisqc {

class QualityCheckWorker;

class ExecutionPage : public QWidget {
    Q_OBJECT
public:
    explicit ExecutionPage(QWidget* parent = nullptr);
    ~ExecutionPage() override;
    void setDatasetPath(const QString& path);

signals:
    void logMessage(const QString& message);
    void reportReady(const TaskSessionReport& report);

private slots:
    void chooseDatasetPath();
    void onSchemeChanged(int index);
    void runQualityCheck();
    void cancelQualityCheck();
    void onWorkerProgress(int percent, const QString& message);
    void onWorkerFinished(const TaskSessionReport& report);
    void onWorkerError(const QString& errorMessage);

private:
    void appendLog(const QString& message);
    void renderReportSummary(const TaskSessionReport& report);
    void setRunning(bool running);
    std::string defaultRulesPath() const;
    void refreshSchemeList();
    void refreshProjectList();
    static QString projectsBaseDir();

    QComboBox* projectCombo_{};
    QComboBox* schemeCombo_{};
    QLineEdit* taskNameEdit_{};
    QLabel* pathLabel_{};
    QLabel* templateLabel_{};
    QLabel* summaryLabel_{};
    QTableWidget* issuePreviewTable_{};
    QTextEdit* localLog_{};
    QProgressBar* progress_{};
    QPushButton* runButton_{};
    QPushButton* cancelButton_{};
    QThread* workerThread_{};
    QualityCheckWorker* worker_{};
};

} // namespace gisqc
