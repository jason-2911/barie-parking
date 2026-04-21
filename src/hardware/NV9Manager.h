/**
 * NV9Manager.h
 * Cash-validator orchestrator. Owns an INV9Validator and re-emits
 * driver events as Qt signals for the Application layer.
 */

#pragma once

#include <QObject>
#include <memory>
#include "app/Config.h"
#include "hardware/INV9Validator.h"

class NV9Manager : public QObject {
    Q_OBJECT
public:
    explicit NV9Manager(const Config& cfg, QObject* parent = nullptr);
    ~NV9Manager() override;

    void start();
    void stop();
    void enableAccept();
    void disableAccept();
    void rejectNote();

    // Demo helper — lets UI inject a fake credit event on macOS.
    void simulateCash(quint32 amount);

signals:
    void cashInserted(quint32 amount);
    void errorOccurred(const QString& error);

private:
    const Config&                 cfg_;
    std::unique_ptr<INV9Validator> driver_;
    bool                          running_ = false;
};
