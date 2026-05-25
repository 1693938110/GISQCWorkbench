#include "RuleConfigPage.h"

#include "../core/RuleTemplateStore.h"

#include "../core/DatasetScanner.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTableWidgetItem>

#include <filesystem>
#include <set>
#include <QTreeWidget>
#include <QVBoxLayout>

#ifdef GISQC_HAVE_GDAL
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#endif

namespace gisqc {

namespace {

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem* editableItem(const QString& text) {
    return new QTableWidgetItem(text);
}

QString friendlyParamLabel(const std::string& key) {
    static const std::map<std::string, QString> labels = {
        {"layers", "检查图层"},
        {"sourceLayer", "源图层"},
        {"referenceLayer", "参照图层"},
        {"angleTolerance", "尖锐角阈值（度）"},
        {"areaTolerance", "微小面阈值（㎡）"},
        {"wkid", "坐标系"},
        {"name", "坐标系名称"},
        {"minNodes", "最少节点数"},
        {"maxNodes", "最多节点数（0=不限）"},
        {"requiredFolders", "必选目录（逗号分隔）"},
        {"requiredFiles", "必选文件（逗号分隔）"},
        {"requiredPaths", "必选路径（逗号分隔）"},
        {"requiredDataSource", "需要数据入口"},
        {"scanEmptyFolders", "扫描空目录"},
        {"fileNameRegex", "文件名正则"},
        {"checkShapefileSidecars", "检查配套文件"},
        {"allowedTables", "允许的数据表"},
        {"requiredTables", "必选数据表（逗号分隔）"},
        {"requiredFields", "必选字段"},
        {"allowedFields", "允许字段"},
        {"fieldTypes", "字段类型规范"},
        {"fieldLengths", "字段长度规范"},
        {"valueDomains", "值域规范"},
        {"uniqueFields", "唯一性字段"},
        {"requiredValueFields", "必填字段"},
        {"singleFieldEnums", "单字段枚举"},
        {"multiFieldEnums", "多字段枚举对应"},
        {"unit", "单位"},
        {"minLength", "碎线最短长度（m）"},
        {"minEdgeLength", "超短边最短长度（m）"},
        {"gapTolerance", "空隙面积阈值"},
        {"templateDir", "模板目录"},
    };
    auto it = labels.find(key);
    return it != labels.end() ? it->second : QString::fromStdString(key);
}

// Determine geometry-aware layer presets based on rule object type
QStringList layerPresets(const std::string& object, const QStringList& realLayers = {}) {
    QStringList items;
    items << "" << "*（全部图层）";
    if (object.find("点") != std::string::npos || object == "空间基础")
        items << "*点（全部点图层）";
    if (object.find("线") != std::string::npos || object == "空间基础")
        items << "*线（全部线图层）";
    if (object.find("面") != std::string::npos || object == "空间基础")
        items << "*面（全部面图层）";
    // Append actual layer names from data source
    if (!realLayers.isEmpty()) {
        items << "---";
        items << realLayers;
    }
    return items;
}

// Built-in coordinate reference systems
struct CrsEntry { QString wkid; QString label; };
const std::vector<CrsEntry>& builtinCrs() {
    static const std::vector<CrsEntry> list = {
        {"4490", "CGCS2000 地理坐标系 (EPSG:4490)"},
        {"4326", "WGS 84 地理坐标系 (EPSG:4326)"},
        {"4549", "CGCS2000 / 3度带 投影 (EPSG:4549)"},
        {"4548", "CGCS2000 / 6度带 投影 (EPSG:4548)"},
        {"4528", "CGCS2000 / 高斯 3度带 (EPSG:4528)"},
        {"4527", "CGCS2000 / 高斯 6度带 (EPSG:4527)"},
        {"2435", "Beijing 1954 / 3度带 (EPSG:2435)"},
        {"4214", "Beijing 1954 地理坐标系 (EPSG:4214)"},
        {"4610", "Xian 1980 地理坐标系 (EPSG:4610)"},
        {"32650", "WGS 84 / UTM zone 50N (EPSG:32650)"},
        {"32651", "WGS 84 / UTM zone 51N (EPSG:32651)"},
        {"3857", "WGS 84 / Web Mercator (EPSG:3857)"},
    };
    return list;
}

} // namespace

RuleConfigPage::RuleConfigPage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    // ---- Compact toolbar: project | scheme | actions | tolerance ----
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(6);
    toolbar->setContentsMargins(0, 0, 0, 0);

    toolbar->addWidget(new QLabel(QStringLiteral("\u9879\u76ee"), this));
    projectCombo_ = new QComboBox(this);
    projectCombo_->setMinimumWidth(140);
    connect(projectCombo_, QOverload<int>::of(&QComboBox::activated), this, &RuleConfigPage::onProjectChanged);
    toolbar->addWidget(projectCombo_);
    auto* newProjBtn = new QPushButton(QStringLiteral("\u65b0\u5efa"), this);
    newProjBtn->setToolTip(QStringLiteral("\u65b0\u5efa\u9879\u76ee"));
    connect(newProjBtn, &QPushButton::clicked, this, &RuleConfigPage::newProject);
    toolbar->addWidget(newProjBtn);
    auto* delProjBtn = new QPushButton(QStringLiteral("\u5220\u9664"), this);
    delProjBtn->setToolTip(QStringLiteral("\u5220\u9664\u9879\u76ee"));
    connect(delProjBtn, &QPushButton::clicked, this, &RuleConfigPage::deleteProject);
    toolbar->addWidget(delProjBtn);

    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFixedWidth(1);
    toolbar->addWidget(sep1);

