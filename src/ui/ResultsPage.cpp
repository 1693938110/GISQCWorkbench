#include "ResultsPage.h"

#include "../core/IssueCsvExporter.h"
#include "../core/ReportExporter.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <array>

namespace gisqc {

namespace {

QFrame* makeStatCard(const QString& title, QLabel** valueLabel, const QString& objectName, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("StatCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 12);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("StatTitle");
    layout->addWidget(titleLabel);

    *valueLabel = new QLabel("--", card);
    (*valueLabel)->setObjectName(objectName);
    layout->addWidget(*valueLabel);
    layout->addStretch();
    return card;
}

QFrame* makePanel(QWidget* parent) {
    auto* panel = new QFrame(parent);
    panel->setObjectName("Card");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    return panel;
}

QString safeFileBaseName(QString value) {
    if (value.trimmed().isEmpty()) {
        value = "GIS数据质检报告";
    }
    for (const QChar ch : std::array<QChar, 9>{'\\', '/', ':', '*', '?', '"', '<', '>', '|'}) {
        value.replace(ch, '_');
    }
    return value.trimmed();
}

bool writeUtf8TextFile(const QString& fileName, const QString& content, bool bom = false) {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (bom) {
        stream << QChar(0xFEFF);
    }
    stream << content;
    return true;
}

} // namespace

ResultsPage::ResultsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // ---- Band 1: Stat cards row ----
    auto* stats = new QHBoxLayout();
    stats->setSpacing(14);
    stats->addWidget(makeStatCard("总规则数", &totalRulesLabel_, "StatValue", this));
    stats->addWidget(makeStatCard("已通过", &passedRulesLabel_, "StatValueGreen", this));
    stats->addWidget(makeStatCard("警告", &warningCountLabel_, "StatValueOrange", this));
    stats->addWidget(makeStatCard("错误", &errorCountLabel_, "StatValueRed", this));
    stats->addWidget(makeStatCard("通过率", &passRateLabel_, "StatValue", this));
    layout->addLayout(stats);

    // ---- Band 2: Table + Detail in a single card ----
    auto* bodyCard = new QFrame(this);
    bodyCard->setObjectName("Card");
    auto* bodyCardLayout = new QHBoxLayout(bodyCard);
    bodyCardLayout->setContentsMargins(0, 0, 0, 0);
    bodyCardLayout->setSpacing(0);

    issueTable_ = new QTableWidget(0, 7, bodyCard);
    issueTable_->setHorizontalHeaderLabels({"问题编号", "规则编码", "图层", "要素ID", "问题描述", "严重级别", "状态"});
    issueTable_->horizontalHeader()->setStretchLastSection(true);
    issueTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    issueTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    issueTable_->verticalHeader()->setVisible(false);
    issueTable_->setAlternatingRowColors(true);
    issueTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    issueTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    issueTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    issueTable_->setStyleSheet("QTableWidget { border: none; border-radius: 0; border-right: 1px solid #dee7f1; }");
    connect(issueTable_, &QTableWidget::currentCellChanged, this, &ResultsPage::updateSelectedIssueDetail);
    bodyCardLayout->addWidget(issueTable_, 3);

