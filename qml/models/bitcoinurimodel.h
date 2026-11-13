// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BITCOINURIMODEL_H
#define BITCOIN_QML_MODELS_BITCOINURIMODEL_H

#include <qml/snapshotquery.h>
#include <QObject>
#include <QVariantMap>
#include <atomic>
#include <memory>

struct BitcoinUriParseWork;
struct BitcoinUriOperations;
class BitcoinUriModel;

/** One original text/file intent. Cancellation hides presentation only; an
 * already executing filesystem read retains its own background lifetime. */
class BitcoinUriParseOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 requestId READ requestId CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY finished)
    Q_PROPERTY(QVariantMap result READ result NOTIFY finished)
public:
    ~BitcoinUriParseOperation() override;
    quint64 requestId() const { return m_id; }
    bool pending() const { return !admissionClosed() && m_pending; }
    QVariantMap result() const;
    void closeAdmission();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retryPublication();
Q_SIGNALS:
    void finished();
private:
    friend class BitcoinUriModel;
    BitcoinUriParseOperation(quint64 id, std::shared_ptr<const std::atomic_bool> admission, QObject* parent);
    bool admissionClosed() const { return m_closed || (m_admission && *m_admission); }
    const std::shared_ptr<const std::atomic_bool> m_admission;
    const quint64 m_id;
    bool m_pending{true}, m_closed{false};
    std::shared_ptr<BitcoinUriParseWork> m_work;
    std::function<void()> m_unregister;
};

/** Wallet-independent parser. Native parse and file I/O use injected shared
 * services; GUI only observes the original operation's cached result. */
class BitcoinUriModel : public QObject
{
    Q_OBJECT
public:
    enum class Rejection { Capacity };
    Q_ENUM(Rejection)
    static constexpr size_t MAX_OPERATIONS{128};
    explicit BitcoinUriModel(QObject* parent = nullptr);
    ~BitcoinUriModel() override;
    void setServices(execution::SnapshotServices services);
    void closeAdmission();
    Q_INVOKABLE BitcoinUriParseOperation* requestParse(const QString& text, QObject* owner, BitcoinUriParseOperation* replace = nullptr);
    Q_INVOKABLE BitcoinUriParseOperation* requestProbe(const QString& text, const QVariantMap& context, QObject* owner, BitcoinUriParseOperation* replace = nullptr);
    Q_INVOKABLE BitcoinUriParseOperation* requestParseFile(const QString& path, QObject* owner, BitcoinUriParseOperation* replace = nullptr);
Q_SIGNALS:
    void requestRejected(BitcoinUriModel::Rejection reason);
private:
    BitcoinUriParseOperation* start(const QString& input, bool file, QObject* owner, BitcoinUriParseOperation* replace, const QVariantMap& context = {});
    execution::SnapshotServices m_services;
    const std::shared_ptr<BitcoinUriOperations> m_operations;
    const std::shared_ptr<std::atomic_bool> m_admission{std::make_shared<std::atomic_bool>(false)};
    quint64 m_sequence{0};
    bool m_closed{false};
};

#endif // BITCOIN_QML_MODELS_BITCOINURIMODEL_H