    toolbar->addWidget(new QLabel(QStringLiteral("\u65b9\u6848"), this));
    schemeCombo_ = new QComboBox(this);
    schemeCombo_->setMinimumWidth(160);
    connect(schemeCombo_, QOverload<int>::of(&QComboBox::activated), this, &RuleConfigPage::loadSelectedScheme);
    toolbar->addWidget(schemeCombo_);

    auto* newBtn = new QPushButton(QStringLiteral("\u65b0\u5efa"), this);
    connect(newBtn, &QPushButton::clicked, this, &RuleConfigPage::newScheme);
    toolbar->addWidget(newBtn);
    auto* saveBtn = new QPushButton(QStringLiteral("\u4fdd\u5b58"), this);
    saveBtn->setObjectName("PrimaryButton");
    connect(saveBtn, &QPushButton::clicked, this, &RuleConfigPage::saveScheme);
    toolbar->addWidget(saveBtn);
    auto* delBtn = new QPushButton(QStringLiteral("\u5220\u9664"), this);
    connect(delBtn, &QPushButton::clicked, this, &RuleConfigPage::deleteScheme);
    toolbar->addWidget(delBtn);

    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedWidth(1);
    toolbar->addWidget(sep2);

    toolbar->addWidget(new QLabel(QStringLiteral("\u5168\u5c40\u5bb9\u5dee"), this));
    toleranceEdit_ = new QLineEdit(this);
    toleranceEdit_->setPlaceholderText("0.001");
    toleranceEdit_->setMaximumWidth(80);
    toleranceEdit_->setText("0.001");
    toolbar->addWidget(toleranceEdit_);

    toolbar->addStretch();
    mainLayout->addLayout(toolbar);