    auto* detailPane = new QWidget(bodyCard);
    auto* detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(18, 16, 18, 16);
    detailLayout->setSpacing(10);
    auto* detailTitle = new QLabel("问题详情", detailPane);
    detailTitle->setObjectName("CardTitle");
    detailLayout->addWidget(detailTitle);
    detailLabel_ = new QLabel(detailPane);
    detailLabel_->setWordWrap(true);
    detailLabel_->setObjectName("SubtleText");
    detailLayout->addWidget(detailLabel_, 1);

    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(8);
    markPendingButton_ = new QPushButton("待处理", detailPane);
    connect(markPendingButton_, &QPushButton::clicked, this, &ResultsPage::markSelectedIssuePending);
    statusRow->addWidget(markPendingButton_);
    markConfirmedButton_ = new QPushButton("已确认", detailPane);
    connect(markConfirmedButton_, &QPushButton::clicked, this, &ResultsPage::markSelectedIssueConfirmed);
    statusRow->addWidget(markConfirmedButton_);
    markFixedButton_ = new QPushButton("已整改", detailPane);
    markFixedButton_->setObjectName("PrimaryButton");
    connect(markFixedButton_, &QPushButton::clicked, this, &ResultsPage::markSelectedIssueFixed);
    statusRow->addWidget(markFixedButton_);
    markIgnoredButton_ = new QPushButton("忽略", detailPane);
    connect(markIgnoredButton_, &QPushButton::clicked, this, &ResultsPage::markSelectedIssueIgnored);
    statusRow->addWidget(markIgnoredButton_);
    detailLayout->addLayout(statusRow);
    bodyCardLayout->addWidget(detailPane, 1);
    layout->addWidget(bodyCard, 1);

    // ---- Band 3: Log section ----
    auto* logCard = new QFrame(this);
    logCard->setObjectName("Card");
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(18, 14, 18, 14);
    logLayout->setSpacing(8);
    auto* logTitle = new QLabel("执行日志", logCard);
    logTitle->setObjectName("CardTitle");
    logLayout->addWidget(logTitle);
    logLabel_ = new QLabel("暂无执行日志。质检完成后日志将显示在此。", logCard);
    logLabel_->setObjectName("SubtleText");
    logLabel_->setWordWrap(true);
    logLayout->addWidget(logLabel_);
    logCard->setMaximumHeight(120);
    layout->addWidget(logCard);

    // ---- Band 4: Export buttons ----
    auto* exports = new QHBoxLayout();
    exports->setSpacing(12);
    exportExcelButton_ = new QPushButton("导出 Excel 报告", this);
    exportExcelButton_->setObjectName("PrimaryButton");
    exportExcelButton_->setToolTip("导出 Excel 兼容格式的质检报告");
    connect(exportExcelButton_, &QPushButton::clicked, this, &ResultsPage::exportExcelReport);
    exports->addWidget(exportExcelButton_);
    exportWordButton_ = new QPushButton("导出 Word 报告", this);
    exportWordButton_->setToolTip("导出 Word 兼容格式的质检报告");
    connect(exportWordButton_, &QPushButton::clicked, this, &ResultsPage::exportWordReport);
    exports->addWidget(exportWordButton_);
    exportCsvButton_ = new QPushButton("导出问题图层", this);
    exportCsvButton_->setToolTip("导出问题清单为 CSV 文件");
    connect(exportCsvButton_, &QPushButton::clicked, this, &ResultsPage::exportIssueCsv);
    exports->addWidget(exportCsvButton_);
    openResultDirButton_ = new QPushButton("打开结果目录", this);
    openResultDirButton_->setToolTip("在资源管理器中打开最近一次导出目录");
    connect(openResultDirButton_, &QPushButton::clicked, this, &ResultsPage::openResultDirectory);
    exports->addWidget(openResultDirButton_);
    exportPackageButton_ = new QPushButton("一键导出全部", this);
    exportPackageButton_->setObjectName("PrimaryButton");
    exportPackageButton_->setToolTip("一键导出 CSV + Excel + Word + HTML 结果包到指定目录");
    connect(exportPackageButton_, &QPushButton::clicked, this, &ResultsPage::exportResultPackage);
    exports->addWidget(exportPackageButton_);
    exports->addStretch();
    exportHtmlButton_ = new QPushButton("导出 HTML", this);
    exportHtmlButton_->hide();
    connect(exportHtmlButton_, &QPushButton::clicked, this, &ResultsPage::exportHtmlReport);

    layout->addLayout(exports);

