#include "TemplateManagePage.h"

#include "../core/RuleTemplateLoader.h"
#include "../core/RuleTemplateStore.h"

#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

#include <map>

namespace gisqc {

namespace {

QFrame* makeCard(QWidget* parent = nullptr) {
    auto* card = new QFrame(parent);
    card->setObjectName("Card");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(10);
    return card;
}

QString categoryLabel(const std::string& code) {
    if (code.rfind("A", 0) == 0) return "A 成果完整性";
    if (code.rfind("B01", 0) == 0) return "B 数据库分层";
    if (code.rfind("B02", 0) == 0) return "B 属性专题";
    if (code.rfind("C01", 0) == 0) return "C 空间基础";
    if (code.rfind("C02", 0) == 0) return "C 空间几何";
    if (code.rfind("C03", 0) == 0) return "C 空间拓扑";
    return "其他";
}

} // namespace

TemplateManagePage::TemplateManagePage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);

    // Template info card
    auto* infoCard = makeCard(this);
    auto* infoLayout = qobject_cast<QVBoxLayout*>(infoCard->layout());

    auto* infoTitle = new QLabel("当前模板", infoCard);
    infoTitle->setObjectName("CardTitle");
    infoLayout->addWidget(infoTitle);

    templateInfoLabel_ = new QLabel(infoCard);
    templateInfoLabel_->setWordWrap(true);
    infoLayout->addWidget(templateInfoLabel_);

    sourceLabel_ = new QLabel(infoCard);
    sourceLabel_->setObjectName("SubtleText");
    infoLayout->addWidget(sourceLabel_);

    // Toolbar
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(12);

    auto* reloadBtn = new QPushButton("重载模板", infoCard);
    reloadBtn->setToolTip("从磁盘重新加载当前活跃模板");
    connect(reloadBtn, &QPushButton::clicked, this, &TemplateManagePage::reloadTemplate);
    toolbar->addWidget(reloadBtn);

    auto* resetBtn = new QPushButton("恢复默认", infoCard);
    resetBtn->setToolTip("删除用户自定义配置，恢复内置默认规则模板");
    connect(resetBtn, &QPushButton::clicked, this, &TemplateManagePage::resetToDefault);
    toolbar->addWidget(resetBtn);

    auto* exportBtn = new QPushButton("导出 JSON", infoCard);
    exportBtn->setToolTip("将当前模板导出为 JSON 文件");
    connect(exportBtn, &QPushButton::clicked, this, &TemplateManagePage::exportTemplate);
    toolbar->addWidget(exportBtn);

    auto* importBtn = new QPushButton("导入 JSON", infoCard);
    importBtn->setObjectName("PrimaryButton");
    importBtn->setToolTip("从 JSON 文件导入规则模板");
    connect(importBtn, &QPushButton::clicked, this, &TemplateManagePage::importTemplate);
    toolbar->addWidget(importBtn);

    toolbar->addStretch();
    infoLayout->addLayout(toolbar);
    layout->addWidget(infoCard);

    // Rule summary table (read-only)
    auto* tableCard = makeCard(this);
    auto* tableLayout = qobject_cast<QVBoxLayout*>(tableCard->layout());

    auto* tableTitle = new QLabel("规则概览（只读）", tableCard);
    tableTitle->setObjectName("CardTitle");
    tableLayout->addWidget(tableTitle);

    summaryTable_ = new QTableWidget(0, 5, tableCard);
    summaryTable_->setHorizontalHeaderLabels({"分类", "规则编码", "规则名称", "严重级别", "状态"});
    summaryTable_->horizontalHeader()->setStretchLastSection(true);
    summaryTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    summaryTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    summaryTable_->verticalHeader()->setVisible(false);
    summaryTable_->setAlternatingRowColors(true);
    summaryTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    summaryTable_->setSelectionBehavior(QTableWidget::SelectRows);
    tableLayout->addWidget(summaryTable_, 1);
    layout->addWidget(tableCard, 1);

    // Load
    reloadTemplate();
}