    // ---- Data source row ----
    auto* dsRow = new QHBoxLayout();
    dsRow->setSpacing(6);
    dsRow->setContentsMargins(0, 0, 0, 0);
    dsRow->addWidget(new QLabel(QStringLiteral("\u6570\u636e\u6e90"), this));
    dataSourceCombo_ = new QComboBox(this);
    dataSourceCombo_->setEditable(true);
    dataSourceCombo_->setMinimumWidth(300);
    dataSourceCombo_->setPlaceholderText(QStringLiteral("\u9009\u62e9\u6216\u6d4f\u89c8\u6570\u636e\u76ee\u5f55"));
    dsRow->addWidget(dataSourceCombo_, 1);
    auto* browseBtn = new QPushButton(QStringLiteral("\u6d4f\u89c8..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &RuleConfigPage::browseDataSource);
    dsRow->addWidget(browseBtn);
    auto* reloadBtn = new QPushButton(QStringLiteral("\u52a0\u8f7d\u56fe\u5c42"), this);
    connect(reloadBtn, &QPushButton::clicked, this, &RuleConfigPage::reloadLayerNames);
    dsRow->addWidget(reloadBtn);
    auto* layerCountLabel = new QLabel(this);
    layerCountLabel->setObjectName("dsLayerCount");
    dsRow->addWidget(layerCountLabel);
    mainLayout->addLayout(dsRow);

    // ---- Two-panel body ----
    auto* body = new QHBoxLayout();
    body->setSpacing(14);

    // == Left: Available rules pool ==
    auto* leftPanel = new QVBoxLayout();
    leftPanel->setSpacing(8);

    auto* leftTitle = new QLabel("可用质检项", this);
    leftTitle->setObjectName("CardTitle");
    leftPanel->addWidget(leftTitle);

    // Category tree (compact)
    categoryTree_ = new QTreeWidget(this);
    categoryTree_->setHeaderLabel("按分类筛选");
    categoryTree_->addTopLevelItem(new QTreeWidgetItem(QStringList("全部规则")));
    categoryTree_->addTopLevelItem(new QTreeWidgetItem(QStringList("A 成果完整性")));
    categoryTree_->addTopLevelItem(new QTreeWidgetItem(QStringList("B 属性专题")));
    categoryTree_->addTopLevelItem(new QTreeWidgetItem(QStringList("C 空间基础")));
    categoryTree_->addTopLevelItem(new QTreeWidgetItem(QStringList("C 空间几何")));
    categoryTree_->addTopLevelItem(new QTreeWidgetItem(QStringList("C 空间拓扑")));
    categoryTree_->setMaximumHeight(180);
    categoryTree_->setCurrentItem(categoryTree_->topLevelItem(0));
    connect(categoryTree_, &QTreeWidget::currentItemChanged, this, &RuleConfigPage::filterByCategory);
    leftPanel->addWidget(categoryTree_);

    availableTable_ = new QTableWidget(0, 4, this);
    availableTable_->setHorizontalHeaderLabels({"编码", "规则名称", "分类", "检查对象"});
    availableTable_->horizontalHeader()->setStretchLastSection(true);
    availableTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    availableTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    availableTable_->verticalHeader()->setVisible(false);
    availableTable_->setAlternatingRowColors(true);
    availableTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    availableTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    availableTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    leftPanel->addWidget(availableTable_, 1);

    auto* addBtn = new QPushButton("添加到方案  \u2192", this);
    addBtn->setObjectName("PrimaryButton");
    connect(addBtn, &QPushButton::clicked, this, &RuleConfigPage::addSelectedRules);
    leftPanel->addWidget(addBtn);

    body->addLayout(leftPanel, 2);

    // == Right: Scheme rules ==
    auto* rightPanel = new QVBoxLayout();
    rightPanel->setSpacing(8);

    auto* rightHeader = new QHBoxLayout();
    auto* rightTitle = new QLabel("当前方案质检项", this);
    rightTitle->setObjectName("CardTitle");
    rightHeader->addWidget(rightTitle);
    schemeCountLabel_ = new QLabel("(0 条)", this);
    schemeCountLabel_->setObjectName("SubtleText");
    rightHeader->addWidget(schemeCountLabel_);
    rightHeader->addStretch();
    rightPanel->addLayout(rightHeader);

    schemeTable_ = new QTableWidget(0, 4, this);
    schemeTable_->setHorizontalHeaderLabels({QStringLiteral("\u7f16\u7801"), QStringLiteral("\u89c4\u5219\u540d\u79f0"), QStringLiteral("\u68c0\u67e5\u5bf9\u8c61"), QStringLiteral("\u4e25\u91cd\u7ea7\u522b")});
    schemeTable_->horizontalHeader()->setStretchLastSection(true);
    schemeTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    schemeTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    schemeTable_->verticalHeader()->setVisible(false);
    schemeTable_->setAlternatingRowColors(true);
    schemeTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    schemeTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(schemeTable_, &QTableWidget::currentCellChanged, this, &RuleConfigPage::updateDetailPanel);
    rightPanel->addWidget(schemeTable_, 1);

    auto* removeBtn = new QPushButton(QStringLiteral("\u2190  \u4ece\u65b9\u6848\u79fb\u9664"), this);
    connect(removeBtn, &QPushButton::clicked, this, &RuleConfigPage::removeSelectedRules);
    rightPanel->addWidget(removeBtn);

    // Interactive parameter panel
    auto* detailBox = new QGroupBox(QStringLiteral("\u53c2\u6570\u8bbe\u7f6e"), this);
    detailBox->setObjectName("Card");
    detailBox->setFixedHeight(200);
    auto* detailOuter = new QVBoxLayout(detailBox);
    detailOuter->setContentsMargins(8, 8, 8, 8);
    detailOuter->setSpacing(4);
    detailLabel_ = new QLabel(detailBox);
    detailLabel_->setWordWrap(true);
    detailLabel_->setObjectName("SubtleText");
    detailOuter->addWidget(detailLabel_);
    paramPanel_ = new QWidget(detailBox);
    paramForm_ = new QFormLayout(paramPanel_);
    paramForm_->setContentsMargins(0, 0, 0, 0);
    paramForm_->setSpacing(4);
    detailOuter->addWidget(paramPanel_);
    auto* applyBtn = new QPushButton(QStringLiteral("\u5e94\u7528\u53c2\u6570"), detailBox);
    connect(applyBtn, &QPushButton::clicked, this, &RuleConfigPage::applyParamEdits);
    detailOuter->addWidget(applyBtn);
    rightPanel->addWidget(detailBox);

    body->addLayout(rightPanel, 3);
    mainLayout->addLayout(body, 1);

    loadMasterRules();
    refreshProjectList();
    loadScheme();
    refreshSchemeLibrary();
    refreshAvailableTable();
    refreshSchemeTable();
}

void RuleConfigPage::setDatasetPath(const QString& path) {
    if (path.isEmpty() || !dataSourceCombo_) return;
    if (dataSourceCombo_->findText(path) < 0) {
        dataSourceCombo_->addItem(path);
    }
    dataSourceCombo_->setCurrentText(path);
    reloadLayerNames();
}

// ---- Data loading ----

void RuleConfigPage::loadMasterRules() {
    RuleTemplateStore store;
    const auto templ = store.loadDefault();
    masterRules_ = templ.rules;
}

void RuleConfigPage::loadScheme() {
    RuleTemplateStore store;
    const auto userPath = store.userTemplatePath();
    if (!userPath.empty() && std::filesystem::exists(std::filesystem::u8path(userPath))) {
        scheme_ = store.loadActive();
    } else {
        // First launch: start with empty scheme, user adds rules manually
        scheme_ = {};
        scheme_.templateName = "";
        scheme_.templateCode = "GIS_QC_USER_NEW";
        scheme_.globalTolerance = "0.001";
    }
    if (toleranceEdit_) {
        toleranceEdit_->setText(QString::fromStdString(
            scheme_.globalTolerance.empty() ? "0.001" : scheme_.globalTolerance));
    }
    if (dataSourceCombo_) {
        dataSourceCombo_->setCurrentText(QString::fromStdString(scheme_.dataSourcePath));
        if (!scheme_.dataSourcePath.empty()) reloadLayerNames();
    }
}

// ---- Table rendering ----

void RuleConfigPage::refreshAvailableTable() {
    if (!availableTable_) return;

    availableTable_->setRowCount(static_cast<int>(masterRules_.size()));
    for (int r = 0; r < static_cast<int>(masterRules_.size()); ++r) {
        const auto& rule = masterRules_[static_cast<std::size_t>(r)];
        const bool inScheme = schemeContainsRule(rule.code);
        auto* codeItem = readOnlyItem(QString::fromStdString(rule.code));
        auto* nameItem = readOnlyItem(cleanRuleName(rule.code));
        auto* catItem = readOnlyItem(categoryTextForCode(rule.code));

        QString objText;
        if (rule.code.rfind("A", 0) == 0) objText = "成果目录/文件";
        else if (rule.code.rfind("B01", 0) == 0) objText = "数据库分层";
        else if (rule.code.rfind("B02", 0) == 0) objText = "数据表/属性";
        else if (rule.code.rfind("C01", 0) == 0) objText = "空间基础";
        else if (rule.code.rfind("C02", 0) == 0) objText = "空间几何";
        else if (rule.code.rfind("C03", 0) == 0) objText = "空间拓扑";
        else objText = QString::fromStdString(rule.targetObject);
        auto* objItem = readOnlyItem(objText);

        if (inScheme) {
            const QColor muted(148, 163, 184);
            codeItem->setForeground(muted);
            nameItem->setForeground(muted);
            catItem->setForeground(muted);
            objItem->setForeground(muted);
        }

        availableTable_->setItem(r, 0, codeItem);
        availableTable_->setItem(r, 1, nameItem);
        availableTable_->setItem(r, 2, catItem);
        availableTable_->setItem(r, 3, objItem);
    }
    updateCategoryCounts();
    filterByCategory();
}

void RuleConfigPage::refreshSchemeTable() {
    if (!schemeTable_) return;

    schemeTable_->setRowCount(static_cast<int>(scheme_.rules.size()));
    for (int r = 0; r < static_cast<int>(scheme_.rules.size()); ++r) {
        const auto& rule = scheme_.rules[static_cast<std::size_t>(r)];
        schemeTable_->setItem(r, 0, readOnlyItem(QString::fromStdString(rule.code)));
        schemeTable_->setItem(r, 1, readOnlyItem(cleanRuleName(rule.code)));
        schemeTable_->setItem(r, 2, readOnlyItem(QString::fromStdString(rule.targetObject)));
        // Severity as Chinese combo box
        auto* sevCombo = new QComboBox(schemeTable_);
        sevCombo->addItems({QStringLiteral("\u9519\u8bef"), QStringLiteral("\u8b66\u544a"), QStringLiteral("\u63d0\u793a")});
        const QString sev = QString::fromStdString(rule.severity);
        if (sev == "warning" || sev == "\xe8\xad\xa6\xe5\x91\x8a") sevCombo->setCurrentIndex(1);
        else if (sev == "info" || sev == "\xe6\x8f\x90\xe7\xa4\xba") sevCombo->setCurrentIndex(2);
        else sevCombo->setCurrentIndex(0);
        schemeTable_->setCellWidget(r, 3, sevCombo);
    }
    if (schemeCountLabel_) {
        schemeCountLabel_->setText(QString("(%1 条)").arg(scheme_.rules.size()));
    }
}

// ---- Slots ----

void RuleConfigPage::newScheme() {
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        QStringLiteral("\u65b0\u5efa\u65b9\u6848"),
        QStringLiteral("\u8bf7\u8f93\u5165\u65b9\u6848\u540d\u79f0\uff1a"),
        QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    scheme_.rules.clear();
    scheme_.templateCode = "GIS_QC_USER_NEW";
    scheme_.templateName = name.trimmed().toStdString();
    refreshSchemeTable();
    refreshAvailableTable();
}

void RuleConfigPage::saveScheme() {
    // Collect severity from combo widgets
    for (int r = 0; r < schemeTable_->rowCount() && r < static_cast<int>(scheme_.rules.size()); ++r) {
        auto& rule = scheme_.rules[static_cast<std::size_t>(r)];
        auto* combo = qobject_cast<QComboBox*>(schemeTable_->cellWidget(r, 3));
        if (combo) rule.severity = combo->currentText().toStdString();
    }

    // Determine scheme name: from combo selection or prompt
    QString name = schemeCombo_ ? schemeCombo_->currentText().trimmed() : "";
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this,
            QStringLiteral("\u4fdd\u5b58\u65b9\u6848"),
            QStringLiteral("\u8bf7\u8f93\u5165\u65b9\u6848\u540d\u79f0\uff1a"),
            QLineEdit::Normal, QString::fromStdString(scheme_.templateName), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        name = name.trimmed();
    }
    scheme_.templateName = name.toStdString();
    scheme_.templateCode = "GIS_QC_INTERNAL_USER";
    if (toleranceEdit_) {
        scheme_.globalTolerance = toleranceEdit_->text().trimmed().toStdString();
    }
    if (dataSourceCombo_) {
        scheme_.dataSourcePath = dataSourceCombo_->currentText().trimmed().toStdString();
    }

    for (auto& rule : scheme_.rules) {
        rule.enabled = true;
    }

    try {
        RuleTemplateStore store;
        store.saveUserTemplate(scheme_);

        const QString dir = currentProjectDir();
        QDir().mkpath(dir);
        const std::string libPath = (dir + "/" + name + ".json").toUtf8().constData();
        RuleTemplateStore::saveToFile(scheme_, libPath);

        // Refresh combo without triggering loadSelectedScheme, then re-select
        const bool blocked = schemeCombo_->signalsBlocked();
        schemeCombo_->blockSignals(true);
        refreshSchemeLibrary();
        const int savedIdx = schemeCombo_->findText(name);
        if (savedIdx >= 0) schemeCombo_->setCurrentIndex(savedIdx);
        schemeCombo_->blockSignals(blocked);

        // Inline feedback instead of QMessageBox to avoid window jitter
        auto* lbl = findChild<QLabel*>("dsLayerCount");
        if (lbl) lbl->setText(QString("\u2714 \u5df2\u4fdd\u5b58 (%1 \u6761\u89c4\u5219)").arg(scheme_.rules.size()));
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("\u4fdd\u5b58\u5931\u8d25"), QString::fromUtf8(ex.what()));
    }
}

