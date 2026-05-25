#pragma once

#include "../core/TaskSession.h"

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include <memory>

namespace gisqc {

class ResultsPage : public QWidget {
    Q_OBJECT
public:
    explicit ResultsPage(QWidget* parent = nullptr);
    void showReport(const TaskSessionReport& report);
    void showEmptyState();

private slots:
    void exportIssueCsv();
    void exportHtmlReport();
    void exportExcelReport();
    void exportWordReport();
    void exportResultPackage();
    void openResultDirectory();
    void updateSelectedIssueDetail();
    void markSelectedIssuePending();
    void markSelectedIssueConfirmed();
    void markSelectedIssueFixed();
    void markSelectedIssueIgnored();

private:
    void setStatLabels(const ResultStatistics& statistics);
    void markSelectedIssueStatus(const QString& status);
    void setIssueWorkflowButtonsEnabled(bool enabled);
    static QString severityText(Severity severity);

    QLabel* totalRulesLabel_{};
    QLabel* passedRulesLabel_{};
    QLabel* warningCountLabel_{};
    QLabel* errorCountLabel_{};
    QLabel* passRateLabel_{};
    QLabel* detailLabel_{};
    QLabel* logLabel_{};
    QTableWidget* issueTable_{};
    QPushButton* exportHtmlButton_{};
    QPushButton* exportExcelButton_{};
    QPushButton* exportWordButton_{};
    QPushButton* exportCsvButton_{};
    QPushButton* exportPackageButton_{};
    QPushButton* openResultDirButton_{};
    QPushButton* markPendingButton_{};
    QPushButton* markConfirmedButton_{};
    QPushButton* markFixedButton_{};
    QPushButton* markIgnoredButton_{};
    std::unique_ptr<TaskSessionReport> currentReport_;
    QString lastExportDirectory_;
};

} // namespace gisqc
