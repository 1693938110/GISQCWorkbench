#include "ExecutionPage.h"
#include "QualityCheckWorker.h"

#include "../core/RuleTemplateLoader.h"
#include "../core/RuleTemplateStore.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

namespace gisqc {

namespace {

QFrame* makePanel(QWidget* parent = nullptr) {
    auto* panel = new QFrame(parent);
    panel->setObjectName("Card");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    return panel;
}

QLabel* titleLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("CardTitle");
    return label;
}

QString severityLabel(Severity severity) {
    switch (severity) {
    case Severity::Warning:
        return "警告";
    case Severity::Error:
        return "错误";
    default:
        return "提示";
    }
}

} // namespace

ExecutionPage::ExecutionPage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // ---- Band 1: Setup card ----
    auto* setupCard = makePanel(this);
    auto* setupLayout = qobject_cast<QVBoxLayout*>(setupCard->layout());

    auto* taskRow = new QHBoxLayout();
    taskRow->setSpacing(10);
    taskRow->addWidget(new QLabel("任务名称", setupCard));
    taskNameEdit_ = new QLineEdit("标准化成果质检任务", setupCard);
    taskRow->addWidget(taskNameEdit_, 1);
    taskRow->addWidget(new QLabel("质检方案", setupCard));
    schemeCombo_ = new QComboBox(setupCard);
    schemeCombo_->setMinimumWidth(180);
    connect(schemeCombo_, QOverload<int>::of(&QComboBox::activated), this, &ExecutionPage::onSchemeChanged);
    taskRow->addWidget(schemeCombo_, 1);
    templateLabel_ = new QLabel("", setupCard);
    templateLabel_->setObjectName("SubtleText");
    taskRow->addWidget(templateLabel_);
    setupLayout->addLayout(taskRow);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(10);
    pathEdit_ = new QLineEdit(setupCard);
    pathEdit_->setPlaceholderText("选择包含 FileGDB / Shapefile / GeoPackage 的成果目录");
    pathRow->addWidget(pathEdit_, 1);
    auto* browseButton = new QPushButton("浏览...", setupCard);
    browseButton->setToolTip("选择包含 GIS 数据的成果目录");
    connect(browseButton, &QPushButton::clicked, this, &ExecutionPage::chooseDatasetPath);
    pathRow->addWidget(browseButton);
    runButton_ = new QPushButton("开始执行质检", setupCard);
    runButton_->setObjectName("PrimaryButton");
    runButton_->setToolTip("加载内部规则配置，对成果目录执行完整质检");
    connect(runButton_, &QPushButton::clicked, this, &ExecutionPage::runQualityCheck);
    pathRow->addWidget(runButton_);
    cancelButton_ = new QPushButton("取消", setupCard);
    cancelButton_->setToolTip("取消正在执行的质检任务");
    cancelButton_->setEnabled(false);
    connect(cancelButton_, &QPushButton::clicked, this, &ExecutionPage::cancelQualityCheck);
    pathRow->addWidget(cancelButton_);
    setupLayout->addLayout(pathRow);
    layout->addWidget(setupCard);

    // ---- Band 2: Summary + Issue preview in one card ----
    auto* bodyCard = new QFrame(this);
    bodyCard->setObjectName("Card");
    auto* bodyCardLayout = new QHBoxLayout(bodyCard);
    bodyCardLayout->setContentsMargins(0, 0, 0, 0);
    bodyCardLayout->setSpacing(0);

    auto* summaryPane = new QWidget(bodyCard);
    auto* summaryVBox = new QVBoxLayout(summaryPane);
    summaryVBox->setContentsMargins(18, 16, 18, 16);
    summaryVBox->setSpacing(8);
    summaryVBox->addWidget(titleLabel("执行摘要", summaryPane));
    summaryLabel_ = new QLabel(QString::fromUtf8("尚未执行。选择成果目录后点击【开始执行质检】。"), summaryPane);
    summaryLabel_->setObjectName("SubtleText");
    summaryLabel_->setWordWrap(true);
    summaryVBox->addWidget(summaryLabel_, 1);
    bodyCardLayout->addWidget(summaryPane, 1);

    issuePreviewTable_ = new QTableWidget(0, 5, bodyCard);
    issuePreviewTable_->setHorizontalHeaderLabels({"问题编号", "规则", "对象", "描述", "级别"});
    issuePreviewTable_->horizontalHeader()->setStretchLastSection(true);
    issuePreviewTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    issuePreviewTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    issuePreviewTable_->verticalHeader()->setVisible(false);
    issuePreviewTable_->setAlternatingRowColors(true);
    issuePreviewTable_->setStyleSheet("QTableWidget { border: none; border-radius: 0; border-left: 1px solid #dee7f1; }");
    bodyCardLayout->addWidget(issuePreviewTable_, 2);
    layout->addWidget(bodyCard, 1);

    // ---- Band 3: Log ----
    auto* logCard = makePanel(this);
    auto* logLayout = qobject_cast<QVBoxLayout*>(logCard->layout());
    logLayout->addWidget(titleLabel("执行日志", logCard));
    localLog_ = new QTextEdit(logCard);
    localLog_->setReadOnly(true);
    localLog_->setMaximumHeight(90);
    localLog_->setObjectName("LogView");
    logLayout->addWidget(localLog_);
    progress_ = new QProgressBar(logCard);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setFormat("质检进度 %p%");
    logLayout->addWidget(progress_);
    layout->addWidget(logCard);

    refreshSchemeList();
    appendLog("就绪，选择质检方案后点击【开始执行质检】。");
}