void RuleConfigPage::resetToDefault() {
    if (QMessageBox::question(this, QStringLiteral("\u91cd\u7f6e\u4e3a\u9ed8\u8ba4"),
            QStringLiteral("\u5c06\u5220\u9664\u5df2\u4fdd\u5b58\u7684\u7528\u6237\u914d\u7f6e\u65b9\u6848\u5e76\u6062\u590d\u9ed8\u8ba4\u89c4\u5219\u3002\u662f\u5426\u7ee7\u7eed\uff1f")) != QMessageBox::Yes) {
        return;
    }
    RuleTemplateStore store;
    store.resetUserTemplate();
    loadScheme();
    refreshSchemeTable();
    refreshAvailableTable();
}

// ---- Project management ----

void RuleConfigPage::newProject() {
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        QStringLiteral("\u65b0\u5efa\u9879\u76ee"),
        QStringLiteral("\u8bf7\u8f93\u5165\u9879\u76ee\u540d\u79f0\uff1a"),
        QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString dir = projectsBaseDir() + "/" + name.trimmed();
    QDir().mkpath(dir);
    refreshProjectList();
    // Select the new project
    const int idx = projectCombo_->findText(name.trimmed());
    if (idx >= 0) projectCombo_->setCurrentIndex(idx);
    onProjectChanged();
}

