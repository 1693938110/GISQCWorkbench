#include "DashboardPage.h"

#include "../core/DatasetScanService.h"

#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>

namespace gisqc {

namespace {

QFrame* makeCard(QWidget* parent = nullptr) {
    auto* card = new QFrame(parent);
    card->setObjectName("Card");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);
    return card;
}

} // namespace

DashboardPage::DashboardPage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // ---- Band 1: Path input card ----
    auto* pathCard = makeCard(this);
    auto* pathLayout = qobject_cast<QVBoxLayout*>(pathCard->layout());
    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(10);
    pathEdit_ = new QLineEdit(pathCard);
    pathEdit_->setPlaceholderText("选择包含 FileGDB、Shapefile 或 GeoPackage 的成果目录");
    pathRow->addWidget(pathEdit_, 1);
    auto* browseButton = new QPushButton("浏览...", pathCard);
    browseButton->setToolTip("打开文件浏览器选择成果目录");
    connect(browseButton, &QPushButton::clicked, this, &DashboardPage::chooseDatasetPath);
    pathRow->addWidget(browseButton);
    auto* scanButton = new QPushButton("扫描", pathCard);
    scanButton->setObjectName("PrimaryButton");
    scanButton->setToolTip("扫描当前路径下的 GIS 数据源");
    connect(scanButton, &QPushButton::clicked, this, &DashboardPage::scanCurrentPath);
    pathRow->addWidget(scanButton);
    pathLayout->addLayout(pathRow);

    auto* flowRow = new QHBoxLayout();
    flowRow->setSpacing(10);
    configureButton_ = new QPushButton("下一步：配置规则", pathCard);
    configureButton_->setToolTip("跳转到规则配置页面，调整质检规则参数");
    connect(configureButton_, &QPushButton::clicked, this, &DashboardPage::requestRuleConfig);
    flowRow->addWidget(configureButton_);
    runButton_ = new QPushButton("执行质检", pathCard);
    runButton_->setObjectName("PrimaryButton");
    runButton_->setToolTip("立即开始质量检查");
    connect(runButton_, &QPushButton::clicked, this, &DashboardPage::requestExecution);
    flowRow->addWidget(runButton_);
    flowRow->addStretch();
    pathLayout->addLayout(flowRow);
    layout->addWidget(pathCard);

    // ---- Band 2: Table + Summary in one card ----
    auto* bodyCard = new QFrame(this);
    bodyCard->setObjectName("Card");
    auto* bodyLayout = new QHBoxLayout(bodyCard);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    layerTable_ = new QTableWidget(0, 5, bodyCard);
    layerTable_->setHorizontalHeaderLabels({"图层名", "类型", "要素数", "坐标系", "状态"});
    layerTable_->horizontalHeader()->setStretchLastSection(true);
    layerTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layerTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layerTable_->verticalHeader()->setVisible(false);
    layerTable_->setAlternatingRowColors(true);
    layerTable_->setStyleSheet("QTableWidget { border: none; border-radius: 0; border-right: 1px solid #dee7f1; }");
    bodyLayout->addWidget(layerTable_, 3);

    auto* summaryPane = new QWidget(bodyCard);
    auto* summaryVBox = new QVBoxLayout(summaryPane);
    summaryVBox->setContentsMargins(18, 16, 18, 16);
    summaryVBox->setSpacing(8);
    auto* summaryTitle = new QLabel("数据概况", summaryPane);
    summaryTitle->setObjectName("CardTitle");
    summaryVBox->addWidget(summaryTitle);
    summaryLabel_ = new QLabel(QString::fromUtf8("选择成果目录并点击【扫描】后，\n将在此显示数据集结构信息。"), summaryPane);
    summaryLabel_->setObjectName("SubtleText");
    summaryLabel_->setWordWrap(true);
    summaryVBox->addWidget(summaryLabel_, 1);
    bodyLayout->addWidget(summaryPane, 1);
    layout->addWidget(bodyCard, 1);

    // ---- Band 3: Log ----
    auto* logCard = makeCard(this);
    auto* logLayout = qobject_cast<QVBoxLayout*>(logCard->layout());
    auto* logTitle = new QLabel("扫描日志", logCard);
    logTitle->setObjectName("CardTitle");
    logLayout->addWidget(logTitle);
    localLog_ = new QTextEdit(logCard);
    localLog_->setReadOnly(true);
    localLog_->setMaximumHeight(80);
    localLog_->setObjectName("LogView");
    logLayout->addWidget(localLog_);
    progress_ = new QProgressBar(logCard);
    progress_->setValue(0);
    progress_->setFormat("扫描进度 %p%");
    logLayout->addWidget(progress_);
    layout->addWidget(logCard);

    appendLog("等待选择成果目录。");
}

