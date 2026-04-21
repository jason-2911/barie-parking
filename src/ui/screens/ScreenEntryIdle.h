#pragma once
#include <QWidget>
#include "app/Config.h"
class ScreenEntryIdle : public QWidget {
    Q_OBJECT
public:
    explicit ScreenEntryIdle(const Config& cfg, QWidget* parent = nullptr);
signals:
    void callSupportRequested();
private:
    const Config& cfg_;
};
