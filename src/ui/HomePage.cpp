#include "HomePage.h"

#include "../core/TaskHistoryStore.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace gisqc {

namespace {

QFrame* makeCard(QWidget* parent = nullptr) {
    auto* card = new QFrame(parent);
    card->setObjectName("Card");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);
    return card;
}

QFrame* makeStatCard(const QString& title, const QString& valueObjName, QLabel** out, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("StatCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("StatTitle");
    layout->addWidget(titleLabel);

    *out = new QLabel("--", card);
    (*out)->setObjectName(valueObjName);
    layout->addWidget(*out);
    layout->addStretch();
    return card;
}

QFrame* makeActionCard(const QString& icon, const QString& title, const QString& subtitle,
                       const QString& buttonText, QWidget* parent) {
    auto* card = makeCard(parent);
    auto* layout = qobject_cast<QVBoxLayout*>(card->layout());

    if (!icon.isEmpty()) {
        auto* iconLabel = new QLabel(icon, card);
        iconLabel->setObjectName("ActionCardIcon");
        layout->addWidget(iconLabel);
    }

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("CardTitle");
    layout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(subtitle, card);
    subtitleLabel->setObjectName("CardSubtitle");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);
    layout->addStretch();

    auto* button = new QPushButton(buttonText, card);
    button->setObjectName("PrimaryButton");
    button->setProperty("actionRole", title);
    layout->addWidget(button);
    return card;
}

} // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ---- Band 1: Welcome banner ----
    auto* banner = new QFrame(this);
    banner->setObjectName("WelcomeBanner");
    auto* bannerLayout = new QVBoxLayout(banner);
    bannerLayout->setContentsMargins(16, 12, 16, 12);
    bannerLayout->setSpacing(4);
    auto* welcomeTitle = new QLabel("欢迎使用 GIS 数据质检工作台", banner);
    welcomeTitle->setObjectName("WelcomeTitle");
    bannerLayout->addWidget(welcomeTitle);
    layout->addWidget(banner);

    // ---- Band 2: Stats row ----
    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(12);
    statsRow->addWidget(makeStatCard("累计任务数", "StatValue", &taskCountLabel_, this));
    statsRow->addWidget(makeStatCard("累计问题数", "StatValueOrange", &issueCountLabel_, this));
    statsRow->addWidget(makeStatCard("平均通过率", "StatValueGreen", &passRateLabel_, this));
    layout->addLayout(statsRow);

    // ---- Band 3: Quick actions ----
    auto* actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(12);

    auto* newTaskCard = makeActionCard("\u25B6", "新建质检任务",
        "选择成果目录，扫描 GIS 数据源", "开始导入", this);
    actionsRow->addWidget(newTaskCard);

    auto* ruleCard = makeActionCard("\u2699", "配置质检规则",
        "管理规则启停、参数与严重级别", "打开配置", this);
    actionsRow->addWidget(ruleCard);

    auto* resultCard = makeActionCard("\u25C8", "查看质检结果",
        "问题清单、统计与报告导出", "结果中心", this);
    actionsRow->addWidget(resultCard);


    layout->addLayout(actionsRow);

    // Connect action buttons
    for (auto* card : {newTaskCard, ruleCard, resultCard}) {
        auto* button = card->findChild<QPushButton*>();
        if (button) {
            const QString role = button->property("actionRole").toString();
            if (role == "新建质检任务") {
                connect(button, &QPushButton::clicked, this, &HomePage::requestNewTask);
            } else if (role == "配置质检规则") {
                connect(button, &QPushButton::clicked, this, &HomePage::requestRuleConfig);
            } else if (role == "查看质检结果") {
                connect(button, &QPushButton::clicked, this, &HomePage::requestResults);
            }
        }
    }

    // ---- Band 4: Recent tasks table ----
    auto* recentCard = makeCard(this);
    auto* recentLayout = qobject_cast<QVBoxLayout*>(recentCard->layout());
    auto* recentTitle = new QLabel("最近质检任务", recentCard);
    recentTitle->setObjectName("CardTitle");
    recentLayout->addWidget(recentTitle);

    recentTable_ = new QTableWidget(0, 5, recentCard);
    recentTable_->setHorizontalHeaderLabels({"时间", "任务名称", "数据源数", "问题数", "通过率"});
    recentTable_->horizontalHeader()->setStretchLastSection(true);
    recentTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    recentTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    recentTable_->verticalHeader()->setVisible(false);
    recentTable_->setAlternatingRowColors(true);
    recentTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recentTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    recentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(recentTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        emit requestResults();
    });
    recentLayout->addWidget(recentTable_, 1);
    layout->addWidget(recentCard, 1);

    renderStats();
    renderRecentTasks();
}

void HomePage::refreshData() {
    renderStats();
    renderRecentTasks();
}

void HomePage::renderStats() {
    TaskHistoryStore store;
    const auto allRecords = store.all();

    const int totalTasks = static_cast<int>(allRecords.size());
    int totalIssues = 0;
    double avgPassRate = 0.0;
    for (const auto& r : allRecords) {
        totalIssues += r.issueCount;
        std::string rateStr = r.passRateText;
        if (!rateStr.empty() && rateStr.back() == '%') {
            rateStr.pop_back();
        }
        try {
            avgPassRate += std::stod(rateStr);
        } catch (...) {}
    }
    if (totalTasks > 0) {
        avgPassRate /= totalTasks;
    }

    if (taskCountLabel_) taskCountLabel_->setText(QString::number(totalTasks));
    if (issueCountLabel_) issueCountLabel_->setText(QString::number(totalIssues));
    if (passRateLabel_) passRateLabel_->setText(totalTasks > 0 ? QString::number(avgPassRate, 'f', 1) + "%" : "--");
}

void HomePage::renderRecentTasks() {
    if (!recentTable_) return;

    TaskHistoryStore store;
    const auto records = store.latest(10);

    recentTable_->setRowCount(static_cast<int>(records.size()));
    for (int r = 0; r < static_cast<int>(records.size()); ++r) {
        const auto& record = records[static_cast<std::size_t>(r)];
        recentTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(record.timestamp)));
        recentTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(record.taskName)));
        recentTable_->setItem(r, 2, new QTableWidgetItem(QString::number(record.sourceCount)));
        recentTable_->setItem(r, 3, new QTableWidgetItem(QString::number(record.issueCount)));
        recentTable_->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(record.passRateText)));
    }
}

} // namespace gisqc
