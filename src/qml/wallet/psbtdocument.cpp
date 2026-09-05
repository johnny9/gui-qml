// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtdocument.h>
#include <streams.h>
#include <util/result.h>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUrl>

namespace {
QString LocalPath(const QString& path)
{
    // Native Windows drive paths must not be mistaken for URL schemes.
    if (QDir::isAbsolutePath(path)) return path;
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? path : QString{};
}
}

QByteArray PsbtDocument::serialized() const
{
    if (!modified) return original;
    DataStream stream;
    stream << current;
    return QByteArray::fromStdString(stream.str());
}

std::optional<PsbtDocument> DecodePsbtDocument(const QByteArray& data, QString& error)
{
    error.clear();
    if (data.isEmpty() || data.size() > PSBT_FILE_LIMIT) {
        error = QObject::tr("The PSBT is empty or exceeds the 100 MiB limit.");
        return std::nullopt;
    }
    QByteArray binary = data;
    if (!data.startsWith(QByteArray("psbt\xff", 5))) {
        const auto decoded = QByteArray::fromBase64Encoding(data.trimmed(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            error = QObject::tr("The file is not a binary or base64 PSBT.");
            return std::nullopt;
        }
        binary = *decoded;
    }
    auto result = DecodeRawPSBT(std::as_bytes(std::span{binary.constData(), size_t(binary.size())}));
    if (!result) {
        error = QObject::tr("Invalid or unsupported PSBT: %1").arg(QString::fromStdString(util::ErrorString(result).original));
        return std::nullopt;
    }
    return PsbtDocument{std::move(*result), std::move(binary)};
}

std::optional<PsbtDocument> ReadPsbtDocument(const QString& path, QString& error)
{
    QFile file(LocalPath(path));
    if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly)) {
        error = QObject::tr("Could not open the PSBT file.");
        return std::nullopt;
    }
    if (file.size() > PSBT_FILE_LIMIT) {
        error = QObject::tr("The PSBT exceeds the 100 MiB limit.");
        return std::nullopt;
    }
    const QByteArray data = file.read(PSBT_FILE_LIMIT + 1);
    if (file.error() != QFile::NoError) {
        error = QObject::tr("Could not read the PSBT file.");
        return std::nullopt;
    }
    return DecodePsbtDocument(data, error);
}

bool WritePsbtDocument(const PsbtDocument& document, const QString& path, QString& error)
{
    error.clear();
    QSaveFile file(LocalPath(path));
    const auto bytes = document.serialized();
    if (file.fileName().isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QObject::tr("Could not save the PSBT file. The in-memory PSBT is unchanged.");
        return false;
    }
    return true;
}
