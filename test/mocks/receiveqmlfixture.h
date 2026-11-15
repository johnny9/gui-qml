// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_RECEIVEQMLFIXTURE_H
#define BITCOIN_QML_TEST_MOCKS_RECEIVEQMLFIXTURE_H
#include <test/mocks/receiveeditortest.h>

class ReceiveQmlFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(WalletReceiveModel* receive READ receive CONSTANT)
    Q_PROPERTY(PaymentRequestEditorModel* savedEditor READ savedEditor NOTIFY changed)
    Q_PROPERTY(int writes READ writes NOTIFY changed)
    Q_PROPERTY(int generations READ generations NOTIFY changed)
    Q_PROPERTY(int preferences READ preferences NOTIFY changed)
    Q_PROPERTY(int clipboardWrites READ clipboardWrites NOTIFY changed)
    Q_PROPERTY(QString copiedText READ copiedText NOTIFY changed)
    Q_PROPERTY(qint64 persistedAmount READ persistedAmount NOTIFY changed)
    Q_PROPERTY(QString persistedMessage READ persistedMessage NOTIFY changed)
    Q_PROPERTY(QString persistedNote READ persistedNote NOTIFY changed)
public:
    explicit ReceiveQmlFixture(QObject* parent = nullptr) : QObject(parent), m_receive(std::make_unique<WalletReceiveModel>(2, m_ports.all()))
    {
        connect(m_receive.get(), &WalletReceiveModel::requestOpened, this, [this](auto* editor) { m_saved = editor; Q_EMIT changed(); });
    }
    ~ReceiveQmlFixture() override { m_receive.reset(); m_ports.drain(); }
    WalletReceiveModel* receive() { return m_receive.get(); }
    PaymentRequestEditorModel* savedEditor() const { return m_saved; }
    int writes() const { return m_ports.writes; }
    int generations() const { return m_ports.generations; }
    int preferences() const { return m_ports.preferences; }
    int clipboardWrites() const { return m_ports.clipboard; }
    QString copiedText() const { return m_ports.clipboard_text; }
    qint64 persistedAmount() const { return persisted().values[requestedit::Amount].amount; }
    QString persistedMessage() const { return persisted().values[requestedit::Message].text; }
    QString persistedNote() const { return persisted().values[requestedit::Note].text; }
    Q_INVOKABLE void settle() { m_ports.drain(); Q_EMIT changed(); }
    Q_INVOKABLE void domain() { m_ports.domain(); Q_EMIT changed(); }
    Q_INVOKABLE void backend() { m_ports.execution.drain(); Q_EMIT changed(); }
    Q_INVOKABLE void publish() { m_ports.publish(); Q_EMIT changed(); }
    Q_INVOKABLE void setNextGenerationError(ReceiveError::Code error) { m_ports.next_generation = error; }
    Q_INVOKABLE void setNextWriteError(ReceiveError::Code error) { m_ports.next_write = error; }
    Q_INVOKABLE void markDraftUsed() { m_ports.useDraft(); m_receive->refresh(); }
    Q_INVOKABLE void markSavedUsed() { m_ports.useSaved(17); m_receive->refresh(); }
    Q_INVOKABLE bool openSaved() { return m_receive->openRequest(QStringLiteral("17")); }
Q_SIGNALS:
    void changed();
private:
    const requestedit::Source& persisted() const { return m_ports.records.at(m_ports.records.contains(47) ? 47 : 17); }
    ReceiveEditorPorts m_ports;
    std::unique_ptr<WalletReceiveModel> m_receive;
    QPointer<PaymentRequestEditorModel> m_saved;
};
#endif // BITCOIN_QML_TEST_MOCKS_RECEIVEQMLFIXTURE_H