void RuleConfigPage::deleteProject() {
    if (!projectCombo_ || projectCombo_->count() == 0) return;
    const QString selected = projectCombo_->currentText();
    if (selected.isEmpty()) return;

    if (QMessageBox::question(this, QStringLiteral("\u5220\u9664\u9879\u76ee"),
            QStringLiteral("\u786e\u5b9a\u8981\u5220\u9664\u9879\u76ee\u300a") + selected +
            QStringLiteral("\u300b\u53ca\u5176\u6240\u6709\u65b9\u6848\u5417\uff1f")) != QMessageBox::Yes) {
        return;
    }

    QDir dir(projectsBaseDir() + "/" + selected);
    dir.removeRecursively();
    refreshProjectList();
    onProjectChanged();
}

void RuleConfigPage::onProjectChanged() {
    refreshSchemeLibrary();
    // Auto-load the first scheme if available
    if (schemeCombo_ && schemeCombo_->count() > 0) {
        schemeCombo_->setCurrentIndex(0);
        loadSelectedScheme();
    } else {
        // Clear the view instead of popping up a dialog
        scheme_.rules.clear();
        scheme_.templateCode = "GIS_QC_USER_NEW";
        scheme_.templateName = "";
        refreshSchemeTable();
        refreshAvailableTable();
    }
}

void RuleConfigPage::refreshProjectList() {
    if (!projectCombo_) return;
    const QString prev = projectCombo_->currentText();
    projectCombo_->clear();

    const QDir base(projectsBaseDir());
    QDir().mkpath(projectsBaseDir());
    const auto dirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& d : dirs) {
        projectCombo_->addItem(d);
    }

    // If no projects, create a default one
    if (projectCombo_->count() == 0) {
        QDir().mkpath(projectsBaseDir() + "/" + QStringLiteral("\u9ed8\u8ba4\u9879\u76ee"));
        projectCombo_->addItem(QStringLiteral("\u9ed8\u8ba4\u9879\u76ee"));
    }

    const int idx = projectCombo_->findText(prev);
    projectCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

// ---- Scheme management ----

void RuleConfigPage::deleteScheme() {
    if (!schemeCombo_ || schemeCombo_->currentIndex() < 0) return;
    const QString selected = schemeCombo_->currentText();
    if (selected.isEmpty()) return;

    if (QMessageBox::question(this, QStringLiteral("\u5220\u9664\u65b9\u6848"),
            QStringLiteral("\u786e\u5b9a\u8981\u5220\u9664\u65b9\u6848\u300a") + selected + QStringLiteral("\u300b\u5417\uff1f")) != QMessageBox::Yes) {
        return;
    }

    QFile::remove(currentProjectDir() + "/" + selected + ".json");
    refreshSchemeLibrary();
    if (schemeCombo_->count() > 0) loadSelectedScheme();
    else newScheme();
}

void RuleConfigPage::loadSelectedScheme() {
    if (!schemeCombo_ || schemeCombo_->currentIndex() < 0) return;
    const QString selected = schemeCombo_->currentText();
    if (selected.isEmpty()) return;

    const QString path = currentProjectDir() + "/" + selected + ".json";
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("\u52a0\u8f7d\u5931\u8d25"),
            QStringLiteral("\u65b9\u6848\u6587\u4ef6\u4e0d\u5b58\u5728\u3002"));
        return;
    }

    try {
        RuleTemplateLoader loader;
        scheme_ = loader.loadFromFile(path.toUtf8().constData());
        if (toleranceEdit_) toleranceEdit_->setText(QString::fromStdString(
            scheme_.globalTolerance.empty() ? "0.001" : scheme_.globalTolerance));
        if (dataSourceCombo_) {
            dataSourceCombo_->setCurrentText(QString::fromStdString(scheme_.dataSourcePath));
            if (!scheme_.dataSourcePath.empty()) reloadLayerNames();
        }
        refreshSchemeTable();
        refreshAvailableTable();
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("\u52a0\u8f7d\u5931\u8d25"), QString::fromUtf8(ex.what()));
    }
}

void RuleConfigPage::refreshSchemeLibrary() {
    if (!schemeCombo_) return;
    schemeCombo_->clear();

    const QDir dir(currentProjectDir());
    if (!dir.exists()) return;

    const auto entries = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& entry : entries) {
        schemeCombo_->addItem(entry.chopped(5)); // remove .json
    }

    const QString currentName = QString::fromStdString(scheme_.templateName);
    const int idx = schemeCombo_->findText(currentName);
    if (idx >= 0) schemeCombo_->setCurrentIndex(idx);
}

QString RuleConfigPage::projectsBaseDir() {
    return QDir::currentPath() + "/data/projects";
}

