#pragma once

#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QWidget>

namespace gisqc {

struct DatasetScanSummary;

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(QWidget* parent = nullptr);
    void setDatasetPath(const QString& path);

signals:
    void logMessage(const QString& message);
    void datasetPathChanged(const QString& path);
    void requestRuleConfig();
    void requestExecution();

private slots:
    void chooseDatasetPath();
    void scanCurrentPath();

private:
    void appendLog(const QString& message);
    void renderScanSummary(const DatasetScanSummary& summary);
    void loadHistory();
    void saveHistory();

    QComboBox* pathCombo_{};
    QLabel* summaryLabel_{};
    QTableWidget* layerTable_{};
    QTextEdit* localLog_{};
    QProgressBar* progress_{};
    QPushButton* configureButton_{};
    QPushButton* runButton_{};
};

} // namespace gisqc