void DashboardPage::setDatasetPath(const QString& path) {
    if (pathEdit_) {
        pathEdit_->setText(path);
    }
}

void DashboardPage::chooseDatasetPath() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择成果目录", pathEdit_->text());
    if (!dir.isEmpty()) {
        pathEdit_->setText(dir);
        emit datasetPathChanged(dir);
        appendLog("已选择成果目录：" + dir);
        scanCurrentPath();
    }
}

void DashboardPage::scanCurrentPath() {
    const QString inputPath = pathEdit_->text().trimmed();
    if (inputPath.isEmpty()) {
        appendLog("请先选择成果目录。");
        progress_->setValue(0);
        return;
    }
    emit datasetPathChanged(inputPath);

    DatasetScanService service;
    const auto summary = service.scan(inputPath.toStdString());

    layerTable_->setRowCount(static_cast<int>(summary.rows.size()));
    for (int r = 0; r < static_cast<int>(summary.rows.size()); ++r) {
        const auto& row = summary.rows[static_cast<std::size_t>(r)];
        layerTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(row.name)));
        layerTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(row.type)));
        layerTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(row.featureCountText)));
        layerTable_->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(row.crsText)));
        layerTable_->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(row.status)));
    }

    for (const auto& log : summary.logs) {
        appendLog(QString::fromStdString(log));
    }
    renderScanSummary(summary);
    progress_->setValue(summary.rows.empty() ? 0 : 100);
}

void DashboardPage::appendLog(const QString& message) {
    if (localLog_) {
        localLog_->append(message);
    }
    emit logMessage(message);
}

void DashboardPage::renderScanSummary(const DatasetScanSummary& summary) {
    if (!summaryLabel_) {
        return;
    }

    int fileGdbCount = 0;
    int shapefileCount = 0;
    int geoPackageCount = 0;
    int featureKnownCount = 0;
    long long totalFeatureCount = 0;
    int crsKnownCount = 0;

    for (const auto& row : summary.rows) {
        if (row.type == "FileGDB") {
            ++fileGdbCount;
        } else if (row.type == "Shapefile") {
            ++shapefileCount;
        } else if (row.type == "GeoPackage") {
            ++geoPackageCount;
        }

        bool numeric = !row.featureCountText.empty();
        long long value = 0;
        for (const char ch : row.featureCountText) {
            if (ch < '0' || ch > '9') {
                numeric = false;
                break;
            }
            value = value * 10 + (ch - '0');
        }
        if (numeric) {
            ++featureKnownCount;
            totalFeatureCount += value;
        }

        if (!row.crsText.empty() &&
            row.crsText.find("待接入") == std::string::npos &&
            row.crsText.find("缺少") == std::string::npos &&
            row.crsText.find("未发现") == std::string::npos) {
            ++crsKnownCount;
        }
    }

    const QString featureText = featureKnownCount > 0
        ? QString("%1（%2 个数据源已读取）").arg(totalFeatureCount).arg(featureKnownCount)
        : QString("待读取");
    const QString statusText = summary.rows.empty() ? "未发现支持的数据源" : "扫描完成";

    summaryLabel_->setText(QString("数据集：%1\n\n数据源：%2 个\nFileGDB：%3 个\nShapefile：%4 个\nGeoPackage：%5 个\n\n要素总数：%6\n坐标系：%7 / %8 个数据源已识别\n状态：%9")
        .arg(QString::fromStdString(summary.rootPath))
        .arg(summary.sourceCount)
        .arg(fileGdbCount)
        .arg(shapefileCount)
        .arg(geoPackageCount)
        .arg(featureText)
        .arg(crsKnownCount)
        .arg(static_cast<int>(summary.rows.size()))
        .arg(statusText));
}

} // namespace gisqc
