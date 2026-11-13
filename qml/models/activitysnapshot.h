// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYSNAPSHOT_H
#define BITCOIN_QML_MODELS_ACTIVITYSNAPSHOT_H

#include <qml/execution.h>
#include <qml/snapshotquery.h>
#include <QObject>

#include <QMap>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace walletopening { class Session; }

namespace activityview {
using Row = QMap<int, QVariant>;
using SharedRow = std::shared_ptr<const Row>;
struct Node;
using Tree = std::shared_ptr<const Node>;
/** Immutable order-statistics AVL index. Domain performs every allocation and
 * rebalance; GUI rank lookups visit logarithmically many cached row pointers.
 * Patches transfer an already-built root, avoiding a full GUI vector shift. */
struct Node {
    QString id;
    SharedRow row;
    Tree left, right;
    int height{1}, size{1};
};
inline int Size(const Tree& tree) { return tree ? tree->size : 0; }
inline int Height(const Tree& tree) { return tree ? tree->height : 0; }
inline Tree Make(const QString& id, SharedRow row, Tree left = {}, Tree right = {}) {
    auto node = std::make_shared<Node>();
    node->id = id; node->row = std::move(row); node->left = std::move(left); node->right = std::move(right);
    node->height = 1 + std::max(Height(node->left), Height(node->right));
    node->size = 1 + Size(node->left) + Size(node->right);
    return node;
}
inline Tree RotateLeft(const Tree& tree) {
    const auto& child = tree->right;
    return Make(child->id, child->row, Make(tree->id, tree->row, tree->left, child->left), child->right);
}
inline Tree RotateRight(const Tree& tree) {
    const auto& child = tree->left;
    return Make(child->id, child->row, child->left, Make(tree->id, tree->row, child->right, tree->right));
}
inline Tree Balance(Tree tree) {
    const int delta = Height(tree->left) - Height(tree->right);
    if (delta > 1) {
        if (Height(tree->left->right) > Height(tree->left->left)) tree = Make(tree->id, tree->row, RotateLeft(tree->left), tree->right);
        return RotateRight(tree);
    }
    if (delta < -1) {
        if (Height(tree->right->left) > Height(tree->right->right)) tree = Make(tree->id, tree->row, tree->left, RotateRight(tree->right));
        return RotateLeft(tree);
    }
    return tree;
}
struct Ancestor { const Node* node; bool left; };
inline Tree RebuildPath(const std::vector<Ancestor>& path, Tree result) {
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto* node = it->node;
        result = Balance(it->left ? Make(node->id, node->row, std::move(result), node->right)
            : Make(node->id, node->row, node->left, std::move(result)));
    }
    return result;
}
inline Tree Insert(const Tree& tree, const QString& id, SharedRow row) {
    std::vector<Ancestor> path;
    path.reserve(Height(tree));
    auto* node = tree.get();
    while (node && node->id != id) {
        const bool left = id < node->id;
        path.push_back({node, left});
        node = left ? node->left.get() : node->right.get();
    }
    auto result = node ? Make(id, std::move(row), node->left, node->right) : Make(id, std::move(row));
    return RebuildPath(path, std::move(result));
}
inline Tree Erase(const Tree& tree, const QString& id) {
    std::vector<Ancestor> path;
    path.reserve(Height(tree));
    auto* node = tree.get();
    while (node && node->id != id) {
        const bool left = id < node->id;
        path.push_back({node, left});
        node = left ? node->left.get() : node->right.get();
    }
    Tree result;
    if (node) {
        if (!node->left) result = node->right;
        else if (!node->right) result = node->left;
        else {
            std::vector<Ancestor> successor_path;
            successor_path.reserve(Height(node->right));
            auto* successor = node->right.get();
            while (successor->left) {
                successor_path.push_back({successor, true});
                successor = successor->left.get();
            }
            auto right = RebuildPath(successor_path, successor->right);
            result = Balance(Make(successor->id, successor->row, node->left, std::move(right)));
        }
    }
    return RebuildPath(path, std::move(result));
}
inline const Node* At(Tree tree, int rank) {
    if (rank < 0 || rank >= Size(tree)) return nullptr;
    while (tree) {
        const int left = Size(tree->left);
        if (rank == left) return tree.get();
        if (rank < left) tree = tree->left;
        else { rank -= left + 1; tree = tree->right; }
    }
    return nullptr;
}
inline int Rank(Tree tree, const QString& id) {
    int rank{0};
    while (tree) {
        if (id == tree->id) return rank + Size(tree->left);
        if (id < tree->id) tree = tree->left;
        else { rank += Size(tree->left) + 1; tree = tree->right; }
    }
    return rank;
}
struct Patch {
    enum class Kind { Insert, Remove, Change, Move } kind;
    int position;
    Tree root;
    QList<int> roles;
    int transaction_delta{0}, request_delta{0}, destination{0};
};
/** Full immutable rows are for Domain filtering/export. Presentation values may
 * use bounded previews; neither consumer reaches into a QObject or Core. */
struct Snapshot {
    quint64 revision{0};
    int display_unit{0};
    std::shared_ptr<const void> detail_source; // Original immutable Native domain frame; never exposed to QML.
    std::shared_ptr<const void> opening;
    std::function<QString(const QString&)> raw; // Native-only immutable document codec
    std::map<QString, SharedRow> rows;
    std::map<QString, int> source_positions;
    std::map<QString, QString> order_keys;
    Tree presentation;
    std::map<QString, std::shared_ptr<const QVariantMap>> details;
    std::map<QString, QString> request_uris;
    QString selected_txid;
    std::shared_ptr<const QVariantMap> selected_details;
    QString selected_raw;
    std::shared_ptr<const QVariantList> detail_inputs, detail_outputs, detail_actions;
    std::shared_ptr<const QVariantList> full_inputs, full_outputs, full_actions, entry_document;
    int transactions{0}, requests{0};
};
/** Cleanup was reserved before the underlying value was admitted. Drop GUI
 * references before its lease so native cleanup owns every large last ref. */
struct Hold {
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<execution::ResourceLease> lease;
    ~Hold() { snapshot.reset(); lease.reset(); }
};
class Source {
public:
    virtual ~Source() = default;
    virtual std::shared_ptr<const Hold> preparedSnapshot() const = 0;
    virtual execution::SnapshotServices activityServices() const = 0;
    virtual std::shared_ptr<walletopening::Session> opening() const = 0;
    virtual bool activityLoading() const = 0;
    virtual QString activityError() const = 0;
    virtual int activityRowForId(const QString& id) const = 0;
    virtual QList<QMetaObject::Connection> observeActivity(QObject* receiver, std::function<void()> changed) = 0;
};
inline execution::BorrowAdmission HoldValues(const std::shared_ptr<execution::Scheduler>& scheduler, std::function<void()> cleanup)
{
    const auto key = scheduler->allocateKey();
    const auto reserved = execution::Submit(*scheduler, std::move(cleanup), [] {}, {}, {execution::Lane::Cleanup, 256, key}, true);
    if (!reserved) return {{}, reserved.rejection};
    auto held = scheduler->borrow(key);
    scheduler->activateCleanup(reserved.id);
    return held;
}
} // namespace activityview
Q_DECLARE_INTERFACE(activityview::Source, "org.bitcoincore.qt.activitysnapshot/1.0")

#endif // BITCOIN_QML_MODELS_ACTIVITYSNAPSHOT_H
