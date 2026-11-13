// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_SENDTEST_H
#define BITCOIN_QML_TEST_MOCKS_SENDTEST_H

#include <qml/models/walletqmlmodel.h>
#include <qml/walletsend.h>
#include <QtTest/QtTest>

// A commit result, not a mempool/broadcast assertion. Tests of observer loss
// retain the domain operation explicitly instead of borrowing a deleted facade.
inline bool AwaitCommittedSend(WalletQmlModel& model, std::optional<QString> passphrase = {})
{
    const QPointer<WalletQmlModel> guard{&model};
    const auto ticket = passphrase ? model.requestSendTransactionWithPassphrase(*passphrase) : model.requestSendTransaction();
    if (!ticket) return false;
    if (!guard || guard->sendOperationId() != ticket) {
        QTest::qFail("Send observer disappeared or changed before its original receipt was captured", __FILE__, __LINE__);
        return false;
    }
    const auto original = guard->sendState() == WalletQmlModel::SendState::SendPending ? guard->sendOperation() : nullptr;
    if (original && !QTest::qWaitFor([&] { return original->result().has_value(); }, 5000)) {
        QTest::qFail("Accepted send did not produce its original domain outcome", __FILE__, __LINE__);
        return false;
    }
    if (!QTest::qWaitFor([&] { return !guard || !guard->sendBusy(); }, 5000) || !guard || guard->sendOperationId() != ticket) {
        QTest::qFail("Send did not reach its original matching terminal outcome", __FILE__, __LINE__);
        return false;
    }
    return original ? bool(original->result()->committed) : guard->sendState() == WalletQmlModel::SendState::SendCommitted;
}

#endif // BITCOIN_QML_TEST_MOCKS_SENDTEST_H
