// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinurimodel.h>
#include <qml/backendexecutor.h>

#include <qml/models/bitcoinuri.h>

#include <QFile>
#include <QUrl>
#include <QVariantMap>

namespace {
QVariantMap BuildBitcoinUriResultMap(const BitcoinUriParseResult& r)
{
    return {
        {QStringLiteral("success"),    r.success},
        {QStringLiteral("error"),      r.error},
        {QStringLiteral("address"),    r.address},
        {QStringLiteral("amountSats"), static_cast<qlonglong>(r.amount_sats)},
        {QStringLiteral("hasAmount"),  r.has_amount},
        {QStringLiteral("label"),      r.label},
        {QStringLiteral("hasLabel"),   r.has_label},
        // Key is "uriMessage" (not "message") to avoid shadowing the JavaScript
        // built-in Error.message property when this map is used in QML.
        {QStringLiteral("uriMessage"), r.message},
        {QStringLiteral("hasMessage"), r.has_message},
    };
}
} // namespace

BitcoinUriModel::BitcoinUriModel(QObject* parent)
    : QObject(parent), m_executor(std::make_shared<BackendExecutor>())
{
    connect(m_executor.get(), &BackendExecutor::drained, this, &BitcoinUriModel::shutdownFinished);
}

void BitcoinUriModel::beginShutdown()
{
    if (m_executor->isDrained()) Q_EMIT shutdownFinished();
    else m_executor->shutdown();
}

QVariantMap BitcoinUriModel::parseBitcoinUri(const QString& uri_text)
{
    return BuildBitcoinUriResultMap(BitcoinUri::Parse(uri_text));
}

quint64 BitcoinUriModel::parseBitcoinUriFromFile(const QString& source_path)
{
    const auto request = ++m_next_request;
    const bool accepted = m_executor->submit(this, [source_path] {
        constexpr qint64 MAX_FILE_SIZE = 1024 * 1024; // 1 MiB

        // Accept either a local file path or a file:// URL (e.g. from a DropArea).
        // QUrl::toLocalFile() handles the platform-specific conversion correctly,
        // including the extra leading slash in file:///C:/path on Windows.
        QString local_path = source_path;
        const QUrl url(source_path);
        if (url.isLocalFile()) {
            local_path = url.toLocalFile();
        }

        QFile file(local_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            BitcoinUriParseResult err;
            err.error = tr("Cannot open file: %1").arg(file.errorString());
            return BuildBitcoinUriResultMap(err);
        }
        if (file.size() > MAX_FILE_SIZE) {
            BitcoinUriParseResult err;
            err.error = tr("File is too large to be a payment URI.");
            return BuildBitcoinUriResultMap(err);
        }
        const auto bytes = file.read(MAX_FILE_SIZE + 1);
        if (bytes.size() > MAX_FILE_SIZE || file.error() != QFileDevice::NoError) {
            BitcoinUriParseResult err;
            err.error = tr("Could not read the complete payment URI file.");
            return BuildBitcoinUriResultMap(err);
        }
        const QString content = QString::fromUtf8(bytes).trimmed();
        return BuildBitcoinUriResultMap(BitcoinUri::Parse(content));
    }, [this, request](const QVariantMap& result) { Q_EMIT fileParsed(request, result); },
       [this, request](std::exception_ptr) { Q_EMIT fileParsed(request, {{QStringLiteral("success"), false}, {QStringLiteral("error"), tr("Could not read the payment URI file.")}}); });
    return accepted ? request : 0;
}
