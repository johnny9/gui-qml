// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletqrimageprovider.h>
#include <bitcoin-build-config.h>
#include <QImage>
#ifdef USE_QRCODE
#include <qrencode.h>
#endif

QImage WalletQRImageProvider::render(const QString& payload)
{
#ifdef USE_QRCODE
    // Bound work and preserve a four-module quiet zone for reliable scanning.
    const QByteArray bytes{payload.toUtf8()};
    if (bytes.isEmpty() || bytes.size() > 2953) return {};
    QRcode* code{QRcode_encodeString(bytes.constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1)};
    if (!code) return {};
    constexpr int BORDER{4};
    QImage image{code->width + 2 * BORDER, code->width + 2 * BORDER, QImage::Format_RGB32};
    image.fill(Qt::white);
    for (int y = 0; y < code->width; ++y) {
        for (int x = 0; x < code->width; ++x) {
            image.setPixelColor(x + BORDER, y + BORDER, (code->data[y * code->width + x] & 1) ? Qt::black : Qt::white);
        }
    }
    QRcode_free(code);
    return image;
#else
    return {};
#endif
}

QImage WalletQRImageProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    const QImage image{render(QString::fromUtf8(QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors)))};
    if (size) *size = image.size();
    return image;
}
