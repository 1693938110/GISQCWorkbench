#include "../ui/MainWindow.h"
#include "../core/LicenseManager.h"
#include "../core/TaskSession.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMetaType>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStyleFactory>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

bool showLicenseRegistrationDialog(const gisqc::LicenseStatus& initialStatus) {
    while (true) {
        QDialog dialog;
        dialog.setWindowTitle(QStringLiteral("软件注册"));
        dialog.setWindowIcon(QIcon(":/app_icon.png"));
        dialog.resize(640, 460);

        auto* layout = new QVBoxLayout(&dialog);
        auto* title = new QLabel(QStringLiteral("GIS 数据质量检查工作台需要注册后使用"));
        QFont titleFont = title->font();
        titleFont.setPointSize(13);
        titleFont.setBold(true);
        title->setFont(titleFont);
        layout->addWidget(title);

        auto* message = new QLabel(QString::fromStdString(initialStatus.message));
        message->setWordWrap(true);
        layout->addWidget(message);

        layout->addWidget(new QLabel(QStringLiteral("本机机器码：")));
        auto* machineCode = new QLineEdit(QString::fromStdString(gisqc::LicenseManager::machineCode()));
        machineCode->setReadOnly(true);
        layout->addWidget(machineCode);

        auto* copyMachine = new QPushButton(QStringLiteral("复制机器码"));
        QObject::connect(copyMachine, &QPushButton::clicked, [&]() {
            QApplication::clipboard()->setText(machineCode->text());
            QMessageBox::information(&dialog, QStringLiteral("已复制"), QStringLiteral("机器码已复制到剪贴板。"));
        });
        layout->addWidget(copyMachine);

        layout->addWidget(new QLabel(QStringLiteral("请输入注册码 / 授权内容：")));
        auto* licenseText = new QTextEdit;
        licenseText->setPlaceholderText(QStringLiteral("把注册机生成的完整授权内容粘贴到这里，例如：\nproduct=GISQCWorkbench\nmachineCode=...\nexpireDate=...\nmaxRuns=...\nissuedAt=...\nsignature=..."));
        layout->addWidget(licenseText, 1);

        auto* pathLabel = new QLabel(QStringLiteral("授权将保存到：") + QString::fromStdString(gisqc::LicenseManager::defaultLicensePath().u8string()));
        pathLabel->setWordWrap(true);
        layout->addWidget(pathLabel);

        auto* buttons = new QDialogButtonBox;
        auto* registerButton = buttons->addButton(QStringLiteral("注册并启动"), QDialogButtonBox::AcceptRole);
        buttons->addButton(QStringLiteral("退出"), QDialogButtonBox::RejectRole);
        layout->addWidget(buttons);

        QObject::connect(registerButton, &QPushButton::clicked, [&]() {
            const auto text = licenseText->toPlainText().toStdString();
            if (text.empty()) {
                QMessageBox::warning(&dialog, QStringLiteral("注册失败"), QStringLiteral("请先粘贴注册码/授权内容。"));
                return;
            }
            if (!gisqc::LicenseManager::installLicenseText(text, gisqc::LicenseManager::defaultLicensePath())) {
                QMessageBox::critical(&dialog, QStringLiteral("注册失败"), QStringLiteral("注册码格式错误、产品不匹配或签名无效，请检查是否复制完整。"));
                return;
            }
            const auto check = gisqc::LicenseManager::check(gisqc::LicenseManager::defaultLicensePath(),
                                                            gisqc::LicenseManager::defaultStatePath(),
                                                            false);
            if (!check.valid) {
                QMessageBox::critical(&dialog, QStringLiteral("注册失败"), QString::fromStdString(check.message));
                return;
            }
            dialog.accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        return true;
    }
}

} // namespace

int main(int argc, char *argv[]) {
    qRegisterMetaType<gisqc::TaskSessionReport>("TaskSessionReport");

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/app_icon.png"));
    QApplication::setApplicationName("GIS 数据质检工作台");
    QApplication::setOrganizationName("GISQC");
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    auto license = gisqc::LicenseManager::check(gisqc::LicenseManager::defaultLicensePath(),
                                                gisqc::LicenseManager::defaultStatePath(),
                                                false);
    if (!license.valid) {
        if (!showLicenseRegistrationDialog(license)) {
            return 3;
        }
        license = gisqc::LicenseManager::check(gisqc::LicenseManager::defaultLicensePath(),
                                               gisqc::LicenseManager::defaultStatePath(),
                                               false);
        if (!license.valid) {
            QMessageBox::critical(nullptr, QStringLiteral("授权校验失败"), QString::fromStdString(license.message));
            return 3;
        }
    }

    // Count one successful GUI launch only after registration/validation succeeds.
    gisqc::LicenseManager::check(gisqc::LicenseManager::defaultLicensePath(),
                                 gisqc::LicenseManager::defaultStatePath(),
                                 true);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f1f5f9"));
    palette.setColor(QPalette::WindowText, QColor("#1e293b"));
    palette.setColor(QPalette::Base, QColor("#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor("#f8fafc"));
    palette.setColor(QPalette::Text, QColor("#1e293b"));
    palette.setColor(QPalette::Button, QColor("#ffffff"));
    palette.setColor(QPalette::ButtonText, QColor("#374151"));
    palette.setColor(QPalette::Highlight, QColor("#cbd5e1"));
    palette.setColor(QPalette::HighlightedText, QColor("#1e293b"));
    palette.setColor(QPalette::PlaceholderText, QColor("#94a3b8"));
    app.setPalette(palette);

    // Load QSS from filesystem first (avoids stale compiled resource)
    const QStringList qssCandidates = {
        QCoreApplication::applicationDirPath() + "/app.qss",
        QCoreApplication::applicationDirPath() + "/../src/resources/app.qss",
        QCoreApplication::applicationDirPath() + "/../../src/resources/app.qss",
        QDir::currentPath() + "/src/resources/app.qss",
    };
    bool qssLoaded = false;
    for (const auto& path : qssCandidates) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            app.setStyleSheet(QTextStream(&f).readAll());
            qssLoaded = true;
            break;
        }
    }
    if (!qssLoaded) {
        QFile qss(":/app.qss");
        if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
            app.setStyleSheet(QTextStream(&qss).readAll());
        }
    }

    gisqc::MainWindow window;
    window.resize(1440, 900);
    window.show();
    return app.exec();
}
