#include "MainWindow.h"

#include "DashboardPage.h"
#include "ExecutionPage.h"
#include "HomePage.h"
#include "ResultsPage.h"
#include "RuleConfigPage.h"
#include "SettingsPage.h"
#include "TemplateManagePage.h"

#include <QApplication>
#include <QDockWidget>
#include <QIcon>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include <iterator>

namespace gisqc {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("GIS \u6570\u636e\u8d28\u68c0\u5de5\u4f5c\u53f0");
    setWindowIcon(QIcon(":/app_icon.png"));
    resize(1280, 800);

    setupMenuBar();

    auto* root = new QWidget(this);
    root->setObjectName("AppBody");
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createSidebar());

    auto* content = new QWidget(root);
    content->setObjectName("ContentArea");
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 22, 28, 18);
    contentLayout->setSpacing(14);
    contentLayout->addWidget(createHeader());

    pages_ = new QStackedWidget(content);

    homePage_ = new HomePage(content);
    auto* dataImportPage = new DashboardPage(content);
    auto* ruleConfigPage = new RuleConfigPage(content);
    executionPage_ = new ExecutionPage(content);
    resultsPage_ = new ResultsPage(content);
    auto* templatePage = new TemplateManagePage(content);
    auto* settingsPage = new SettingsPage(content);

    // HomePage signals
    connect(homePage_, &HomePage::requestNewTask, this, [this]() { navigateToPage(1); });
    connect(homePage_, &HomePage::requestRuleConfig, this, [this]() { navigateToPage(2); });
    connect(homePage_, &HomePage::requestResults, this, [this]() { navigateToPage(4); });
    connect(homePage_, &HomePage::requestTemplateManage, this, [this]() { navigateToPage(5); });

    // DashboardPage (data import) signals
    connect(dataImportPage, &DashboardPage::datasetPathChanged, this, &MainWindow::rememberDatasetPath);
    connect(dataImportPage, &DashboardPage::requestRuleConfig, this, &MainWindow::openRuleConfigFromDashboard);
    connect(dataImportPage, &DashboardPage::requestExecution, this, &MainWindow::openExecutionFromDashboard);
    connect(dataImportPage, &DashboardPage::logMessage, this, &MainWindow::appendGlobalLog);

    // ExecutionPage signals
    connect(executionPage_, &ExecutionPage::logMessage, this, &MainWindow::appendGlobalLog);
    connect(executionPage_, &ExecutionPage::reportReady, this, &MainWindow::handleTaskReport);

    pages_->addWidget(homePage_);         // 0 - 首页
    pages_->addWidget(dataImportPage);    // 1 - 数据导入
    pages_->addWidget(ruleConfigPage);    // 2 - 规则配置
    pages_->addWidget(executionPage_);    // 3 - 执行质检
    pages_->addWidget(resultsPage_);      // 4 - 结果中心
    pages_->addWidget(templatePage);      // 5 - 模板管理
    pages_->addWidget(settingsPage);      // 6 - 系统设置
    contentLayout->addWidget(pages_, 1);

    rootLayout->addWidget(content, 1);
    setCentralWidget(root);

    // Log dock
    logDock_ = new QDockWidget("执行日志", this);
    logDock_->setObjectName("LogDock");
    logDock_->setWidget(createLogDock());
    logDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock_->hide();
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);

    // Status bar
    statusLabel_ = new QLabel("就绪", this);
    statusLabel_->setObjectName("StatusLabel");
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->setObjectName("AppStatusBar");

    connect(navList_, &QListWidget::currentRowChanged, this, &MainWindow::switchPage);
    navList_->setCurrentRow(0);
}

void MainWindow::setupMenuBar() {
    auto* mb = menuBar();
    mb->setObjectName("AppMenuBar");

    auto* fileMenu = mb->addMenu(QStringLiteral("\u6587\u4ef6(&F)"));
    fileMenu->addAction(QStringLiteral("\u65b0\u5efa\u8d28\u68c0\u4efb\u52a1"), this, [this]() { navigateToPage(1); });
    fileMenu->addAction(QStringLiteral("\u6253\u5f00\u6210\u679c\u76ee\u5f55..."));
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("\u9000\u51fa(&X)"), qApp, &QApplication::quit, QKeySequence::Quit);

    auto* viewMenu = mb->addMenu(QStringLiteral("\u89c6\u56fe(&V)"));
    viewMenu->addAction(QStringLiteral("\u9996\u9875"), this, [this]() { navigateToPage(0); });
    viewMenu->addAction(QStringLiteral("\u6570\u636e\u5bfc\u5165"), this, [this]() { navigateToPage(1); });
    viewMenu->addAction(QStringLiteral("\u89c4\u5219\u914d\u7f6e"), this, [this]() { navigateToPage(2); });
    viewMenu->addAction(QStringLiteral("\u6267\u884c\u8d28\u68c0"), this, [this]() { navigateToPage(3); });
    viewMenu->addAction(QStringLiteral("\u7ed3\u679c\u4e2d\u5fc3"), this, [this]() { navigateToPage(4); });
    viewMenu->addSeparator();
    auto* logAction = viewMenu->addAction(QStringLiteral("\u6267\u884c\u65e5\u5fd7"));
    logAction->setCheckable(true);
    connect(logAction, &QAction::toggled, this, &MainWindow::toggleLogDock);

    auto* toolsMenu = mb->addMenu(QStringLiteral("\u5de5\u5177(&T)"));
    toolsMenu->addAction(QStringLiteral("\u6a21\u677f\u7ba1\u7406"), this, [this]() { navigateToPage(5); });
    toolsMenu->addAction(QStringLiteral("\u7cfb\u7edf\u8bbe\u7f6e"), this, [this]() { navigateToPage(6); });

    auto* helpMenu = mb->addMenu(QStringLiteral("\u5e2e\u52a9(&H)"));
    helpMenu->addAction(QStringLiteral("\u5173\u4e8e GIS \u8d28\u68c0\u5de5\u4f5c\u53f0"));
}