    showEmptyState();
}

void ResultsPage::showReport(const TaskSessionReport& report) {
    currentReport_ = std::make_unique<TaskSessionReport>(report);
    if (exportCsvButton_) {
        exportCsvButton_->setEnabled(true);
    }
    if (exportHtmlButton_) exportHtmlButton_->setEnabled(true);
    if (exportExcelButton_) exportExcelButton_->setEnabled(true);
    if (exportWordButton_) exportWordButton_->setEnabled(true);
    if (exportPackageButton_) exportPackageButton_->setEnabled(true);
    if (openResultDirButton_) openResultDirButton_->setEnabled(!lastExportDirectory_.isEmpty());
    setIssueWorkflowButtonsEnabled(!report.issues.empty());
    setStatLabels(report.statistics);
    issueTable_->setRowCount(static_cast<int>(report.issues.size()));
    for (int r = 0; r < static_cast<int>(report.issues.size()); ++r) {
        const auto& issue = report.issues[static_cast<std::size_t>(r)];
        issueTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(issue.issueId)));
        issueTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(issue.ruleCode)));
        issueTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(issue.layerName)));
        issueTable_->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(issue.featureId)));
        issueTable_->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(issue.description)));
        issueTable_->setItem(r, 5, new QTableWidgetItem(severityText(issue.severity)));
        issueTable_->setItem(r, 6, new QTableWidgetItem(QString::fromStdString(issue.status)));
    }

    if (issueTable_->rowCount() > 0) {
        issueTable_->setCurrentCell(0, 0);
    } else {
        updateSelectedIssueDetail();
    }

    if (logLabel_ && !report.logs.empty()) {
        QStringList logLines;
        for (const auto& log : report.logs) {
            logLines << QString::fromStdString(log);
        }
        logLabel_->setText(logLines.join("\n"));
    }
}

void ResultsPage::showEmptyState() {
    currentReport_.reset();
    if (exportCsvButton_) {
        exportCsvButton_->setEnabled(false);
    }
    if (exportHtmlButton_) exportHtmlButton_->setEnabled(false);
    if (exportExcelButton_) exportExcelButton_->setEnabled(false);
    if (exportWordButton_) exportWordButton_->setEnabled(false);
    if (exportPackageButton_) exportPackageButton_->setEnabled(false);
    if (openResultDirButton_) openResultDirButton_->setEnabled(false);
    setIssueWorkflowButtonsEnabled(false);
    ResultStatistics empty;
    setStatLabels(empty);
    if (issueTable_) {
        issueTable_->setRowCount(0);
    }
    if (detailLabel_) {
        detailLabel_->setText("暂无质检结果。请先在数据导入或执行质检页面选择成果目录并运行检查。");
    }
}

void ResultsPage::setStatLabels(const ResultStatistics& statistics) {
    totalRulesLabel_->setText(QString::number(statistics.totalRules));
    passedRulesLabel_->setText(QString::number(statistics.passedRules));
    warningCountLabel_->setText(QString::number(statistics.warningCount));
    errorCountLabel_->setText(QString::number(statistics.errorCount));
    passRateLabel_->setText(QString::fromStdString(statistics.passRateText));
}

void ResultsPage::exportIssueCsv() {
    if (!currentReport_) {
        QMessageBox::information(this, "暂无结果", "请先执行一次质检，再导出问题清单 CSV。");
        return;
    }

    const QString defaultName = QString::fromStdString(currentReport_->task.name()) + "_问题清单.csv";
    const QString fileName = QFileDialog::getSaveFileName(this, "导出问题清单 CSV", defaultName, "CSV 文件 (*.csv)");
    if (fileName.isEmpty()) {
        return;
    }

    const std::string csv = IssueCsvExporter::toCsv(currentReport_->issues);
    if (!writeUtf8TextFile(fileName, QString::fromStdString(csv), true)) {
        QMessageBox::critical(this, "导出失败", "无法写入文件：" + fileName);
        return;
    }
    lastExportDirectory_ = QFileInfo(fileName).absolutePath();
    if (openResultDirButton_) openResultDirButton_->setEnabled(true);
    QMessageBox::information(this, "导出完成", "问题清单 CSV 已保存：" + fileName);
}