QString RuleConfigPage::currentProjectDir() const {
    const QString proj = projectCombo_ ? projectCombo_->currentText() : "";
    if (proj.isEmpty()) return projectsBaseDir() + "/" + QStringLiteral("\u9ed8\u8ba4\u9879\u76ee");
    return projectsBaseDir() + "/" + proj;
}

void RuleConfigPage::addSelectedRules() {
    const auto selected = availableTable_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this,
            QStringLiteral("\u63d0\u793a"),
            QStringLiteral("\u8bf7\u5148\u5728\u5de6\u4fa7\u300a\u53ef\u7528\u8d28\u68c0\u9879\u300b\u4e2d\u9009\u62e9\u8981\u6dfb\u52a0\u7684\u89c4\u5219\u3002"));
        return;
    }

    int added = 0;
    for (const auto& index : selected) {
        const int row = index.row();
        const auto* codeItem = availableTable_->item(row, 0);
        if (!codeItem) continue;
        const std::string code = codeItem->text().toStdString();
        if (schemeContainsRule(code)) continue;

        // Find the rule definition from master
        for (const auto& master : masterRules_) {
            if (master.code == code) {
                scheme_.rules.push_back(master);
                scheme_.rules.back().enabled = true;
                ++added;
                break;
            }
        }
    }

    if (added > 0) {
        refreshSchemeTable();
        refreshAvailableTable();
    }
}

void RuleConfigPage::removeSelectedRules() {
    const auto selected = schemeTable_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this,
            QStringLiteral("\u63d0\u793a"),
            QStringLiteral("\u8bf7\u5148\u5728\u53f3\u4fa7\u300a\u5f53\u524d\u65b9\u6848\u300b\u4e2d\u9009\u62e9\u8981\u79fb\u9664\u7684\u89c4\u5219\u3002"));
        return;
    }

    // Collect row indices, remove from end to avoid index shifting
    std::vector<int> rows;
    for (const auto& index : selected) {
        rows.push_back(index.row());
    }
    std::sort(rows.rbegin(), rows.rend());

    for (const int row : rows) {
        if (row >= 0 && row < static_cast<int>(scheme_.rules.size())) {
            scheme_.rules.erase(scheme_.rules.begin() + row);
        }
    }

    refreshSchemeTable();
    refreshAvailableTable();
}

void RuleConfigPage::browseDataSource() {
    const QString path = QFileDialog::getExistingDirectory(this,
        QStringLiteral("\u9009\u62e9\u6570\u636e\u6e90\u76ee\u5f55\uff08GDB \u6216\u6570\u636e\u6587\u4ef6\u5939\uff09"),
        dataSourceCombo_ ? dataSourceCombo_->currentText() : "");
    if (path.isEmpty()) return;

    // Warn when switching data source
    if (dataSourceCombo_ && !dataSourceCombo_->currentText().trimmed().isEmpty()
        && dataSourceCombo_->currentText().trimmed() != path) {
        if (QMessageBox::question(this, QStringLiteral("\u66f4\u6362\u6570\u636e\u6e90"),
                QStringLiteral("\u66f4\u6362\u6570\u636e\u6e90\u540e\uff0c\u89c4\u5219\u53c2\u6570\u4e2d\u7684\u56fe\u5c42\u9009\u62e9\u7b49\u914d\u7f6e\u53ef\u80fd\u9700\u8981\u91cd\u65b0\u8bbe\u7f6e\u3002\n\u662f\u5426\u7ee7\u7eed\uff1f")) != QMessageBox::Yes) {
            return;
        }
    }

    if (dataSourceCombo_) {
        if (dataSourceCombo_->findText(path) < 0) {
            dataSourceCombo_->addItem(path);
        }
        dataSourceCombo_->setCurrentText(path);
    }
    reloadLayerNames();
}

