// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETQRIMAGEPROVIDER_H
#define BITCOIN_QML_WALLET_WALLETQRIMAGEPROVIDER_H
#include <QQuickImageProvider>

class WalletQRImageProvider : public QQuickImageProvider
{
public:
    WalletQRImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;
    static QImage render(const QString& payload);
};
#endif // BITCOIN_QML_WALLET_WALLETQRIMAGEPROVIDER_H