QWidget* MainWindow::createSidebar() {
    auto* side = new QWidget(this);
    side->setObjectName("Sidebar");
    side->setFixedWidth(220);
    auto* layout = new QVBoxLayout(side);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(4);

    auto* logo = new QLabel("GIS QC", side);
    logo->setObjectName("Logo");
    layout->addWidget(logo);

    auto* logoSub = new QLabel("Desktop Workbench", side);
    logoSub->setObjectName("LogoSub");
    layout->addWidget(logoSub);
    layout->addSpacing(20);

    navList_ = new QListWidget(side);
    navList_->setObjectName("NavList");
    navList_->addItems({
        QStringLiteral("  \u9996\u9875"),
        QStringLiteral("  \u6570\u636e\u5bfc\u5165"),
        QStringLiteral("  \u89c4\u5219\u914d\u7f6e"),
        QStringLiteral("  \u6267\u884c\u8d28\u68c0"),
        QStringLiteral("  \u7ed3\u679c\u4e2d\u5fc3"),
        QStringLiteral("  \u6a21\u677f\u7ba1\u7406"),
        QStringLiteral("  \u7cfb\u7edf\u8bbe\u7f6e")
    });
    layout->addWidget(navList_, 1);
    return side;
}

QWidget* MainWindow::createHeader() {
    auto* header = new QWidget(this);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    auto* titleBlock = new QWidget(header);
    auto* titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);

    pageTitle_ = new QLabel("任务工作台 / 数据导入", header);
    pageTitle_->setObjectName("PageTitle");
    titleLayout->addWidget(pageTitle_);

    pageSubtitle_ = new QLabel("", header);
    pageSubtitle_->setObjectName("PageSubtitle");
    titleLayout->addWidget(pageSubtitle_);

    layout->addWidget(titleBlock, 1);

    auto* templateButton = new QPushButton("当前模板：通用标准化质检 V2.0", header);
    templateButton->setObjectName("TemplateButton");
    layout->addWidget(templateButton);
    return header;
}

QWidget* MainWindow::createLogDock() {
    logView_ = new QTextEdit(this);
    logView_->setObjectName("LogView");
    logView_->setReadOnly(true);
    logView_->setText("[15:32:10] 已加载通用标准化质检模板 V2.0\n[15:32:14] 等待选择成果目录...");
    return logView_;
}

void MainWindow::switchPage(int index) {
    if (!pages_) {
        return;
    }
    pages_->setCurrentIndex(index);
    updateHeaderForPage(index);
}

void MainWindow::updateHeaderForPage(int index) {
    if (!pageTitle_ || !pageSubtitle_) {
        return;
    }

    struct HeaderText {
        const char* title;
        const char* subtitle;
    };
    static const HeaderText headers[] = {
        {"首页", "新建任务、选择成果数据并进入规则配置或执行质检"},
        {"数据导入", "扫描 FileGDB、Shapefile、GeoPackage，识别图层、要素数和坐标系"},
        {"规则配置", "规则启停、参数、严重级别全部在软件内维护，不再依赖 Excel 配置表"},
        {"执行质检", "加载内部规则配置，对成果目录执行完整质检"},
        {"结果中心", "查看问题清单、统计通过率，导出 CSV / HTML / Excel / Word 报告"},
        {"模板管理", "维护项目规则模板和默认业务参数"},
        {"系统设置", "查看运行环境、GIS 库状态和交付包配置"}
    };
    if (index >= 0 && index < static_cast<int>(std::size(headers))) {
        pageTitle_->setText(headers[index].title);
        pageSubtitle_->setText(headers[index].subtitle);
    }
}

void MainWindow::rememberDatasetPath(const QString& path) {
    currentDatasetPath_ = path;
    if (executionPage_) {
        executionPage_->setDatasetPath(path);
    }
}

void MainWindow::openRuleConfigFromDashboard() {
    if (navList_) {
        navList_->setCurrentRow(2);
    } else if (pages_) {
        switchPage(2);
    }
}

void MainWindow::openExecutionFromDashboard() {
    if (executionPage_ && !currentDatasetPath_.isEmpty()) {
        executionPage_->setDatasetPath(currentDatasetPath_);
    }
    if (navList_) {
        navList_->setCurrentRow(3);
    } else if (pages_) {
        switchPage(3);
    }
}

void MainWindow::navigateToPage(int index) {
    if (navList_) {
        navList_->setCurrentRow(index);
    } else if (pages_) {
        switchPage(index);
    }
}

void MainWindow::appendGlobalLog(const QString& message) {
    if (logView_) {
        logView_->append(message);
    }
    if (statusLabel_) {
        statusLabel_->setText(message);
    }
}

void MainWindow::toggleLogDock() {
    if (logDock_) {
        logDock_->setVisible(!logDock_->isVisible());
    }
}

void MainWindow::handleTaskReport(const TaskSessionReport& report) {
    if (resultsPage_) {
        resultsPage_->showReport(report);
    }
    if (pages_ && resultsPage_) {
        pages_->setCurrentWidget(resultsPage_);
    }
    if (navList_) {
        navList_->setCurrentRow(4);
    }
    appendGlobalLog("结果中心已更新为本次真实质检报告。");
}

} // namespace gisqc
