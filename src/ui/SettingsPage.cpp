#include "SettingsPage.h"

#include "../core/RuleTemplateStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSysInfo>
#include <QVBoxLayout>

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

} // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("Page");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);

    // Environment card
    auto* envCard = makeCard(this);
    auto* envLayout = qobject_cast<QVBoxLayout*>(envCard->layout());
    auto* envTitle = new QLabel("运行环境", envCard);
    envTitle->setObjectName("CardTitle");
    envLayout->addWidget(envTitle);

    envLabel_ = new QLabel(envCard);
    envLabel_->setWordWrap(true);

    QString gdalStatus = "未检测到";
#ifdef GISQC_HAVE_GDAL
    gdalStatus = "已加载";
#endif

    const QString appDir = QCoreApplication::applicationDirPath();
    auto detectLib = [&](const QStringList& candidates) -> QString {
        for (const auto& name : candidates) {
            if (QFileInfo::exists(appDir + "/" + name)) return "已加载";
        }
        return "未检测到";
    };
    const QString geosStatus = detectLib({"geos_c.dll", "geos.dll"});
    const QString projStatus = detectLib({"proj_9.dll", "proj.dll"});
    const QString sqliteStatus = detectLib({"sqlite3.dll"});

    envLabel_->setText(
        QString("操作系统：%1 %2\n"
                "CPU 架构：%3\n"
                "Qt 版本：%4\n\n"
                "GDAL：%5\n"
                "GEOS：%6\n"
                "PROJ：%7\n"
                "SQLite：%8")
            .arg(QSysInfo::prettyProductName(),
                 QSysInfo::kernelVersion(),
                 QSysInfo::currentCpuArchitecture(),
                 qVersion(),
                 gdalStatus,
                 geosStatus,
                 projStatus,
                 sqliteStatus));
    envLayout->addWidget(envLabel_);
    envLayout->addStretch();
    layout->addWidget(envCard);

    // App info card
    auto* appCard = makeCard(this);
    auto* appLayout = qobject_cast<QVBoxLayout*>(appCard->layout());
    auto* appTitle = new QLabel("应用信息", appCard);
    appTitle->setObjectName("CardTitle");
    appLayout->addWidget(appTitle);

    appInfoLabel_ = new QLabel(appCard);
    appInfoLabel_->setWordWrap(true);
    appInfoLabel_->setText(
        QString("应用名称：GIS 数据质检工作台\n"
                "版本号：0.1.0\n"
                "构建日期：%1\n"
                "工作目录：%2")
            .arg(QString(__DATE__),
                 QDir::currentPath()));
    appLayout->addWidget(appInfoLabel_);
    appLayout->addStretch();
    layout->addWidget(appCard);

    // Paths card
    auto* pathsCard = makeCard(this);
    auto* pathsLayout = qobject_cast<QVBoxLayout*>(pathsCard->layout());
    auto* pathsTitle = new QLabel("数据路径", pathsCard);
    pathsTitle->setObjectName("CardTitle");
    pathsLayout->addWidget(pathsTitle);

    RuleTemplateStore store;
    pathsLabel_ = new QLabel(pathsCard);
    pathsLabel_->setWordWrap(true);
    pathsLabel_->setText(
        QString("默认模板路径：%1\n"
                "用户配置路径：%2\n"
                "活跃模板路径：%3\n"
                "历史记录目录：data/history/")
            .arg(QString::fromStdString(store.defaultTemplatePath()),
                 QString::fromStdString(store.userTemplatePath()),
                 QString::fromStdString(store.activeTemplatePath())));
    pathsLayout->addWidget(pathsLabel_);
    pathsLayout->addStretch();
    layout->addWidget(pathsCard);

    layout->addStretch();
}

} // namespace gisqc