void RuleConfigPage::reloadLayerNames() {
    cachedLayerNames_.clear();
    const QString dsPath = dataSourceCombo_ ? dataSourceCombo_->currentText().trimmed() : "";
    if (dsPath.isEmpty()) return;

    // Use DatasetScanner + GDAL to enumerate all layer names
    DatasetScanner scanner;
    const auto scanned = scanner.scan(dsPath.toStdString());
    std::set<std::string> nameSet;
    for (const auto& source : scanned.sources) {
        if (!source.layerName.empty()) nameSet.insert(source.layerName);
#ifdef GISQC_HAVE_GDAL
        if (source.type == DatasetType::FileGDB || source.type == DatasetType::GeoPackage) {
            GDALAllRegister();
            auto* ds = static_cast<GDALDataset*>(GDALOpenEx(
                source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (!ds) continue;
            for (int i = 0; i < ds->GetLayerCount(); ++i) {
                OGRLayer* lyr = ds->GetLayer(i);
                if (lyr && lyr->GetName()) {
                    nameSet.insert(lyr->GetName());
                }
            }
            GDALClose(ds);
        }
#endif
    }

    for (const auto& n : nameSet) {
        cachedLayerNames_ << QString::fromStdString(n);
    }

    // Update UI label
    auto* lbl = findChild<QLabel*>("dsLayerCount");
    if (lbl) {
        lbl->setText(QString(QStringLiteral("\u5df2\u52a0\u8f7d %1 \u4e2a\u56fe\u5c42")).arg(cachedLayerNames_.size()));
    }
}

void RuleConfigPage::filterByCategory() {
    if (!categoryTree_ || !availableTable_) return;

    auto* current = categoryTree_->currentItem();
    if (!current) return;

    const QString selected = current->text(0).section('(', 0, 0).trimmed();
    const bool showAll = (selected == "全部规则");

    for (int r = 0; r < availableTable_->rowCount(); ++r) {
        if (showAll) {
            availableTable_->setRowHidden(r, false);
        } else {
            const auto* catItem = availableTable_->item(r, 2);
            const QString rowCategory = catItem ? catItem->text() : "";
            availableTable_->setRowHidden(r, !rowCategory.startsWith(selected));
        }
    }
}

void RuleConfigPage::updateDetailPanel() {
    if (!schemeTable_ || !detailLabel_ || !paramForm_) return;

    // Clear previous form fields
    while (paramForm_->rowCount() > 0) {
        paramForm_->removeRow(0);
    }

    const int row = schemeTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(scheme_.rules.size())) {
        detailLabel_->setText(QStringLiteral("\u9009\u62e9\u53f3\u4fa7\u65b9\u6848\u4e2d\u7684\u89c4\u5219\uff0c\u5728\u6b64\u5904\u7f16\u8f91\u53c2\u6570\u3002"));
        return;
    }

    const auto& rule = scheme_.rules[static_cast<std::size_t>(row)];
    detailLabel_->setText(
        QString::fromStdString(rule.code) + "  " + cleanRuleName(rule.code) +
        "\n" + QString::fromStdString(rule.message));

    // Build specialized form fields per parameter
    for (const auto& [key, value] : rule.parameters) {
        // Global tolerance handles geometry tolerance — skip per-rule tolerance
        if (key == "tolerance") continue;

        const QString qval = QString::fromStdString(value);
        const QString qkey = QString::fromStdString(key);

        if (key == "wkid") {
            // ---- CRS selector (single-select dropdown) ----
            auto* combo = new QComboBox(paramPanel_);
            combo->setObjectName(qkey);
            int selectedIdx = -1;
            for (std::size_t ci = 0; ci < builtinCrs().size(); ++ci) {
                const auto& crs = builtinCrs()[ci];
                combo->addItem(crs.label, crs.wkid);
                if (crs.wkid == qval) selectedIdx = static_cast<int>(ci);
            }
            if (selectedIdx >= 0) {
                combo->setCurrentIndex(selectedIdx);
            } else if (!qval.isEmpty()) {
                combo->addItem(qval + " (自定义)", qval);
                combo->setCurrentIndex(combo->count() - 1);
            }
            paramForm_->addRow(friendlyParamLabel(key), combo);

        } else if (key == "name") {
            // Skip "name" — auto-derived from wkid selection
            continue;

        } else if (key == "layers" || key == "sourceLayer" || key == "referenceLayer") {
            // ---- Layer selector (editable combo with presets) ----
            auto* combo = new QComboBox(paramPanel_);
            combo->setEditable(true);
            combo->setObjectName(qkey);
            const auto presets = layerPresets(rule.targetObject, cachedLayerNames_);
            combo->addItems(presets);
            // Set current value
            if (!qval.isEmpty()) {
                int idx = combo->findText(qval);
                if (idx >= 0) {
                    combo->setCurrentIndex(idx);
                } else {
                    // Check if it's a preset value like "*" stored without label
                    bool found = false;
                    for (int pi = 0; pi < combo->count(); ++pi) {
                        if (combo->itemText(pi).startsWith(qval)) {
                            combo->setCurrentIndex(pi);
                            found = true;
                            break;
                        }
                    }
                    if (!found) combo->setCurrentText(qval);
                }
            }
            paramForm_->addRow(friendlyParamLabel(key), combo);

        } else if (key == "templateDir") {
            // ---- Directory picker ----
            auto* container = new QWidget(paramPanel_);
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            auto* edit = new QLineEdit(qval, container);
            edit->setObjectName(qkey);
            auto* browseBtn = new QPushButton(QStringLiteral("..."), container);
            browseBtn->setFixedWidth(30);
            hbox->addWidget(edit);
            hbox->addWidget(browseBtn);
            connect(browseBtn, &QPushButton::clicked, this, [edit, this]() {
                const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择模板目录"), edit->text());
                if (!dir.isEmpty()) edit->setText(dir);
            });
            paramForm_->addRow(friendlyParamLabel(key), container);
        } else {
            // ---- Default: plain text input ----
            auto* edit = new QLineEdit(qval, paramPanel_);
            edit->setObjectName(qkey);
            paramForm_->addRow(friendlyParamLabel(key), edit);
        }
    }
}

void RuleConfigPage::applyParamEdits() {
    if (!schemeTable_ || !paramForm_) return;
    const int row = schemeTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(scheme_.rules.size())) return;

    auto& rule = scheme_.rules[static_cast<std::size_t>(row)];
    for (int i = 0; i < paramForm_->rowCount(); ++i) {
        auto* widget = paramForm_->itemAt(i, QFormLayout::FieldRole)->widget();
        const std::string key = widget->objectName().toStdString();

        if (key == "wkid") {
            auto* combo = qobject_cast<QComboBox*>(widget);
            if (combo) {
                rule.parameters["wkid"] = combo->currentData().toString().toStdString();
                // Auto-derive CRS name
                const QString label = combo->currentText();
                const int parenPos = label.indexOf('(');
                rule.parameters["name"] = (parenPos > 0
                    ? label.left(parenPos).trimmed() : label.trimmed()).toStdString();
            }
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            // Layer selector — extract the value prefix before "（"
            QString val = combo->currentText().trimmed();
            const int parenPos = val.indexOf(QStringLiteral("\uff08"));
            if (parenPos > 0) val = val.left(parenPos);
            rule.parameters[key] = val.toStdString();
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            rule.parameters[key] = edit->text().trimmed().toStdString();
        }
    }
    QMessageBox::information(this, QStringLiteral("\u53c2\u6570\u5df2\u5e94\u7528"),
        QString(QStringLiteral("\u5df2\u66f4\u65b0\u89c4\u5219 %1 \u7684\u53c2\u6570\u3002")).arg(QString::fromStdString(rule.code)));
}

