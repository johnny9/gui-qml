// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_WALLET_TEST_FIXTURE_H
#define BITCOIN_QML_TEST_WALLET_TEST_FIXTURE_H

#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <outputtype.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/translation.h>
#include <wallet/walletutil.h>

#include <QTemporaryDir>
#include <QCoreApplication>
#include <QEvent>
#include <QUuid>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <stdexcept>

/** Case-local real SQLite wallets in the integration process's regtest node. */
class WalletTestFixture
{
public:
    explicit WalletTestFixture(interfaces::Node& node) : m_node(node) {}
    ~WalletTestFixture()
    {
        for (auto& wallet : m_node.walletLoader().getWallets()) {
            if (std::ranges::find(m_names, wallet->getWalletName()) != m_names.end()) wallet->remove();
        }
    }
    std::shared_ptr<interfaces::Wallet> create(const SecureString& passphrase = {}, uint64_t flags = wallet::WALLET_FLAG_DESCRIPTORS)
    {
        std::vector<bilingual_str> warnings;
        const std::string name = QString(QStringLiteral("qml-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdString();
        auto result = m_node.walletLoader().createWallet(name, passphrase, flags, warnings);
        if (!result) throw std::runtime_error(util::ErrorString(result).original);
        m_names.push_back(name);
        return std::shared_ptr<interfaces::Wallet>(std::move(*result));
    }
    QString backupPath() const { return m_files.filePath(QStringLiteral("wallet-backup.bak")); }
    /** Seed once, then fund independent wallets without regtest halving drift.
     * The faucet is always unloaded before returning, including failed RPCs:
     * no static backend outlives Core or becomes the selected GUI wallet.
     */
    void fund(interfaces::Wallet& destination_wallet, CAmount amount = 10 * COIN, unsigned int outputs = 1)
    {
        if (outputs == 0 || amount < CAmount(outputs) || !MoneyRange(amount)) throw std::runtime_error("Invalid fixture funding amount");
        std::vector<bilingual_str> warnings;
        const bool create = m_faucet_name.empty();
        if (create) m_faucet_name = "qml-faucet-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        auto result = create ? m_node.walletLoader().createWallet(m_faucet_name, {}, wallet::WALLET_FLAG_DESCRIPTORS, warnings)
                             : m_node.walletLoader().loadWallet(m_faucet_name, warnings);
        if (!result) throw std::runtime_error(util::ErrorString(result).original);
        struct Unload {
            std::unique_ptr<interfaces::Wallet> wallet;
            ~Unload()
            {
                wallet->remove();
                wallet.reset();
                // Release the manager's queued load notification before another
                // fund() reloads this SQLite file. Membership reconciliation sees
                // an already-unloaded wallet, so no faucet view is published.
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            }
        } faucet{std::move(*result)};
        const auto mining = faucet.wallet->getNewDestination(OutputType::BECH32, "fixture-mining");
        if (!mining) throw std::runtime_error("Fixture mining address unavailable");
        auto mine = [&](int count) {
            UniValue args(UniValue::VARR);
            args.push_back(count);
            args.push_back(EncodeDestination(*mining));
            m_node.executeRpc("generatetoaddress", args, "");
        };
        if (create) mine(110);
        UniValue recipients(UniValue::VOBJ);
        for (unsigned int i = 0; i < outputs; ++i) {
            const auto address = destination_wallet.getNewDestination(OutputType::BECH32, "fixture-funding");
            if (!address) throw std::runtime_error("Fixture funding address unavailable");
            const CAmount part = amount / outputs + (i == 0 ? amount % outputs : 0);
            recipients.pushKV(EncodeDestination(*address), UniValue(UniValue::VNUM, FormatMoney(part)));
        }
        UniValue args(UniValue::VOBJ);
        args.pushKV("dummy", "");
        args.pushKV("amounts", recipients);
        args.pushKV("fee_rate", 2);
        m_node.executeRpc("sendmany", args, "/wallet/" + m_faucet_name);
        mine(1);
    }
    QString copyCanonicalBackup(const QString& name) const
    {
        const QDir fixtures(QStringLiteral(QML_WALLET_FIXTURE_DIR));
        QFile metadata(fixtures.filePath(QStringLiteral("metadata.json")));
        QFile source(fixtures.filePath(name));
        if (!metadata.open(QIODevice::ReadOnly) || !source.open(QIODevice::ReadOnly))
            throw std::runtime_error("Missing canonical QML wallet fixture or metadata");
        const auto expected = QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("fixtures")).toObject().value(name).toString();
        const auto actual = QString::fromLatin1(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex());
        if (expected.isEmpty() || actual != expected) throw std::runtime_error("Canonical QML wallet fixture checksum mismatch");
        source.close();
        const auto target = m_files.filePath(name);
        if (!QFile::copy(source.fileName(), target)) throw std::runtime_error("Could not copy canonical QML wallet fixture");
        return target;
    }
private:
    inline static std::string m_faucet_name;
    interfaces::Node& m_node;
    QTemporaryDir m_files;
    std::vector<std::string> m_names;
};

#endif // BITCOIN_QML_TEST_WALLET_TEST_FIXTURE_H
