// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H
#define BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H

#include <qml/backendexecutor.h>
#include <qml/snapshotquery.h>

#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <deque>
#include <QVector>

namespace interfaces {
class Node;
}

struct RpcConsoleMailbox;
struct RpcConsoleDrainState;

/**
 * List model exposing the console output rows.
 *
 * Owned by RpcConsoleModel and exposed to QML via the outputModel property;
 * the MonospaceOutputView in CommandConsole.qml binds to it directly. Rows
 * are capped at kMaxRows — the oldest rows are dropped when the cap is hit,
 * protecting against runaway output from chatty RPCs.
 */
class RpcOutputListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        ContentRole,
        CategoryRole,
        ContinuationRole,
    };

    static constexpr int kMaxRows = 5000;
    static constexpr qsizetype kMaxCharacters = 2 * 1024 * 1024;
    static constexpr int kRowCharacters = 1024;
    struct Row {
        QString timestamp;
        QString contentHtml;
        int category;
        bool continuation{false};
    };
    using Batch = QList<Row>;

    explicit RpcOutputListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendRow(const QString& timestamp, const QString& contentHtml, int category);
    void appendRows(const Batch& rows, int offset, int count);
    void resetAll();
    std::shared_ptr<const Batch> releaseRows(bool notify = true);

Q_SIGNALS:
    void countChanged();

private:
    Batch m_rows;
    qsizetype m_characters{0};
};

/**
 * Model for the RPC command console page.
 *
 * Exposes:
 *  - availableCommands  – sorted list of all RPC commands for autocomplete
 *  - executing          – true while an RPC call is in flight
 *  - outputModel        – list of output rows (timestamp + pre-formatted HTML)
 *  - requestColor / replyColor / errorColor / keyColor –
 *        palette used to render rows; QML writes these from Theme so the
 *        colours update when the user toggles dark/light mode (already-
 *        emitted rows keep their baked-in colours, which is intentional).
 *
 * Invokable from QML:
 *  - submitCommand(command, walletName) → bool (false if rejected, e.g. empty or
 *        refused while another command is executing)
 *  - browseHistory(direction, currentText) → QString
 *  - resetHistoryNavigation()
 *  - clear()
 */
class RpcConsoleModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString completionPrefix READ completionPrefix WRITE setCompletionPrefix NOTIFY completionChanged)
    Q_PROPERTY(QStringList completionSuggestions READ completionSuggestions NOTIFY completionChanged)
    Q_PROPERTY(QString completionError READ completionError NOTIFY completionChanged)
    Q_PROPERTY(bool completionBusy READ completionBusy NOTIFY completionChanged)
    Q_PROPERTY(bool executing READ executing NOTIFY executingChanged)
    Q_PROPERTY(QStringList availableCommands READ availableCommands NOTIFY availableCommandsChanged)
    Q_PROPERTY(RpcOutputListModel* outputModel READ outputModel CONSTANT)
    Q_PROPERTY(QColor requestColor MEMBER m_request_color)
    Q_PROPERTY(QColor replyColor   MEMBER m_reply_color)
    Q_PROPERTY(QColor errorColor   MEMBER m_error_color)
    Q_PROPERTY(QColor keyColor     MEMBER m_key_color)

public:
    enum MessageCategory {
        CMD_REQUEST = 0,
        CMD_REPLY   = 1,
        CMD_ERROR   = 2
    };
    Q_ENUM(MessageCategory)

    explicit RpcConsoleModel(interfaces::Node& node, QObject* parent = nullptr);
    RpcConsoleModel(interfaces::Node& node, execution::SnapshotServices services, QObject* parent = nullptr);
    ~RpcConsoleModel();
    void closeAdmission();
    void beginShutdown();

    QString completionPrefix() const { return m_completion_prefix; }
    void setCompletionPrefix(const QString& prefix);
    QStringList completionSuggestions() const { return m_completion_suggestions; }
    QString completionError() const { return m_completion_error; }
    bool completionBusy() const { return m_completion_busy; }
    bool executing() const { return m_executing; }
    QStringList availableCommands() const { return m_available_commands; }
    RpcOutputListModel* outputModel() { return &m_output_model; }

    Q_INVOKABLE bool submitCommand(const QString& command, const QString& wallet_name = {});

    /**
     * Navigate command history.
     * @param direction  +1 = older entry, -1 = newer entry
     * @param currentText  current input text (saved as pending on first call)
     * @return  the history entry to display, or empty string if at newest end
     */
    Q_INVOKABLE QString browseHistory(int direction, const QString& currentText);

    /** Reset history navigation pointer (call when user starts typing a new command). */
    Q_INVOKABLE void resetHistoryNavigation();

    /** Clear the console output. */
    Q_INVOKABLE void clear();

public Q_SLOTS:
    void onNodeInitialized();

Q_SIGNALS:
    void drained();
    void executingChanged();
    void completionChanged();
    void availableCommandsChanged();

private Q_SLOTS:
    void onResultReady();

private:
    struct Construction;
    static Construction prepareConstruction(execution::SnapshotServices services, QObject* parent);
    RpcConsoleModel(interfaces::Node& node, Construction construction);
    void setExecuting(bool executing);
    void finishDrain();
    void appendPendingRows();
    void retireOutput(std::shared_ptr<const void> value);

    interfaces::Node& m_node;
    bool m_executing{false};
    QStringList m_available_commands;
    RpcOutputListModel m_output_model;

    // Palette — populated from QML via the Q_PROPERTY MEMBERs. Sensible
    // fall-back values so the console is legible before QML binds them.
    QColor m_request_color{"#888888"};
    QColor m_reply_color{"#CCCCCC"};
    QColor m_error_color{"#EC6363"};
    QColor m_key_color{"#98C379"};

    // History (stores redacted/filtered versions only)
    QStringList m_history;
    int m_history_idx{-1};
    QString m_pending_text; // text being edited before browsing history
    QString m_last_wallet_name; // last wallet context surfaced in the output

    bool m_stopping{false};
    bool m_shutdown_started{false};
    bool m_drained{false};
    bool m_stop_pending{false};
    std::unique_ptr<BackendExecutor> m_control_executor;
    execution::SnapshotServices m_services;
    std::shared_ptr<RpcConsoleMailbox> m_mailbox;
    std::shared_ptr<RpcConsoleDrainState> m_drain_state;
    QString m_completion_prefix, m_completion_error;
    QStringList m_completion_suggestions;
    quint64 m_completion_generation{0};
    bool m_completion_busy{false};
    bool m_metadata_pending{false};
    bool m_command_finished{false};
    QTimer m_append_timer;
    struct PendingRows { std::shared_ptr<const RpcOutputListModel::Batch> rows; int offset{0}; };
    std::deque<PendingRows> m_pending_rows;
};

#endif // BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H
