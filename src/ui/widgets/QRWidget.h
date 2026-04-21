#pragma once
#include <QWidget>
#include <QImage>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * QRWidget — shows a VN payment QR code in the hourly-payment screen.
 *
 * Two rendering modes, picked from the URL passed to setUrl():
 *   - "http(s)://…" → fetch the PNG from the server (VietQR image hosted
 *                      on our HTTPS endpoint) and blit it onto the card.
 *   - anything else → qrencode the string locally. Kept as a fallback
 *                      for dev setups without a QR backend.
 *
 * The widget is always square (heightForWidth = width), so neither the
 * rendered QR nor the downloaded PNG ever gets stretched/cropped.
 */
class QRWidget : public QWidget {
    Q_OBJECT
public:
    explicit QRWidget(QWidget* parent = nullptr);
    ~QRWidget() override;

    // URL may be a VietQR payload string (qrencoded locally) OR an
    // http(s) URL pointing to a PNG (downloaded via HTTPS).
    void setUrl(const QString& url);

    // Square aspect, regardless of parent shape.
    bool hasHeightForWidth() const override { return true; }
    int  heightForWidth(int w) const override { return w; }
    QSize sizeHint() const override { return {240, 240}; }
    QSize minimumSizeHint() const override { return {120, 120}; }

protected:
    void paintEvent(QPaintEvent*) override;

private slots:
    void onImageReply(QNetworkReply* reply);

private:
    void renderLocalQR(class QPainter& p, const QRect& card);
    void renderImage (class QPainter& p, const QRect& card);
    void fetchImage(const QString& url);

    QString                url_;
    bool                   has_qr_   = false;
    bool                   is_image_ = false;   // `url_` is http(s)://…
    QImage                 image_;               // decoded PNG (when is_image_)
    QNetworkAccessManager* net_      = nullptr;
    QNetworkReply*         pending_  = nullptr;
};
