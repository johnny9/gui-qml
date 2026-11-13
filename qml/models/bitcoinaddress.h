// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BITCOINADDRESS_H
#define BITCOIN_QML_MODELS_BITCOINADDRESS_H

#include <qml/snapshotquery.h>
#include <QObject>
#include <QString>
#include <atomic>

class BitcoinAddress : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString formattedAddress READ formattedAddress NOTIFY formattedAddressChanged)
    Q_PROPERTY(QString ellipsesAddress READ ellipsesAddress NOTIFY ellipsesAddressChanged)
    Q_PROPERTY(bool normalizationPending READ normalizationPending NOTIFY normalizationChanged)
    Q_PROPERTY(QString normalizationError READ normalizationError NOTIFY normalizationChanged)
    Q_PROPERTY(int normalizedCursor READ normalizedCursor NOTIFY normalizationChanged)
    Q_PROPERTY(quint64 normalizationId READ normalizationId NOTIFY normalizationChanged)

public:
    explicit BitcoinAddress(QObject* parent = nullptr);
    BitcoinAddress(const QString& address, QObject* parent = nullptr);
    ~BitcoinAddress() override;
    static void setDefaultServices(execution::SnapshotServices services);
    void setServices(execution::SnapshotServices services);
    void closeAdmission();
    void setOwnerAdmissionGate(std::shared_ptr<std::atomic_bool> gate) { m_owner_gate = std::move(gate); }
    void retryPublication();
    bool normalizationPending() const { return !admissionClosed() && m_pending; }
    QString normalizationError() const { return m_error; }
    int normalizedCursor() const { return m_cursor; }
    quint64 normalizationId() const { return m_revision; }
    Q_INVOKABLE bool cancelNormalization(quint64 id);
    Q_INVOKABLE int pasteAddress(const QString& text, int start, int end);

    static QString ellipsesAddress(const QString& address);
    static QString formattedAddress(const QString& address);

    QString address() const;
    bool isEmpty() const;
    QString formattedAddress() const;
    QString ellipsesAddress() const;
    Q_INVOKABLE int setAddress(const QString& address, int cursorPosition = 0);

Q_SIGNALS:
    void addressChanged();
    void formattedAddressChanged();
    void ellipsesAddressChanged();
    void normalizationChanged();

private:
    struct Normalized { QString address, formatted; int cursor{0}; bool complete{false}; };
    static Normalized NormalizeEditorInput(const QString& input, int cursor, qsizetype budget);
    int apply(Normalized value, bool force);
    int enqueue(const QString& input, int cursor, const QString& prefix = {}, const QString& suffix = {});
    bool admissionClosed() const { return m_closed || (m_gate && m_gate->load()) || (m_owner_gate && m_owner_gate->load()); }
    std::shared_ptr<std::atomic_bool> m_gate;
    std::shared_ptr<std::atomic_bool> m_owner_gate;
    execution::SnapshotServices m_services;
    std::shared_ptr<execution::OwnedCommand<Normalized>> m_operation;
    quint64 m_revision{0}, m_operation_revision{0};
    bool m_pending{false}, m_closed{false};
    QString m_error;
    int m_cursor{0};
    QString m_address;
    QString m_formattedAddress;
};

#endif // BITCOIN_QML_MODELS_BITCOINADDRESS_H
