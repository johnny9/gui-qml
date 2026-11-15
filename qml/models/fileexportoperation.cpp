// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/fileexportoperation.h>

#include <qml/psbtwork.h>
#include <qml/retainedvalue.h>

#include <QFile>
#include <mutex>
#include <utility>

namespace {
constexpr qsizetype MAX_PSBT_EXPORT_BYTES{100 * 1024 * 1024};

} // namespace

struct FileExportOperationModel::Source {
    std::mutex mutex;
    std::shared_ptr<std::shared_ptr<const PartiallySignedTransaction>> document;

    std::shared_ptr<const PartiallySignedTransaction> take()
    {
        std::lock_guard lock{mutex};
        return document ? std::exchange(*document, {}) : std::shared_ptr<const PartiallySignedTransaction>{};
    }
    void clear()
    {
        // Releasing this alias only activates its reserved Cleanup turn. The
        // retained PSBT itself is destroyed by the scheduler, never here.
        std::shared_ptr<std::shared_ptr<const PartiallySignedTransaction>> retired;
        {
            std::lock_guard lock{mutex};
            retired = std::move(document);
        }
        retired.reset();
    }
};

FileExportOperationModel::FileExportOperationModel(QString rawPath,
    const std::shared_ptr<const PartiallySignedTransaction>& document, backup::Services services, QObject* parent)
    : QObject(parent)
{
    if (!document) {
        m_start_error = tr("No captured PSBT is available to export.");
        return;
    }
    auto retained_document = execution::RetainValues<std::shared_ptr<const PartiallySignedTransaction>>(
        services.scheduler, [&document] { return std::make_unique<std::shared_ptr<const PartiallySignedTransaction>>(document); });
    if (!retained_document) {
        m_start_error = tr("PSBT export cleanup capacity is unavailable.");
        return;
    }
    m_source = std::make_shared<Source>();
    m_source->document = std::move(retained_document);
    if (!services.coordinator) services.coordinator = std::make_shared<backup::InstallCoordinator>();
    const auto coordinator = services.coordinator;
    const auto source = m_source;
    services.wallet_name = [] { return std::string{"transaction"}; };
    services.inspect = [](const std::string& raw, const std::string&) {
        return backup::Inspect(raw, "transaction", ".psbt");
    };
    services.write = [source, coordinator](const backup::Proposal& target, bool replace) {
        return backup::Write(target, replace, [source](const std::string& stagedPath) {
            const auto document = source->take();
            if (!document) return false;
            const auto bytes = psbtwork::SerializeRaw(*document);
            if (bytes.isEmpty() || bytes.size() > MAX_PSBT_EXPORT_BYTES) return false;
            const QString path = QString::fromUtf8(stagedPath.data(), static_cast<qsizetype>(stagedPath.size()));
            QFile file{path};
            if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(bytes) == bytes.size() && file.flush();
        }, *coordinator);
    };
    m_operation = new BackupFileOperation(std::move(rawPath), std::move(services), this);
    connect(m_operation, &BackupFileOperation::changed, this, [this] {
        if (!m_operation->pending() && m_source) m_source->clear();
        Q_EMIT changed();
    });
}

FileExportOperationModel::~FileExportOperationModel()
{
    retire();
}

bool FileExportOperationModel::pending() const
{
    return m_operation && m_operation->pending();
}

bool FileExportOperationModel::confirmationRequired() const
{
    return m_operation && m_operation->confirmationRequired();
}

bool FileExportOperationModel::replacing() const
{
    return m_operation && m_operation->replacing();
}

bool FileExportOperationModel::saved() const
{
    return m_operation && m_operation->succeeded();
}

QString FileExportOperationModel::finalPath() const
{
    return m_operation ? m_operation->finalPath() : QString{};
}

QString FileExportOperationModel::errorText() const
{
    if (!m_start_error.isEmpty()) return m_start_error;
    if (!m_operation || m_operation->pending() || m_operation->succeeded()) return {};
    if (m_operation->phase() == BackupFileOperation::Phase::Declined) return tr("PSBT export was cancelled.");
    if (m_operation->phase() == BackupFileOperation::Phase::Retired) return tr("PSBT export ended before the file was written.");
    switch (m_operation->errorCode()) {
    case backup::Error::None: return tr("The PSBT could not be saved.");
    case backup::Error::InvalidPath: return tr("Choose a valid local PSBT destination.");
    case backup::Error::Inspection: return tr("The PSBT destination could not be inspected.");
    case backup::Error::UnsupportedTarget: return tr("Choose a regular file as the PSBT destination.");
    case backup::Error::Staging: return tr("A temporary PSBT file could not be created in that directory.");
    case backup::Error::CoreBackup: return tr("The captured PSBT could not be written.");
    case backup::Error::Changed: return tr("The PSBT destination changed. Choose the file again.");
    case backup::Error::Install: return tr("The PSBT file could not be installed. Check the destination before retrying.");
    case backup::Error::Unknown: return tr("The PSBT export outcome could not be confirmed. Check the destination before retrying.");
    case backup::Error::Transport: return tr("The PSBT export could not be scheduled.");
    }
    return tr("The PSBT export outcome could not be confirmed.");
}

bool FileExportOperationModel::start()
{
    if (std::exchange(m_started, true)) return false;
    if (!m_operation) {
        Q_EMIT changed();
        return false;
    }
    const bool admitted = m_operation->start();
    if (!admitted && !m_operation->pending()) {
        if (m_source) m_source->clear();
        Q_EMIT changed();
    }
    return admitted;
}

bool FileExportOperationModel::approve()
{
    return m_operation && m_operation->approve();
}

bool FileExportOperationModel::refuse()
{
    return m_operation && m_operation->refuse();
}

void FileExportOperationModel::retire()
{
    if (m_operation) m_operation->retire();
    if (m_source && (!m_operation || !m_operation->pending())) m_source->clear();
}