void RuleConfigPage::updateCategoryCounts() {
    if (!categoryTree_) return;

    std::map<QString, int> counts;
    int total = static_cast<int>(masterRules_.size());
    for (const auto& rule : masterRules_) {
        counts[categoryTextForCode(rule.code)]++;
    }

    for (int i = 0; i < categoryTree_->topLevelItemCount(); ++i) {
        auto* item = categoryTree_->topLevelItem(i);
        const QString base = item->text(0).section('(', 0, 0).trimmed();
        if (base == "全部规则") {
            item->setText(0, QString("全部规则 (%1)").arg(total));
        } else {
            auto it = counts.find(base);
            int count = (it != counts.end()) ? it->second : 0;
            item->setText(0, QString("%1 (%2)").arg(base).arg(count));
        }
    }
}

bool RuleConfigPage::schemeContainsRule(const std::string& code) const {
    for (const auto& rule : scheme_.rules) {
        if (rule.code == code) return true;
    }
    return false;
}

// ---- Static helpers ----

QString RuleConfigPage::categoryTextForCode(const std::string& code) {
    if (code.rfind("A", 0) == 0) return "A 成果完整性";
    if (code.rfind("B", 0) == 0) return "B 属性专题";
    if (code.rfind("C01", 0) == 0) return "C 空间基础";
    if (code.rfind("C02", 0) == 0) return "C 空间几何";
    if (code.rfind("C03", 0) == 0) return "C 空间拓扑";
    return "其他";
}

QString RuleConfigPage::cleanRuleName(const std::string& code) {
    static const std::map<std::string, QString> names = {
        {"A010101", "目录应可正常读取且层级数量符合要求"},
        {"A010102", "目录名称应匹配对应层级命名要求"},
        {"A010103", "必选目录或文件不得缺失"},
        {"A010104", "必交付文件不得缺失"},
        {"A010201", "成果目录必须包含可识别 GIS 数据入口"},
        {"A010301", "成果目录不得包含空目录"},
        {"A010302", "成果文件命名应符合规范"},
        {"A010303", "Shapefile 配套文件必须完整"},
        {"A020101", "文件应可正常读取"},
        {"A020102", "文件名应符合命名规则"},
        {"A020103", "文件扩展名应符合要求"},
        {"A020104", "文件版本应符合要求"},
        {"B010101", "数据集应匹配数据库分层信息"},
        {"B010102", "必选数据集不得缺失"},
        {"B010201", "数据表应匹配数据库分层信息"},
        {"B010202", "必选数据表不得缺失"},
        {"B010203", "表别名和表类型应符合数据库分层规定"},
        {"B010204", "表与数据集组织结构应符合数据库分层规定"},
        {"B020101", "数据表应匹配数据表规范"},
        {"B020102", "字段名应匹配数据表规范"},
        {"B020103", "字段类型应符合数据表规范"},
        {"B020104", "字段长度应符合数据表规范"},
        {"B020106", "字段值域应符合数据表规范"},
        {"B020107", "字段取值唯一性应符合数据表规范"},
        {"B020108", "字段必填性应符合数据表规范"},
        {"B020201", "单字段取值应符合枚举值"},
        {"B020202", "多字段取值应符合枚举值对应关系"},
        {"C010101", "空间数据范围应位于工作范围内"},
        {"C010201", "坐标系统应符合要求"},
        {"C020201", "线要素不得存在短线"},
        {"C020301", "面要素不得存在超短边"},
        {"C020302", "面要素不得存在尖锐角"},
        {"C020303", "面要素不得存在微小面"},
        {"C020401", "线、面节点数量必须符合要求"},
        {"C020402", "线、面不得存在圆弧"},
        {"C020501", "点、线、面不得含 Z 值"},
        {"C030101", "点要素不得重合"},
        {"C030102", "点要素必须为单一部件"},
        {"C030201", "线要素不得重叠"},
        {"C030202", "线要素不得相交"},
        {"C030203", "线要素不得有悬挂点"},
        {"C030206", "线要素不得自重叠"},
        {"C030207", "线要素不得自相交"},
        {"C030208", "线要素必须为单一部件"},
        {"C030301", "面要素不得有空隙"},
        {"C030302", "面要素不得重叠"},
        {"C030303", "面要素不得自相交"},
        {"C030305", "面要素不得有孔洞"},
        {"C030306", "面要素必须为单一部件"}
    };
    const auto it = names.find(code);
    return (it != names.end()) ? it->second : QString::fromStdString(code);
}

QString RuleConfigPage::formatParameters(const std::map<std::string, std::string>& parameters) {
    QStringList parts;
    for (const auto& [key, value] : parameters) {
        parts << QString::fromStdString(key + "=" + value);
    }
    return parts.join("\n");
}

std::map<std::string, std::string> RuleConfigPage::parseParameters(const QString& text) {
    std::map<std::string, std::string> params;
    for (const auto& rawPart : text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts)) {
        const auto part = rawPart.trimmed();
        const int eq = part.indexOf('=');
        if (eq <= 0) continue;
        const auto key = part.left(eq).trimmed().toStdString();
        const auto value = part.mid(eq + 1).trimmed().toStdString();
        if (!key.empty()) params[key] = value;
    }
    return params;
}

} // namespace gisqc
