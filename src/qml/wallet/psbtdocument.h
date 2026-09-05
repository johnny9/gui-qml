// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_PSBTDOCUMENT_H
#define BITCOIN_QML_WALLET_PSBTDOCUMENT_H

#include <psbt.h>
#include <QByteArray>
#include <QString>
#include <optional>

/** Original binary encoding is retained, including ordering of unknown fields. */
struct PsbtDocument {
    PartiallySignedTransaction current;
    QByteArray original;
    bool modified{false};
    QByteArray serialized() const;
};

inline constexpr qint64 PSBT_FILE_LIMIT{100 * 1024 * 1024};
std::optional<PsbtDocument> DecodePsbtDocument(const QByteArray& data, QString& error);
std::optional<PsbtDocument> ReadPsbtDocument(const QString& path, QString& error);
bool WritePsbtDocument(const PsbtDocument& document, const QString& path, QString& error);

#endif // BITCOIN_QML_WALLET_PSBTDOCUMENT_H
