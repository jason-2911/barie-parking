#pragma once
#include <QWidget>

class QLabel;

/**
 * @brief Status title bar shown directly under the navy TopBar.
 *
 * Each screen owns its own TitleBar instance. The colour and label change
 * with the screen state (payment in progress, success, failure).
 */
class TitleBar : public QWidget {
    Q_OBJECT
public:
    enum class Variant { Payment, Success, Failed };

    explicit TitleBar(Variant v, QWidget* parent = nullptr);
    void setVariant(Variant v);
    void setText(const QString& text);

private:
    void apply();

    Variant variant_;
    QLabel* lbl_ = nullptr;
};