void TemplateManagePage::reloadTemplate() {
    try {
        RuleTemplateStore store;
        templ_ = store.loadActive();
        if (sourceLabel_) {
            sourceLabel_->setText("来源路径：" + QString::fromStdString(store.activeTemplatePath()));
        }
        renderTemplateInfo();
        renderRuleSummaryTable();
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "加载失败", QString::fromUtf8(ex.what()));
    }
}

void TemplateManagePage::resetToDefault() {
    if (QMessageBox::question(this, "恢复默认模板",
            "将删除用户自定义配置，恢复使用内置默认规则模板。是否继续？") != QMessageBox::Yes) {
        return;
    }
    RuleTemplateStore store;
    store.resetUserTemplate();
    templ_ = store.loadDefault();
    if (sourceLabel_) {
        sourceLabel_->setText("来源路径：" + QString::fromStdString(store.defaultTemplatePath()));
    }
    renderTemplateInfo();
    renderRuleSummaryTable();
    QMessageBox::information(this, "已恢复", "已恢复为内置默认规则模板。");
}

void TemplateManagePage::exportTemplate() {
    const QString fileName = QFileDialog::getSaveFileName(this, "导出规则模板",
        QString::fromStdString(templ_.templateCode) + ".json", "JSON 文件 (*.json)");
    if (fileName.isEmpty()) return;

    try {
        RuleTemplateStore store;
        const std::string json = RuleTemplateStore::toJson(templ_);
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "导出失败", "无法写入文件：" + fileName);
            return;
        }
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << QString::fromStdString(json);
        QMessageBox::information(this, "导出完成", "规则模板已导出：" + fileName);
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "导出失败", QString::fromUtf8(ex.what()));
    }
}

void TemplateManagePage::importTemplate() {
    const QString fileName = QFileDialog::getOpenFileName(this, "导入规则模板", "", "JSON 文件 (*.json)");
    if (fileName.isEmpty()) return;

    try {
        RuleTemplateStore store;
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "导入失败", "无法读取文件：" + fileName);
            return;
        }
        QTextStream stream(&file);
        const std::string json = stream.readAll().toStdString();
        RuleTemplateLoader loader;
        templ_ = loader.loadFromString(json);
        store.saveUserTemplate(templ_);
        if (sourceLabel_) {
            sourceLabel_->setText("来源路径：" + QString::fromStdString(store.userTemplatePath()));
        }
        renderTemplateInfo();
        renderRuleSummaryTable();
        QMessageBox::information(this, "导入完成", "规则模板已导入并保存为用户配置。");
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "导入失败", QString::fromUtf8(ex.what()));
    }
}

void TemplateManagePage::renderTemplateInfo() {
    if (!templateInfoLabel_) return;

    int enabledCount = 0;
    std::map<QString, int> categoryCounts;
    for (const auto& rule : templ_.rules) {
        if (rule.enabled) ++enabledCount;
        categoryCounts[categoryLabel(rule.code)]++;
    }

    QStringList categoryLines;
    for (const auto& [cat, count] : categoryCounts) {
        categoryLines << QString("  %1：%2 条").arg(cat).arg(count);
    }

    templateInfoLabel_->setText(
        QString("模板编码：%1\n模板名称：%2\n\n规则总数：%3 条（已启用 %4 条）\n\n分类统计：\n%5")
            .arg(QString::fromStdString(templ_.templateCode),
                 QString::fromStdString(templ_.templateName))
            .arg(static_cast<int>(templ_.rules.size()))
            .arg(enabledCount)
            .arg(categoryLines.join("\n")));
}

void TemplateManagePage::renderRuleSummaryTable() {
    if (!summaryTable_) return;

    summaryTable_->setRowCount(static_cast<int>(templ_.rules.size()));
    for (int r = 0; r < static_cast<int>(templ_.rules.size()); ++r) {
        const auto& rule = templ_.rules[static_cast<std::size_t>(r)];
        summaryTable_->setItem(r, 0, new QTableWidgetItem(categoryLabel(rule.code)));
        summaryTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(rule.code)));
        summaryTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(rule.name)));
        summaryTable_->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(rule.severity)));
        summaryTable_->setItem(r, 4, new QTableWidgetItem(rule.enabled ? "已启用" : "已禁用"));
    }
}

} // namespace gisqc
