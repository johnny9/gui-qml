// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTWORK_H
#define BITCOIN_QML_PSBTWORK_H

#include <common/messages.h>
#include <qml/models/psbtfeeservices.h>
#include <qml/walletreview.h>
#include <qml/secretinput.h>
#include <support/allocators/secure.h>
#include <QByteArray>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace interfaces { class Wallet; class Node; }
namespace psbtwork {
enum class Kind { Inspect, Load, Sign, Broadcast, Save, Clipboard };
enum class Error { None, Invalid, Unavailable, NeedsUnlock, IncorrectPassphrase, Changed, Fee, Core, Uncertain, Relock };
struct BroadcastResult {
    bool accepted{false};
    QString error;
};
// Composition retains Node until the owning scheduler drains. The callable is
// invoked only on Core; it never looks up a GUI facade or selected wallet.
using Broadcast = std::function<BroadcastResult(CTransactionRef)>;
Broadcast NodeBroadcast(interfaces::Node* node);
struct Context { psbtfee::Lookup lookup; Broadcast broadcast; };
struct Snapshot {
    std::shared_ptr<const PartiallySignedTransaction> document;
    std::shared_ptr<const psbtfee::Analysis> proof;
    QString status, error;
    QStringList summary;
    bool can_sign{false}, can_broadcast{false}, complete{false};
    bool metadata_failed{false};
    int unsigned_inputs{0}, could_sign{0};
};
struct Request {
    Kind kind{Kind::Inspect};
    std::shared_ptr<const PartiallySignedTransaction> document;
    std::shared_ptr<const psbtfee::Analysis> proof;
    std::optional<CAmount> approved_fee;
    QString path, status;
    std::optional<SecureString> passphrase; // Native/test callers only.
    secretinput::Captured captured_passphrase;
};
struct Result {
    Error code{Error::None};
    QString error, path, txid, clipboard;
    CTransactionRef transaction;
    std::shared_ptr<const Snapshot> snapshot;
    bool submitted{false}, saved{false}, relock_failed{false};
};
// All helpers below are worker/test-setup utilities. Callers retain the original
// Session Action/borrow throughout effects, outcomes and native result release.
Result Execute(interfaces::Wallet* wallet, Request request, const Context& context);
QString LocalFilePath(const QString& path);
QString LoadFile(const QString& path, PartiallySignedTransaction& document);
QByteArray SerializeRaw(const PartiallySignedTransaction& document);
QString SaveFile(const PartiallySignedTransaction& document, const QString& path);
QString ErrorText(common::PSBTError error);
std::optional<std::pair<int, int>> Multisig(const PartiallySignedTransaction& document, size_t input);
struct DocumentResult { std::shared_ptr<const PartiallySignedTransaction> document; QString error; };
DocumentResult ExportReview(interfaces::Wallet& wallet, std::shared_ptr<const PartiallySignedTransaction> document,
                            CTransactionRef reviewed, bool fill_metadata,
                            std::shared_ptr<const psbtfee::Analysis> proof = {}, std::optional<CAmount> approved_fee = {});
QVariantMap CollectReviewFlow(interfaces::Wallet& wallet, CTransactionRef transaction,
    std::shared_ptr<const PartiallySignedTransaction> document = {},
    std::shared_ptr<const psbtfee::Analysis> proof = {}, const QSet<QString>& recipients = {}, int display_unit = 0,
    const QMap<QString, QString>& reviewed_notes = {});
struct ImportResult {
    enum class Disposition { Review, Known, Unsupported };
    Disposition disposition{Disposition::Unsupported};
    std::shared_ptr<const walletreview::Snapshot> review;
    std::shared_ptr<const PartiallySignedTransaction> document;
    QString message, txid;
    bool key_can_send{false}, can_commit_complete{false}, key_can_broadcast{false};
};
ImportResult PrepareReview(interfaces::Wallet& wallet, std::shared_ptr<const PartiallySignedTransaction> document,
                          const Context& context, int display_unit = 0);
} // namespace psbtwork
#endif // BITCOIN_QML_PSBTWORK_H
