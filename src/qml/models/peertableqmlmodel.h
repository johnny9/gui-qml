// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERTABLEQMLMODEL_H
#define BITCOIN_QML_MODELS_PEERTABLEQMLMODEL_H

#include <bitcoin/src/qt/peertablemodel.h>

#include <QObject>

/**
 * QML-friendly wrapper for PeerTableModel that exposes methods as invokable
 * from QML. This allows QML code to call startAutoRefresh() and stopAutoRefresh().
 */
class PeerTableQmlModel : public PeerTableModel
{
    Q_OBJECT

public:
    explicit PeerTableQmlModel(interfaces::Node& node, QObject* parent = nullptr);

    Q_INVOKABLE void startAutoRefresh();
    Q_INVOKABLE void stopAutoRefresh();
};

#endif // BITCOIN_QML_MODELS_PEERTABLEQMLMODEL_H
