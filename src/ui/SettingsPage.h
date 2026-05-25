#pragma once

#include <QLabel>
#include <QWidget>

namespace gisqc {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

private:
    QLabel* envLabel_{};
    QLabel* appInfoLabel_{};
    QLabel* pathsLabel_{};
};

} // namespace gisqc