void ExecutionPage::setDatasetPath(const QString& path) {
    if (!path.isEmpty() && pathEdit_) {
        pathEdit_->setText(path);
        appendLog("已接收数据导入页选择的成果目录：" + path);
    }
}

void ExecutionPage::chooseDatasetPath() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择成果目录", pathEdit_->text());
    if (!dir.isEmpty()) {
        pathEdit_->setText(dir);
        appendLog("已选择成果目录：" + dir);
    }
}

void ExecutionPage::runQualityCheck() {
    const QString inputPath = pathEdit_->text().trimmed();
    if (inputPath.isEmpty()) {
        QMessageBox::warning(this, "缺少成果目录", "请先选择需要质检的成果目录。");
        return;
    }

    if (workerThread_ && workerThread_->isRunning()) {
        QMessageBox::information(this, "任务进行中", "质检任务正在执行，请等待完成或取消当前任务。");
        return;
    }

    try {
        RuleTemplate templ;
        const int idx = schemeCombo_ ? schemeCombo_->currentIndex() : -1;
        const QString schemePath = (idx >= 0) ? schemeCombo_->itemData(idx).toString() : "";
        if (!schemePath.isEmpty() && QFile::exists(schemePath)) {
            RuleTemplateLoader loader;
            templ = loader.loadFromFile(schemePath.toStdString());
            appendLog("加载方案：" + schemeCombo_->currentText() + " (" + schemePath + ")");
        } else {
            RuleTemplateStore store;
            templ = store.loadActive();
            appendLog("加载内部规则配置：" + QString::fromStdString(store.activeTemplatePath()));
        }

        setRunning(true);
        progress_->setValue(5);
        appendLog("正在启动异步质检任务...");

        workerThread_ = new QThread(this);
        worker_ = new QualityCheckWorker();
        worker_->setTaskName(taskNameEdit_->text().toStdString());
        worker_->setInputPath(inputPath.toStdString());
        worker_->setRules(templ.rules);
        worker_->setGlobalTolerance(templ.globalTolerance);
        worker_->moveToThread(workerThread_);

        connect(workerThread_, &QThread::started, worker_, &QualityCheckWorker::process);
        connect(worker_, &QualityCheckWorker::progressChanged, this, &ExecutionPage::onWorkerProgress);
        connect(worker_, &QualityCheckWorker::finished, this, &ExecutionPage::onWorkerFinished);
        connect(worker_, &QualityCheckWorker::errorOccurred, this, &ExecutionPage::onWorkerError);
        connect(worker_, &QualityCheckWorker::finished, workerThread_, &QThread::quit);
        connect(worker_, &QualityCheckWorker::errorOccurred, workerThread_, &QThread::quit);
        connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished, this, [this]() {
            worker_ = nullptr;
            workerThread_ = nullptr;
        });

        workerThread_->start();
    } catch (const std::exception& ex) {
        setRunning(false);
        progress_->setValue(0);
        appendLog("执行失败：" + QString::fromUtf8(ex.what()));
        QMessageBox::critical(this, "执行失败", QString::fromUtf8(ex.what()));
    }
}

void ExecutionPage::cancelQualityCheck() {
    if (worker_) {
        worker_->requestCancel();
        appendLog("正在取消质检任务...");
    }
}

void ExecutionPage::onWorkerProgress(int percent, const QString& message) {
    progress_->setValue(percent);
    appendLog(message);
}