void ResultsPage::exportHtmlReport() {
    if (!currentReport_) {
        QMessageBox::information(this, "暂无结果", "请先执行一次质检，再导出 HTML 报告。");
        return;
    }

    const QString defaultName = QString::fromStdString(ReportExporter::defaultReportBaseName(*currentReport_)) + "_质检报告.html";
    const QString fileName = QFileDialog::getSaveFileName(this, "导出 HTML 质检报告", defaultName, "HTML 文件 (*.html)");
    if (fileName.isEmpty()) {
        return;
    }

    if (!writeUtf8TextFile(fileName, QString::fromStdString(ReportExporter::toHtmlReport(*currentReport_)))) {
        QMessageBox::critical(this, "导出失败", "无法写入文件：" + fileName);
        return;
    }
    lastExportDirectory_ = QFileInfo(fileName).absolutePath();
    if (openResultDirButton_) openResultDirButton_->setEnabled(true);
    QMessageBox::information(this, "导出完成", "HTML 质检报告已保存：" + fileName);
}

void ResultsPage::exportExcelReport() {
    if (!currentReport_) {
        QMessageBox::information(this, "暂无结果", "请先执行一次质检，再导出 Excel 报告。");
        return;
    }

    const QString defaultName = QString::fromStdString(ReportExporter::defaultReportBaseName(*currentReport_)) + "_质检报告.xls";
    const QString fileName = QFileDialog::getSaveFileName(this, "导出 Excel 质检报告", defaultName, "Excel 兼容文件 (*.xls)");
    if (fileName.isEmpty()) {
        return;
    }

    if (!writeUtf8TextFile(fileName, QString::fromStdString(ReportExporter::toExcelHtmlReport(*currentReport_)))) {
        QMessageBox::critical(this, "导出失败", "无法写入文件：" + fileName);
        return;
    }
    lastExportDirectory_ = QFileInfo(fileName).absolutePath();
    if (openResultDirButton_) openResultDirButton_->setEnabled(true);
    QMessageBox::information(this, "导出完成", "Excel 兼容质检报告已保存：" + fileName);
}

void ResultsPage::exportWordReport() {
    if (!currentReport_) {
        QMessageBox::information(this, "暂无结果", "请先执行一次质检，再导出 Word 报告。");
        return;
    }

    const QString defaultName = QString::fromStdString(ReportExporter::defaultReportBaseName(*currentReport_)) + "_质检报告.doc";
    const QString fileName = QFileDialog::getSaveFileName(this, "导出 Word 质检报告", defaultName, "Word 兼容文件 (*.doc)");
    if (fileName.isEmpty()) {
        return;
    }

    if (!writeUtf8TextFile(fileName, QString::fromStdString(ReportExporter::toWordHtmlReport(*currentReport_)))) {
        QMessageBox::critical(this, "导出失败", "无法写入文件：" + fileName);
        return;
    }
    lastExportDirectory_ = QFileInfo(fileName).absolutePath();
    if (openResultDirButton_) openResultDirButton_->setEnabled(true);
    QMessageBox::information(this, "导出完成", "Word 兼容质检报告已保存：" + fileName);
}

void ResultsPage::exportResultPackage() {
    if (!currentReport_) {
        QMessageBox::information(this, "暂无结果", "请先执行一次质检，再导出结果包。");
        return;
    }

    const QString rootDir = QFileDialog::getExistingDirectory(this, "选择结果包保存目录", lastExportDirectory_);
    if (rootDir.isEmpty()) {
        return;
    }

    const QString base = safeFileBaseName(QString::fromStdString(ReportExporter::defaultReportBaseName(*currentReport_)));
    QDir root(rootDir);
    const QString packageName = base + "_结果包";
    if (!root.exists(packageName) && !root.mkdir(packageName)) {
        QMessageBox::critical(this, "导出失败", "无法创建结果包目录：" + root.filePath(packageName));
        return;
    }
    QDir package(root.filePath(packageName));

    const bool ok =
        writeUtf8TextFile(package.filePath(base + "_问题清单.csv"), QString::fromStdString(IssueCsvExporter::toCsv(currentReport_->issues)), true) &&
        writeUtf8TextFile(package.filePath(base + "_质检报告.html"), QString::fromStdString(ReportExporter::toHtmlReport(*currentReport_))) &&
        writeUtf8TextFile(package.filePath(base + "_质检报告.xls"), QString::fromStdString(ReportExporter::toExcelHtmlReport(*currentReport_))) &&
        writeUtf8TextFile(package.filePath(base + "_质检报告.doc"), QString::fromStdString(ReportExporter::toWordHtmlReport(*currentReport_)));

    if (!ok) {
        QMessageBox::critical(this, "导出失败", "结果包文件写入失败，请检查目录权限。");
        return;
    }

    lastExportDirectory_ = package.absolutePath();
    if (openResultDirButton_) openResultDirButton_->setEnabled(true);
    QMessageBox::information(this, "导出完成", "结果包已生成：" + package.absolutePath());
}

