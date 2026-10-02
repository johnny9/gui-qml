// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_ASYNCWALLET_H
#define BITCOIN_QML_TEST_ASYNCWALLET_H

#include <qml/models/walletqmlmodel.h>
#include <QSignalSpy>
#include <QtTest/QTest>

inline bool WaitForWalletResult(QSignalSpy& result)
{
    if (!result.isEmpty() || result.wait(10000)) return true;
    QTest::qFail("Asynchronous wallet result was not delivered", __FILE__, __LINE__);
    return false;
}

template <typename Start>
bool AwaitWalletBool(WalletQmlModel& model, void (WalletQmlModel::*signal)(bool), Start start)
{
    QSignalSpy result{&model, signal};
    if (!start()) return false;
    return WaitForWalletResult(result) && result.front().front().toBool();
}
inline bool PrepareTransaction(WalletQmlModel& model) { return AwaitWalletBool(model, &WalletQmlModel::transactionPrepared, [&] { return model.prepareTransaction(); }); }
inline bool PrepareTransactionWithPassphrase(WalletQmlModel& model, const QString& passphrase) { return AwaitWalletBool(model, &WalletQmlModel::transactionPrepared, [&] { return model.prepareTransactionWithPassphrase(passphrase); }); }
inline bool SendTransaction(WalletQmlModel& model) { return AwaitWalletBool(model, &WalletQmlModel::transactionSent, [&] { return model.sendTransaction(); }); }
inline bool SendTransactionWithPassphrase(WalletQmlModel& model, const QString& passphrase) { return AwaitWalletBool(model, &WalletQmlModel::transactionSent, [&] { return model.sendTransactionWithPassphrase(passphrase); }); }
inline bool BroadcastTransaction(WalletQmlModel& model) { return AwaitWalletBool(model, &WalletQmlModel::transactionSent, [&] { return model.broadcastCurrentTransaction(); }); }
inline WalletQmlModel::PsbtImportResult ImportPsbt(WalletQmlModel& model, const QString& path)
{
    QSignalSpy result{&model, &WalletQmlModel::psbtImported};
    const auto initial = model.importPsbtFromFile(path);
    if (initial != WalletQmlModel::PsbtImportResult::Pending) return initial;
    return WaitForWalletResult(result) ? result.front().front().value<WalletQmlModel::PsbtImportResult>() : WalletQmlModel::PsbtImportResult::PsbtUnsupported;
}
inline QString SavePsbt(WalletQmlModel& model, const QString& path)
{
    QSignalSpy result{&model, &WalletQmlModel::psbtSaved};
    auto error = model.saveCurrentTransactionAsPsbt(path);
    if (!error.isEmpty()) return error;
    return WaitForWalletResult(result) ? result.front().front().toString() : QStringLiteral("No result");
}
inline void ApproveExternalSigner(WalletQmlModel& model)
{
    model.approveExternalSignerTransaction();
    if (!QTest::qWaitFor([&] { return !model.transactionPending(); }, 10000)) QTest::qFail("Signer result was not delivered", __FILE__, __LINE__);
}
#endif // BITCOIN_QML_TEST_ASYNCWALLET_H