void ExecutionPage::onWorkerFinished(const TaskSessionReport& report) {
    setRunning(false);
    for (const auto& log : report.logs) {
        appendLog(QString::fromStdString(log));
    }
    renderReportSummary(report);
    progress_->setValue(100);
    appendLog("质检报告已生成，自动切换到结果中心。");
    emit reportReady(report);
}

void ExecutionPage::onWorkerError(const QString& errorMessage) {
    setRunning(false);
    progress_->setValue(0);
    appendLog("执行失败：" + errorMessage);
    QMessageBox::critical(this, "执行失败", errorMessage);
}

void ExecutionPage::setRunning(bool running) {
    if (runButton_) runButton_->setEnabled(!running);
    if (cancelButton_) cancelButton_->setEnabled(running);
    if (pathEdit_) pathEdit_->setEnabled(!running);
    if (taskNameEdit_) taskNameEdit_->setEnabled(!running);
    if (schemeCombo_) schemeCombo_->setEnabled(!running);
}

void ExecutionPage::appendLog(const QString& message) {
    if (localLog_) {
        localLog_->append(message);
    }
    emit logMessage(message);
}

void ExecutionPage::renderReportSummary(const TaskSessionReport& report) {
    summaryLabel_->setText(QString("任务：%1\n成果目录：%2\n\n数据源：%3 个\n规则：%4 条\n问题：%5 个\n通过率：%6")
        .arg(QString::fromStdString(report.task.name()))
        .arg(QString::fromStdString(report.task.inputPath()))
        .arg(report.scan.sourceCount)
        .arg(report.statistics.totalRules)
        .arg(static_cast<int>(report.issues.size()))
        .arg(QString::fromStdString(report.statistics.passRateText)));

    const int previewCount = std::min<int>(static_cast<int>(report.issues.size()), 20);
    issuePreviewTable_->setRowCount(previewCount);
    for (int r = 0; r < previewCount; ++r) {
        const auto& issue = report.issues[static_cast<std::size_t>(r)];
        issuePreviewTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(issue.issueId)));
        issuePreviewTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(issue.ruleCode)));
        issuePreviewTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(issue.layerName)));
        issuePreviewTable_->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(issue.description)));
        issuePreviewTable_->setItem(r, 4, new QTableWidgetItem(severityLabel(issue.severity)));
    }
}

std::string ExecutionPage::defaultRulesPath() const {
    RuleTemplateStore store;
    return store.activeTemplatePath();
}

void ExecutionPage::refreshSchemeList() {
    if (!schemeCombo_) return;
    schemeCombo_->clear();

    // Add internal active config as first option
    schemeCombo_->addItem(QStringLiteral("\u5185\u7f6e\u914d\u7f6e"), "");

    // Scan project scheme dirs
    const QString baseDir = QDir::currentPath() + "/data/projects";
    QDir base(baseDir);
    if (base.exists()) {
        for (const auto& projDir : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir proj(baseDir + "/" + projDir);
            for (const auto& entry : proj.entryList({"*.json"}, QDir::Files)) {
                const QString fullPath = proj.absoluteFilePath(entry);
                const QString label = projDir + "/" + entry.chopped(5);
                schemeCombo_->addItem(label, fullPath);
            }
        }
    }

    templateLabel_->setText(QString("(%1 个方案可用)").arg(schemeCombo_->count()));
}

void ExecutionPage::onSchemeChanged(int index) {
    if (index < 0 || !schemeCombo_) return;
    const QString schemePath = schemeCombo_->itemData(index).toString();
    if (schemePath.isEmpty()) {
        templateLabel_->setText(QStringLiteral("\u5185\u7f6e\u914d\u7f6e"));
        return;
    }
    try {
        RuleTemplateLoader loader;
        const auto templ = loader.loadFromFile(schemePath.toStdString());
        templateLabel_->setText(QString("%1 条规则").arg(templ.rules.size()));
        // Auto-fill data source path from scheme
        if (!templ.dataSourcePath.empty() && pathEdit_) {
            pathEdit_->setText(QString::fromStdString(templ.dataSourcePath));
            appendLog("方案数据源路径：" + QString::fromStdString(templ.dataSourcePath));
        }
    } catch (...) {
        templateLabel_->setText(QStringLiteral("\u52a0\u8f7d\u5931\u8d25"));
    }
}

ExecutionPage::~ExecutionPage() {
    if (workerThread_ && workerThread_->isRunning()) {
        if (worker_) worker_->requestCancel();
        workerThread_->quit();
        workerThread_->wait(3000);
    }
}

} // namespace gisqc
