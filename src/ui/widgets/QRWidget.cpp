#include "ui/widgets/QRWidget.h"
#include "ui/Theme.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSslConfiguration>
#include <QUrl>

extern "C" {
#include <qrencode.h>
}

QRWidget::QRWidget(QWidget* parent) : QWidget(parent) {
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setAttribute(Qt::WA_StyledBackground, false);
    setStyleSheet("");

    net_ = new QNetworkAccessManager(this);
    connect(net_, &QNetworkAccessManager::finished,
            this, &QRWidget::onImageReply);
}

QRWidget::~QRWidget() {
    if (pending_) {
        pending_->abort();
        pending_->deleteLater();
        pending_ = nullptr;
    }
}

static bool looksLikeHttpUrl(const QString& s) {
    return s.startsWith("http://", Qt::CaseInsensitive)
        || s.startsWith("https://", Qt::CaseInsensitive);
}

void QRWidget::setUrl(const QString& url) {
    url_      = url;
    has_qr_   = !url.isEmpty();
    is_image_ = has_qr_ && looksLikeHttpUrl(url);
    image_    = QImage();

    if (is_image_) {
        fetchImage(url);
    }
    update();
}

void QRWidget::fetchImage(const QString& url) {
    if (pending_) {
        pending_->abort();
        pending_->deleteLater();
        pending_ = nullptr;
    }
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::NoLessSafeRedirectPolicy);

    // Demo backend runs with a self-signed cert. In production swap this
    // out for a real CA-pinned config.
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(ssl);

    pending_ = net_->get(req);
    connect(pending_, &QNetworkReply::sslErrors,
            pending_, qOverload<const QList<QSslError>&>(&QNetworkReply::ignoreSslErrors));
}

void QRWidget::onImageReply(QNetworkReply* reply) {
    if (reply != pending_) {
        reply->deleteLater();
        return;
    }
    pending_ = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[QRWidget] fetch failed:" << reply->errorString()
                   << reply->url();
        image_ = QImage();
    } else {
        QImage img;
        if (img.loadFromData(reply->readAll())) {
            image_ = std::move(img);
        } else {
            qWarning() << "[QRWidget] could not decode image from"
                       << reply->url();
            image_ = QImage();
        }
    }
    reply->deleteLater();
    update();
}

void QRWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int   border = theme::BORDER_W;
    const QRect full   = rect();
    const int   side   = qMin(full.width(), full.height());
    const QRect square(full.left() + (full.width()  - side) / 2,
                        full.top()  + (full.height() - side) / 2,
                        side, side);
    const QRect card = square.adjusted(border, border, -border, -border);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(theme::HEAVY_BORDER));
    p.drawRect(square);
    p.setBrush(QColor(Qt::white));
    p.drawRect(card);

    if (!has_qr_ || url_.isEmpty()) {
        p.setPen(QColor("#94a3b8"));
        p.setFont(QFont("Helvetica Neue", 12));
        p.drawText(card, Qt::AlignCenter, "QR Code\n(Chưa có URL)");
        return;
    }

    if (is_image_) {
        if (image_.isNull()) {
            p.setPen(QColor("#94a3b8"));
            p.setFont(QFont("Helvetica Neue", 12));
            p.drawText(card, Qt::AlignCenter,
                       pending_ ? "Đang tải QR..." : "Không tải được QR");
            return;
        }
        renderImage(p, card);
        return;
    }

    renderLocalQR(p, card);
}

void QRWidget::renderImage(QPainter& p, const QRect& card) {
    // Preserve aspect ratio and center inside the card (8 px inner margin
    // so the QR never touches the black border).
    const int inner = 8;
    const QRect target = card.adjusted(inner, inner, -inner, -inner);
    const QSize scaled = image_.size().scaled(target.size(),
                                               Qt::KeepAspectRatio);
    const QRect dst(target.left() + (target.width()  - scaled.width())  / 2,
                     target.top()  + (target.height() - scaled.height()) / 2,
                     scaled.width(), scaled.height());
    p.drawImage(dst, image_);
}

void QRWidget::renderLocalQR(QPainter& p, const QRect& card) {
    QRcode* qr = QRcode_encodeString(url_.toUtf8().constData(),
                                      0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) {
        p.setPen(Qt::red);
        p.drawText(card, Qt::AlignCenter, "QR Error");
        return;
    }

    const int size   = qr->width;
    const int inner  = 8;
    const int avail  = qMax(size, qMin(card.width(), card.height()) - inner * 2);
    const int cell   = qMax(1, avail / size);
    const int drawn  = cell * size;
    const int ox     = card.left() + (card.width()  - drawn) / 2;
    const int oy     = card.top()  + (card.height() - drawn) / 2;

    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (qr->data[y * size + x] & 1) {
                p.drawRect(QRect(ox + x * cell, oy + y * cell, cell, cell));
            }
        }
    }

    QRcode_free(qr);
}