void ResultsPage::openResultDirectory() {
    if (lastExportDirectory_.isEmpty()) {
        QMessageBox::information(this, "暂无结果目录", "请先导出报告或问题清单。");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(lastExportDirectory_));
}

void ResultsPage::updateSelectedIssueDetail() {
    if (!detailLabel_) {
        return;
    }
    if (!currentReport_ || !issueTable_ || issueTable_->currentRow() < 0 ||
        issueTable_->currentRow() >= static_cast<int>(currentReport_->issues.size())) {
        detailLabel_->setText("暂无选中的问题。");
        setIssueWorkflowButtonsEnabled(false);
        return;
    }

    const auto& issue = currentReport_->issues[static_cast<std::size_t>(issueTable_->currentRow())];
    detailLabel_->setText(QString("问题编号：%1\n规则编码：%2\n图层/对象：%3\n要素/文件：%4\n严重级别：%5\n当前状态：%6\n\n问题描述：\n%7\n\n处理建议：错误级问题优先返修；已复核不需整改的问题可标记为忽略，导出报告会保留当前状态。")
        .arg(QString::fromStdString(issue.issueId),
             QString::fromStdString(issue.ruleCode),
             QString::fromStdString(issue.layerName),
             QString::fromStdString(issue.featureId),
             severityText(issue.severity),
             QString::fromStdString(issue.status),
             QString::fromStdString(issue.description)));
    setIssueWorkflowButtonsEnabled(true);
}

void ResultsPage::markSelectedIssuePending() {
    markSelectedIssueStatus("待处理");
}

void ResultsPage::markSelectedIssueConfirmed() {
    markSelectedIssueStatus("已确认");
}

void ResultsPage::markSelectedIssueFixed() {
    markSelectedIssueStatus("已整改");
}

void ResultsPage::markSelectedIssueIgnored() {
    markSelectedIssueStatus("忽略");
}

void ResultsPage::markSelectedIssueStatus(const QString& status) {
    if (!currentReport_ || !issueTable_) {
        return;
    }
    const int row = issueTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(currentReport_->issues.size())) {
        return;
    }
    currentReport_->issues[static_cast<std::size_t>(row)].status = status.toStdString();
    if (!issueTable_->item(row, 6)) {
        issueTable_->setItem(row, 6, new QTableWidgetItem(status));
    } else {
        issueTable_->item(row, 6)->setText(status);
    }
    updateSelectedIssueDetail();
}

void ResultsPage::setIssueWorkflowButtonsEnabled(bool enabled) {
    if (markPendingButton_) markPendingButton_->setEnabled(enabled);
    if (markConfirmedButton_) markConfirmedButton_->setEnabled(enabled);
    if (markFixedButton_) markFixedButton_->setEnabled(enabled);
    if (markIgnoredButton_) markIgnoredButton_->setEnabled(enabled);
}

QString ResultsPage::severityText(Severity severity) {
    switch (severity) {
    case Severity::Warning:
        return "警告";
    case Severity::Error:
        return "错误";
    default:
        return "提示";
    }
}

} // namespace gisqc
