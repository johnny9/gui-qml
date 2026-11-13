// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtQuickTest/quicktest.h>
#include <qml/models/monospaceoutputmodel.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/flowpresentation.h>
#include <qml/models/uipreferences.h>

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QEventLoop>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQueue>
#include <QInputMethodEvent>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <qqml.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include <qml/backendexecutor.h>
#include <qml/bitcoinamount.h>
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/qrimageprovider.h>

class MockAppMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDesktop READ isDesktop WRITE setIsDesktop NOTIFY isDesktopChanged)
    Q_PROPERTY(bool walletEnabled READ walletEnabled WRITE setWalletEnabled NOTIFY walletEnabledChanged)
    Q_PROPERTY(QString state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled CONSTANT)

public:
    bool isDesktop() const { return m_is_desktop; }
    bool animationsEnabled() const { return !qEnvironmentVariableIsSet("BITCOIN_QML_TEST_NO_ANIMATIONS"); }
    bool walletEnabled() const { return m_wallet_enabled; }
    QString state() const { return m_state; }

public Q_SLOTS:
    void setIsDesktop(const bool value)
    {
        if (m_is_desktop == value) return;
        m_is_desktop = value;
        Q_EMIT isDesktopChanged();
    }
    void setWalletEnabled(const bool value)
    {
        if (m_wallet_enabled == value) return;
        m_wallet_enabled = value;
        Q_EMIT walletEnabledChanged();
    }
    void setState(const QString& value)
    {
        if (m_state == value) return;
        m_state = value;
        Q_EMIT stateChanged();
    }

Q_SIGNALS:
    void isDesktopChanged();
    void walletEnabledChanged();
    void stateChanged();

private:
    bool m_is_desktop{true};
    bool m_wallet_enabled{true};
    QString m_state{QStringLiteral("DESKTOP")};
};

class MockBuildInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)

public:
    bool isDebug() const { return false; }
    QString fullClientVersion() const { return QStringLiteral("v0.0.0-test"); }
};

class MockPeerDetailsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int nodeId MEMBER m_node_id NOTIFY dataChanged)
    Q_PROPERTY(QString rawAddress MEMBER m_raw_address NOTIFY dataChanged)
    Q_PROPERTY(QString address MEMBER m_address NOTIFY dataChanged)
    Q_PROPERTY(QString addressLocal MEMBER m_address_local NOTIFY dataChanged)
    Q_PROPERTY(QString type MEMBER m_type NOTIFY dataChanged)
    Q_PROPERTY(QString network MEMBER m_network NOTIFY dataChanged)
    Q_PROPERTY(QString transport MEMBER m_transport NOTIFY dataChanged)
    Q_PROPERTY(QString sessionId MEMBER m_session_id NOTIFY dataChanged)
    Q_PROPERTY(QString permission MEMBER m_permission NOTIFY dataChanged)
    Q_PROPERTY(QString version MEMBER m_version NOTIFY dataChanged)
    Q_PROPERTY(QString userAgent MEMBER m_user_agent NOTIFY dataChanged)
    Q_PROPERTY(QString services MEMBER m_services NOTIFY dataChanged)
    Q_PROPERTY(bool transactionRelay MEMBER m_transaction_relay NOTIFY dataChanged)
    Q_PROPERTY(bool addressRelay MEMBER m_address_relay NOTIFY dataChanged)
    Q_PROPERTY(bool highBandwidth MEMBER m_high_bandwidth NOTIFY dataChanged)
    Q_PROPERTY(QString addressesProcessed MEMBER m_addresses_processed NOTIFY dataChanged)
    Q_PROPERTY(QString addressesRateLimited MEMBER m_addresses_rate_limited NOTIFY dataChanged)
    Q_PROPERTY(QString mappedAS MEMBER m_mapped_as NOTIFY dataChanged)
    Q_PROPERTY(QString startingHeight MEMBER m_starting_height NOTIFY dataChanged)
    Q_PROPERTY(QString syncedHeaders MEMBER m_synced_headers NOTIFY dataChanged)
    Q_PROPERTY(QString syncedBlocks MEMBER m_synced_blocks NOTIFY dataChanged)
    Q_PROPERTY(QString direction MEMBER m_direction NOTIFY dataChanged)
    Q_PROPERTY(QString connectionDuration MEMBER m_connection_duration NOTIFY dataChanged)
    Q_PROPERTY(QString lastSend MEMBER m_last_send NOTIFY dataChanged)
    Q_PROPERTY(QString lastReceived MEMBER m_last_received NOTIFY dataChanged)
    Q_PROPERTY(QString bytesSent MEMBER m_bytes_sent NOTIFY dataChanged)
    Q_PROPERTY(QString bytesReceived MEMBER m_bytes_received NOTIFY dataChanged)
    Q_PROPERTY(QString pingTime MEMBER m_ping_time NOTIFY dataChanged)
    Q_PROPERTY(QString pingWait MEMBER m_ping_wait NOTIFY dataChanged)
    Q_PROPERTY(QString pingMin MEMBER m_ping_min NOTIFY dataChanged)
    Q_PROPERTY(QString timeOffset MEMBER m_time_offset NOTIFY dataChanged)

public:
    int m_node_id{7};
    QString m_raw_address{QStringLiteral("127.0.0.1")};
    QString m_address{QStringLiteral("127.0.0.1:8333")};
    QString m_address_local{QStringLiteral("127.0.0.1:18444")};
    QString m_type{QStringLiteral("Outbound Full Relay")};
    QString m_network{QStringLiteral("IPv4")};
    QString m_transport{QStringLiteral("v2")};
    QString m_session_id{QStringLiteral("043604a60a54b3f5")};
    QString m_permission{};
    QString m_version{QStringLiteral("70016")};
    QString m_user_agent{QStringLiteral("/Satoshi:test/")};
    QString m_services{QStringLiteral("NETWORK|WITNESS")};
    bool m_transaction_relay{true};
    bool m_address_relay{false};
    bool m_high_bandwidth{true};
    QString m_addresses_processed{QStringLiteral("1076")};
    QString m_addresses_rate_limited{QStringLiteral("0")};
    QString m_mapped_as{};
    QString m_starting_height{};
    QString m_synced_headers{QStringLiteral("200")};
    QString m_synced_blocks{QStringLiteral("150")};
    QString m_direction{QStringLiteral("Inbound")};
    QString m_connection_duration{QStringLiteral("5 min")};
    QString m_last_send{QStringLiteral("2 s")};
    QString m_last_received{QStringLiteral("3 s")};
    QString m_bytes_sent{QStringLiteral("1.0 MiB")};
    QString m_bytes_received{QStringLiteral("2.0 MiB")};
    QString m_ping_time{QStringLiteral("10 ms")};
    QString m_ping_wait{};
    QString m_ping_min{QStringLiteral("7 ms")};
    QString m_time_offset{QStringLiteral("0 s")};

    Q_INVOKABLE void triggerDisconnected() { Q_EMIT disconnected(); }

Q_SIGNALS:
    void dataChanged();
    void disconnected();
};

// Real validated values make the component fixtures exercise production parsing.
class MockBitcoinAmount : public BitcoinAmount
{
    Q_OBJECT
public:
    static constexpr Unit BTC{Unit::BTC};
    static constexpr Unit SAT{Unit::SAT};
    QString display() const { return toDisplay(); }
    void setDisplay(const QString& value) { fromDisplay(value); }
};

class MockBitcoinAddress : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString formattedAddress READ formattedAddress NOTIFY formattedAddressChanged)
    Q_PROPERTY(QString ellipsesAddress READ ellipsesAddress NOTIFY ellipsesAddressChanged)
    Q_PROPERTY(bool normalizationPending READ normalizationPending CONSTANT)
    Q_PROPERTY(QString normalizationError READ normalizationError CONSTANT)
    Q_PROPERTY(quint64 normalizationId MEMBER m_revision NOTIFY formattedAddressChanged)
    Q_PROPERTY(int normalizedCursor MEMBER m_cursor NOTIFY formattedAddressChanged)

public:
    QString address() const { return m_address; }
    QString formattedAddress() const { return m_address; }
    QString ellipsesAddress() const { return m_address; }
    bool normalizationPending() const { return false; }
    QString normalizationError() const { return {}; }
    Q_INVOKABLE bool cancelNormalization(quint64) { return false; }
    Q_INVOKABLE int pasteAddress(const QString& text, int start, int end) {
        start = std::clamp(start, 0, static_cast<int>(m_address.size()));
        end = std::clamp(end, start, static_cast<int>(m_address.size()));
        return setAddress(m_address.left(start) + text + m_address.mid(end), start + static_cast<int>(text.size()));
    }
    Q_INVOKABLE int setAddress(const QString& address, int cursorPosition = 0)
    {
        ++m_revision;
        const auto is_address_char = [](const QChar c) {
            const ushort ch = c.unicode();
            return (ch >= '0' && ch <= '9') ||
                   (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z' && ch != 'I' && ch != 'O');
        };

        QString sanitized;
        const int address_size = static_cast<int>(address.size());
        sanitized.reserve(std::min(address_size, 90));
        const int input_cursor = std::clamp(cursorPosition, 0, address_size);
        int sanitized_cursor{0};
        for (int i = 0; i < address_size && sanitized.size() < 90; ++i) {
            if (!is_address_char(address.at(i))) continue;
            sanitized += address.at(i);
            if (i < input_cursor) ++sanitized_cursor;
        }

        m_cursor = sanitized_cursor;
        if (m_address != sanitized) {
            m_address = sanitized;
            Q_EMIT addressChanged();
            Q_EMIT formattedAddressChanged();
            Q_EMIT ellipsesAddressChanged();
        } else if (address != sanitized) {
            Q_EMIT formattedAddressChanged();
        }
        return sanitized_cursor;
    }

Q_SIGNALS:
    void addressChanged();
    void formattedAddressChanged();
    void ellipsesAddressChanged();

private:
    int m_cursor{0};
    quint64 m_revision{0};
    QString m_address;
};

class MockReceiveRequestOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool requestSaved MEMBER m_saved NOTIFY changed)
    Q_PROPERTY(bool requestLoaded MEMBER m_loaded NOTIFY changed)
    Q_PROPERTY(bool erased MEMBER m_erased NOTIFY changed)
    Q_PROPERTY(bool labelSaved MEMBER m_label_saved NOTIFY changed)
    Q_PROPERTY(QString requestId MEMBER m_id NOTIFY changed)
    Q_PROPERTY(QString errorText MEMBER m_error NOTIFY changed)
    Q_PROPERTY(bool addressReady MEMBER m_address_ready NOTIFY changed)
    Q_PROPERTY(QString addressToken MEMBER m_token NOTIFY changed)
    Q_PROPERTY(quint64 viewRevision MEMBER m_view_revision NOTIFY changed)
    Q_PROPERTY(Error error MEMBER m_error_code NOTIFY changed)
public:
    enum Error { None, NeedsUnlock, IncorrectPassphrase, ReservationPersistence };
    Q_ENUM(Error)
    Error m_error_code{None};
    bool m_address_ready{false}, m_next{false};
    quint64 m_view_revision{0};
    QString m_token, m_type;
    explicit MockReceiveRequestOperation(QObject* parent = nullptr) : QObject(parent) {}
    bool m_pending{false}, m_saved{false}, m_loaded{false}, m_erased{false}, m_label_saved{false};
    QString m_id, m_error;
    Q_INVOKABLE void complete(bool saved, bool erased, const QString& error) {
        m_pending = false; m_saved = saved; m_erased = erased; m_error = error; Q_EMIT changed();
    }
Q_SIGNALS:
    void changed();
};

class MockAddressListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(Category category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QVariantList categoryOptions READ categoryOptions CONSTANT)
    Q_PROPERTY(bool showUsed READ showUsed WRITE setShowUsed NOTIFY showUsedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool setAddressLabelSucceeds MEMBER m_set_address_label_succeeds)

public:
    enum Category {
        SingleUse,
        Change
    };
    Q_ENUM(Category)

    enum AddressRoles {
        AddressRole = Qt::UserRole + 1,
        FormattedAddressRole,
        EllipsesAddressRole,
        LabelRole,
        CategoryRole,
        UsedRole,
        CurrentBalanceRole,
        DisplayAmountRole,
        HasAmountRole,
        ScriptTypeRole,
        CanEditLabelRole,
        CanCreatePaymentRequestRole,
    };

    MockAddressListModel()
    {
        populateRows();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const Entry& entry{m_rows.at(index.row())};
        switch (role) {
        case AddressRole:
        case FormattedAddressRole:
        case EllipsesAddressRole:
            return entry.address;
        case LabelRole:
            return entry.label;
        case CategoryRole:
            return QStringLiteral("single-use");
        case UsedRole:
        case HasAmountRole:
            return false;
        case CurrentBalanceRole:
            return QStringLiteral("0");
        case DisplayAmountRole:
            return QStringLiteral("₿ 0");
        case ScriptTypeRole:
            return QStringLiteral("P2WPKH");
        case CanEditLabelRole:
        case CanCreatePaymentRequestRole:
            return true;
        }
        return {};
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {FormattedAddressRole, "formattedAddress"},
            {EllipsesAddressRole, "ellipsesAddress"},
            {LabelRole, "label"},
            {CategoryRole, "category"},
            {UsedRole, "isUsed"},
            {CurrentBalanceRole, "currentBalance"},
            {DisplayAmountRole, "displayAmount"},
            {HasAmountRole, "hasAmount"},
            {ScriptTypeRole, "scriptType"},
            {CanEditLabelRole, "canEditLabel"},
            {CanCreatePaymentRequestRole, "canCreatePaymentRequest"},
        };
    }

    Category category() const { return m_category; }
    QVariantList categoryOptions() const
    {
        return {
            QVariantMap{{QStringLiteral("value"), static_cast<int>(SingleUse)}, {QStringLiteral("text"), QStringLiteral("Receive")}},
            QVariantMap{{QStringLiteral("value"), static_cast<int>(Change)}, {QStringLiteral("text"), QStringLiteral("Change")}},
        };
    }
    bool showUsed() const { return m_show_used; }
    int count() const { return rowCount(); }

    void setCategory(const Category category)
    {
        if (m_category == category) return;
        m_category = category;
        Q_EMIT categoryChanged();
    }

    void setShowUsed(const bool show_used)
    {
        if (m_show_used == show_used) return;
        m_show_used = show_used;
        Q_EMIT showUsedChanged();
    }

    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE MockReceiveRequestOperation* setAddressLabel(const QString& address, const QString& label)
    {
        if (!m_set_address_label_succeeds) return nullptr;
        for (int row = 0; row < rowCount(); ++row) {
            Entry& entry{m_rows.at(row)};
            if (entry.address != address) continue;
            entry.label = label;
            Q_EMIT dataChanged(index(row), index(row), {LabelRole});
            break;
        }
        auto* operation = new MockReceiveRequestOperation(this); operation->m_label_saved = true;
        return operation;
    }
    Q_INVOKABLE QString addressAt(int row) const
    {
        return row >= 0 && row < rowCount() ? m_rows.at(row).address : QString{};
    }
    Q_INVOKABLE QString labelAt(int row) const
    {
        return row >= 0 && row < rowCount() ? m_rows.at(row).label : QString{};
    }
    Q_INVOKABLE void resetForTest()
    {
        beginResetModel();
        m_rows.clear();
        populateRows();
        endResetModel();
        Q_EMIT countChanged();
    }

Q_SIGNALS:
    void categoryChanged();
    void showUsedChanged();
    void countChanged();

private:
    struct Entry {
        QString address;
        QString label;
    };

    void populateRows()
    {
        for (int row = 0; row < 20; ++row) {
            m_rows.push_back({
                QStringLiteral("bcrt1qtestaddress%1").arg(row, 2, 10, QLatin1Char('0')),
                QStringLiteral("Original note %1").arg(row),
            });
        }
    }

    std::vector<Entry> m_rows;
    Category m_category{SingleUse};
    bool m_show_used{false};
    bool m_set_address_label_succeeds{false};
};

class MockFileDialog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title MEMBER m_title NOTIFY titleChanged)
    Q_PROPERTY(int fileMode MEMBER m_file_mode NOTIFY fileModeChanged)
    Q_PROPERTY(QString defaultSuffix MEMBER m_default_suffix NOTIFY defaultSuffixChanged)
    Q_PROPERTY(bool confirmOverwrite MEMBER m_confirm_overwrite NOTIFY confirmOverwriteChanged)
    Q_PROPERTY(QStringList nameFilters MEMBER m_name_filters NOTIFY nameFiltersChanged)
    Q_PROPERTY(QUrl currentFolder MEMBER m_current_folder NOTIFY currentFolderChanged)
    Q_PROPERTY(QUrl currentFile MEMBER m_current_file NOTIFY currentFileChanged)
    Q_PROPERTY(QUrl selectedFile MEMBER m_selected_file NOTIFY selectedFileChanged)
    Q_PROPERTY(bool visible MEMBER m_visible NOTIFY visibleChanged)
    Q_PROPERTY(int openCalls MEMBER m_open_calls NOTIFY openCallsChanged)

public:
    enum FileMode { OpenFile, SaveFile, Directory };
    Q_ENUM(FileMode)

    QString m_title;
    int m_file_mode{OpenFile};
    QString m_default_suffix;
    bool m_confirm_overwrite{true};
    QStringList m_name_filters;
    QUrl m_current_folder;
    QUrl m_current_file;
    QUrl m_selected_file;
    bool m_visible{false};
    int m_open_calls{0};

    Q_INVOKABLE void open()
    {
        ++m_open_calls;
        m_visible = true;
        Q_EMIT openCallsChanged();
        Q_EMIT visibleChanged();
    }

    Q_INVOKABLE void close()
    {
        m_visible = false;
        Q_EMIT visibleChanged();
    }

    /** Stand in for the user picking a file. */
    Q_INVOKABLE void selectAndAccept(const QUrl& file)
    {
        m_selected_file = file;
        m_visible = false;
        Q_EMIT selectedFileChanged();
        Q_EMIT visibleChanged();
        Q_EMIT accepted();
    }

Q_SIGNALS:
    void titleChanged();
    void fileModeChanged();
    void defaultSuffixChanged();
    void confirmOverwriteChanged();
    void nameFiltersChanged();
    void currentFolderChanged();
    void currentFileChanged();
    void selectedFileChanged();
    void visibleChanged();
    void openCallsChanged();
    void accepted();
    void rejected();
};

class MockPaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id MEMBER m_id NOTIFY idChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(QString message MEMBER m_message NOTIFY messageChanged)
    Q_PROPERTY(QString noteSelf MEMBER m_note_self NOTIFY noteSelfChanged)
    Q_PROPERTY(QString address MEMBER m_address NOTIFY addressChanged)
    Q_PROPERTY(QString addressFormatted READ addressFormatted NOTIFY addressChanged)
    Q_PROPERTY(QString addressType MEMBER m_address_type NOTIFY addressTypeChanged)
    Q_PROPERTY(bool needsUnlock MEMBER m_needs_unlock NOTIFY needsUnlockChanged)
    Q_PROPERTY(QString unlockError MEMBER m_unlock_error NOTIFY unlockErrorChanged)
    Q_PROPERTY(QString createdIso MEMBER m_created_iso NOTIFY createdIsoChanged)
    Q_PROPERTY(QString qrPayload READ qrPayload NOTIFY addressChanged)
    Q_PROPERTY(bool paymentReceived MEMBER m_payment_received NOTIFY paymentReceivedChanged)
    Q_PROPERTY(qint64 receivedAmountSatoshi MEMBER m_received_amount NOTIFY receivedAmountChanged)
    Q_PROPERTY(bool isEditing MEMBER m_is_editing NOTIFY isEditingChanged)
    Q_PROPERTY(quint64 generation MEMBER m_generation NOTIFY generationChanged)
    Q_PROPERTY(quint64 revision MEMBER m_revision NOTIFY draftChanged)

public:
    quint64 m_generation{0}, m_revision{0};
    MockPaymentRequest() {
        const auto edit = [this] { ++m_revision; Q_EMIT draftChanged(); };
        connect(this, &MockPaymentRequest::addressChanged, this, edit);
        connect(this, &MockPaymentRequest::addressTypeChanged, this, edit);
    }
    bool m_payment_received{false};
    qint64 m_received_amount{0};
    QString m_id;
    QString m_amount_error;
    QString m_label;
    QString m_message;
    QString m_note_self;
    QString m_address;
    QString m_address_type;
    bool m_needs_unlock{false};
    QString m_unlock_error;
    QString m_created_iso;
    bool m_is_editing{true};
    MockBitcoinAmount m_amount{};

    QObject* amount() { return &m_amount; }
    QString addressFormatted() const { return m_address; }
    QString qrPayload() const { return m_address.isEmpty() || m_payment_received ? QString{} : QStringLiteral("bitcoin:") + m_address; }
    Q_INVOKABLE void clear()
    {
        ++m_generation;
        Q_EMIT generationChanged();
        m_payment_received = false;
        m_received_amount = 0;
        Q_EMIT receivedAmountChanged();
        Q_EMIT paymentReceivedChanged();
        m_id.clear();
        m_amount_error.clear();
        m_label.clear();
        m_message.clear();
        m_note_self.clear();
        m_address.clear();
        m_address_type.clear();
        m_needs_unlock = false;
        m_unlock_error.clear();
        m_created_iso.clear();
        m_is_editing = true;
        m_amount.setUnit(MockBitcoinAmount::BTC);
        m_amount.setSatoshi(0);
        Q_EMIT idChanged();
        Q_EMIT amountErrorChanged();
        Q_EMIT labelChanged();
        Q_EMIT messageChanged();
        Q_EMIT noteSelfChanged();
        Q_EMIT addressChanged();
        Q_EMIT addressTypeChanged();
        Q_EMIT needsUnlockChanged();
        Q_EMIT unlockErrorChanged();
        Q_EMIT createdIsoChanged();
        Q_EMIT isEditingChanged();
        Q_EMIT m_amount.displayChanged();
    }
    Q_INVOKABLE void edit()
    {
        if (m_is_editing) return;
        m_is_editing = true;
        Q_EMIT isEditingChanged();
    }

Q_SIGNALS:
    void generationChanged();
    void draftChanged();
    void paymentReceivedChanged();
    void receivedAmountChanged();
    void idChanged();
    void amountErrorChanged();
    void labelChanged();
    void messageChanged();
    void noteSelfChanged();
    void addressChanged();
    void addressTypeChanged();
    void needsUnlockChanged();
    void unlockErrorChanged();
    void createdIsoChanged();
    void isEditingChanged();
};

class MockReceiveRequests : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count MEMBER m_count NOTIFY countChanged)

public:
    int m_count{0};

    Q_INVOKABLE QVariantList matchingEntriesForAddress(const QString& address) const
    {
        if (address != QStringLiteral("bcrt1qreceiveaddress")) {
            return {};
        }

        return {
            QVariantMap{
                {QStringLiteral("requestId"), QStringLiteral("req-1")},
                {QStringLiteral("label"), QStringLiteral("Alice")},
                {QStringLiteral("amountDisplay"), QStringLiteral("0.00100000 BTC")},
                {QStringLiteral("date"), QStringLiteral("Fri Jan 2 2026")},
            },
        };
    }
    Q_INVOKABLE QString firstRequestIdForAddress(const QString& address) const
    {
        return address == QStringLiteral("bcrt1qreceiveaddress") ? QStringLiteral("req-1") : QString{};
    }

Q_SIGNALS:
    void countChanged();
};

class MockTransaction : public QObject
{
    Q_OBJECT

public:
    enum Status {
        Confirmed,
        Unconfirmed,
        Confirming,
        Conflicted,
        Abandoned,
        Immature,
        NotAccepted
    };
    Q_ENUM(Status)

    enum Type {
        Other,
        Generated,
        SendToAddress,
        SendToOther,
        RecvWithAddress,
        RecvFromOther,
        SendToSelf
    };
    Q_ENUM(Type)
};

class MockSendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* address READ address CONSTANT)
    Q_PROPERTY(QString addressError MEMBER m_address_error NOTIFY addressErrorChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString paymentRequestLabel MEMBER m_request_label NOTIFY paymentRequestChanged)
    Q_PROPERTY(QString message MEMBER m_message NOTIFY paymentRequestChanged)
    Q_PROPERTY(bool hasPaymentRequest MEMBER m_has_request NOTIFY paymentRequestChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(bool isValid MEMBER m_is_valid NOTIFY isValidChanged)
    Q_PROPERTY(quint64 validationRevision MEMBER m_validation_revision NOTIFY isValidChanged)
    Q_PROPERTY(bool validationPending MEMBER m_validation_pending NOTIFY isValidChanged)

public:
    MockSendRecipient()
    {
        m_address.setAddress(QStringLiteral("bcrt1qsendtoaddress"));
        const auto edit = [this] { ++m_validation_revision; Q_EMIT isValidChanged(); };
        connect(&m_address, &MockBitcoinAddress::addressChanged, this, edit);
        connect(&m_amount, &BitcoinAmount::inputChanged, this, edit);
        connect(&m_amount, &BitcoinAmount::amountChanged, this, edit);
        connect(this, &MockSendRecipient::labelChanged, this, edit);
        connect(this, &MockSendRecipient::paymentRequestChanged, this, edit);
    }

    MockBitcoinAddress m_address{};
    QString m_address_error;
    MockBitcoinAmount m_amount{};
    QString m_amount_error;
    QString m_label;
    QString m_request_label, m_message;
    bool m_has_request{false};
    Q_INVOKABLE void applyPaymentRequest(const QString& address, const QString& label, const QString& message) { m_address.setAddress(address); m_request_label = label; m_message = message; m_has_request = true; Q_EMIT paymentRequestChanged(); }
    bool m_is_valid{true};
    bool m_validation_pending{false};
    quint64 m_validation_revision{0};
    Q_INVOKABLE void applyParsedPaymentRequest(const QVariantMap& parsed) {
        if (!parsed.value("success").toBool()) return;
        applyPaymentRequest(parsed.value("address").toString(), parsed.value("hasLabel").toBool() ? parsed.value("label").toString() : QString{}, parsed.value("hasMessage").toBool() ? parsed.value("uriMessage").toString() : QString{});
        if (parsed.value("hasAmount").toBool()) m_amount.setSatoshi(parsed.value("amountSats").toLongLong());
        const auto note = parsed.value("privateNote").toString();
        if (!note.isEmpty()) { m_label = note; Q_EMIT labelChanged(); }
    }

    QObject* address() { return &m_address; }
    QObject* amount() { return &m_amount; }

Q_SIGNALS:
    void paymentRequestChanged();
    void addressErrorChanged();
    void amountErrorChanged();
    void labelChanged();
    void isValidChanged();
};

class MockRecipientsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool validationPending READ validationPending CONSTANT)
    Q_PROPERTY(QObject* current READ current NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(qint64 totalAmountSatoshi READ totalAmountSatoshi WRITE setTotalAmountSatoshi NOTIFY totalAmountChanged)
    Q_PROPERTY(bool allValid READ allValid WRITE setAllValid NOTIFY validationChanged)
    Q_PROPERTY(QString validationError READ validationError WRITE setValidationError NOTIFY validationChanged)

public:
    bool validationPending() const { return false; }
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        LabelRole,
        AmountRole, RecipientRole
    };

    struct RecipientRow {
        QString address;
        QString label;
        QString amount;
    };

    MockRecipientsModel()
    {
        m_rows.push_back({
            QStringLiteral("bcrt1qsendtoaddress"),
            QStringLiteral("recipient-1"),
            QStringLiteral("0.01000000 BTC"),
        });
    }

    QObject* current() const { return m_current; }
    int currentIndex() const { return m_current_index; } // 1-based, matches QML expectations.
    int count() const { return static_cast<int>(m_rows.size()); }
    qint64 totalAmountSatoshi() const { return m_total_amount_satoshi; }
    bool allValid() const { return m_all_valid; }
    QString validationError() const { return m_validation_error; }

    void setAllValid(bool all_valid)
    {
        if (m_all_valid == all_valid) return;
        m_all_valid = all_valid;
        Q_EMIT validationChanged();
    }

    void setValidationError(const QString& validation_error)
    {
        if (m_validation_error == validation_error) return;
        m_validation_error = validation_error;
        Q_EMIT validationChanged();
    }

    void setTotalAmountSatoshi(qint64 total_amount_satoshi)
    {
        if (m_total_amount_satoshi == total_amount_satoshi) return;
        m_total_amount_satoshi = total_amount_satoshi;
        Q_EMIT totalAmountChanged();
    }

    void setCurrent(QObject* recipient)
    {
        m_current = recipient;
        Q_EMIT currentChanged();
        Q_EMIT currentRecipientChanged();
    }

    Q_INVOKABLE void setCurrentIndex(int index)
    {
        const int bounded = std::clamp(index + 1, 1, std::max(1, count()));
        if (m_current_index == bounded) return;
        m_current_index = bounded;
        Q_EMIT currentIndexChanged();
    }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return count();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
        const RecipientRow& row = m_rows.at(index.row());
        switch (role) {
        case RecipientRole: return QVariant::fromValue(m_current);
        case AddressRole: return row.address;
        case LabelRole: return row.label;
        case AmountRole: return row.amount;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {RecipientRole, "recipientObject"},
            {AddressRole, "address"},
            {LabelRole, "label"},
            {AmountRole, "amount"},
        };
    }

    Q_INVOKABLE void clearToFront()
    {
        beginResetModel();
        while (m_rows.size() > 1) {
            m_rows.pop_back();
        }
        endResetModel();
        m_current_index = 1;
        setTotalAmountSatoshi(0);
        Q_EMIT countChanged();
        Q_EMIT currentIndexChanged();
        Q_EMIT listCleared();
    }

    Q_INVOKABLE void add()
    {
        const int row = count();
        beginInsertRows(QModelIndex{}, row, row);
        const int next = row + 1;
        m_rows.push_back({
            QStringLiteral("bcrt1qrecipient%1").arg(next),
            QStringLiteral("recipient-%1").arg(next),
            QStringLiteral("0.00%1 BTC").arg(next),
        });
        endInsertRows();
        m_current_index = count();
        Q_EMIT countChanged();
        Q_EMIT currentIndexChanged();
    }

    Q_INVOKABLE void prev() { setCurrentIndex(m_current_index - 2); }
    Q_INVOKABLE void next() { setCurrentIndex(m_current_index); }

    Q_INVOKABLE void clear() { clearToFront(); }
    Q_INVOKABLE void removeAt(int) { remove(); }
    Q_INVOKABLE void remove()
    {
        if (count() <= 1) return;
        const int row = count() - 1;
        beginRemoveRows(QModelIndex{}, row, row);
        m_rows.pop_back();
        endRemoveRows();
        if (m_current_index > count()) m_current_index = count();
        Q_EMIT countChanged();
        Q_EMIT currentIndexChanged();
    }

Q_SIGNALS:
    void currentChanged();
    void currentRecipientChanged();
    void currentIndexChanged();
    void countChanged();
    void totalAmountChanged();
    void listCleared();
    void validationChanged();

private:
    QObject* m_current{nullptr};
    int m_current_index{1};
    qint64 m_total_amount_satoshi{0};
    std::vector<RecipientRow> m_rows{};
    bool m_all_valid{true};
    QString m_validation_error;
};

class MockCoinsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString readError READ readError CONSTANT)
    Q_PROPERTY(QString lockError READ lockError CONSTANT)
    Q_PROPERTY(bool lockPending READ lockPending CONSTANT)
    Q_PROPERTY(int markedCount READ markedCount NOTIFY marksChanged)
    Q_PROPERTY(bool marksPending READ marksPending CONSTANT)
public:
    QString readError() const { return {}; }
    QString lockError() const { return {}; }
    bool lockPending() const { return false; }
    bool marksPending() const { return false; }
    int markedCount() const { return std::count_if(m_rows.begin(), m_rows.end(), [](const CoinRow& row) { return row.marked; }); }
    Q_PROPERTY(qint64 totalSelectedSatoshi READ totalSelectedSatoshi NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(qint64 totalSatoshi READ totalSatoshi CONSTANT)
    Q_PROPERTY(qint64 lockedSatoshi READ lockedSatoshi CONSTANT)
    Q_PROPERTY(int visibleCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(QString searchText MEMBER m_search NOTIFY viewChanged)
    Q_PROPERTY(QString sortBy MEMBER m_sort NOTIFY viewChanged)
    Q_PROPERTY(QString filter MEMBER m_filter NOTIFY viewChanged)
    Q_PROPERTY(QString groupBy MEMBER m_group NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending MEMBER m_descending NOTIFY viewChanged)
    Q_PROPERTY(int selectedCoinsCount READ selectedCoinsCount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(int coinCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(QString totalSelected READ totalSelected NOTIFY totalSelectedChanged)
    Q_PROPERTY(bool overRequiredAmount READ overRequiredAmount NOTIFY overRequiredAmountChanged)
    Q_PROPERTY(QString changeAmount READ changeAmount NOTIFY changeAmountChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        LabelRole,
        LockedRole,
        SelectedRole, DateRole, IdRole, GroupRole, SatoshiRole, GroupFirstRole, GroupLastRole, MarkedRole
    };

    struct CoinRow {
        QString address;
        QString amount;
        QString label;
        bool locked;
        bool selected;
        QString id;
        bool marked{false};
    };

    MockCoinsListModel()
    {
        m_rows = {
            {QStringLiteral("bcrt1qcoin1"), QStringLiteral("0.00100000 BTC"), QStringLiteral("utxo-1"), false, false, QStringLiteral("coin-1")},
            {QStringLiteral("bcrt1qcoin2"), QStringLiteral("0.00200000 BTC"), QStringLiteral("utxo-2"), false, false, QStringLiteral("coin-2")},
            {QStringLiteral("bcrt1qcoin3"), QStringLiteral("0.00300000 BTC"), QStringLiteral(""), true, false, QStringLiteral("coin-3")},
        };
    }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return static_cast<int>(m_rows.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const CoinRow& row = m_rows.at(index.row());
        switch (role) {
        case DateRole: return QDateTime::fromSecsSinceEpoch(1700000000);
        case IdRole: return row.id;
        case GroupRole: return QStringLiteral("November 2023");
        case GroupFirstRole: return index.row() == 0;
        case GroupLastRole: return index.row() == rowCount() - 1;
        case SatoshiRole: return (index.row() + 1) * 100000;
        case AddressRole: return row.address;
        case AmountRole: return row.amount;
        case LabelRole: return row.label;
        case LockedRole: return row.locked;
        case SelectedRole: return row.selected;
        case MarkedRole: return row.marked;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {DateRole, "date"}, {IdRole, "coinId"}, {GroupRole, "groupTitle"}, {SatoshiRole, "amountSatoshi"},
            {GroupFirstRole, "groupFirst"}, {GroupLastRole, "groupLast"},
            {AddressRole, "address"},
            {AmountRole, "amount"},
            {LabelRole, "label"},
            {LockedRole, "locked"},
            {SelectedRole, "selected"},
            {MarkedRole, "walletMarked"},
        };
    }

    int selectedCoinsCount() const
    {
        return std::count_if(m_rows.begin(), m_rows.end(), [](const CoinRow& row) { return row.selected; });
    }

    qint64 totalSelectedSatoshi() const { qint64 sum = 0; for (size_t i=0;i<m_rows.size();++i) if(m_rows[i].selected) sum += (i+1)*100000; return sum; }
    qint64 totalSatoshi() const { return 600000; }
    qint64 lockedSatoshi() const { return 300000; }
    Q_INVOKABLE void beginSelection() { m_snapshot = m_rows; }
    Q_INVOKABLE void applySelection() { m_snapshot.clear(); }
    Q_INVOKABLE void cancelSelection() { if (m_snapshot.empty()) return; beginResetModel(); m_rows=m_snapshot; m_snapshot.clear(); endResetModel(); emitAggregateSignals(); }
    Q_INVOKABLE bool setCoinsLocked(const QStringList&, bool) { return true; }
    Q_INVOKABLE bool toggleMarkById(const QString& id)
    {
        for (int row = 0; row < rowCount(); ++row) {
            auto& coin = m_rows[row]; if (coin.id != id) continue;
            coin.marked = !coin.marked;
            Q_EMIT dataChanged(index(row), index(row), {MarkedRole}); Q_EMIT marksChanged(); return true;
        }
        return false;
    }
    Q_INVOKABLE bool clearMarks()
    {
        for (auto& coin : m_rows) coin.marked = false;
        if (rowCount()) Q_EMIT dataChanged(index(0), index(rowCount() - 1), {MarkedRole});
        Q_EMIT marksChanged(); return true;
    }
    Q_INVOKABLE bool lockMarkedCoins(bool locked)
    {
        for (auto& coin : m_rows) if (coin.marked) coin.locked = locked;
        clearMarks(); return true;
    }
    int coinCount() const { return rowCount(); }

    QString totalSelected() const
    {
        return QStringLiteral("%1 selected").arg(selectedCoinsCount());
    }

    bool overRequiredAmount() const { return selectedCoinsCount() > 1; }

    QString changeAmount() const
    {
        return overRequiredAmount() ? QStringLiteral("0.00050000 BTC") : QStringLiteral("0.00000000 BTC");
    }

    Q_INVOKABLE void toggleCoinSelection(int index)
    {
        if (index < 0 || index >= rowCount()) return;
        CoinRow& row = m_rows[static_cast<size_t>(index)];
        if (row.locked) return;
        row.selected = !row.selected;
        const QModelIndex model_index = createIndex(index, 0);
        Q_EMIT dataChanged(model_index, model_index, {SelectedRole});
        emitAggregateSignals();
    }

    Q_INVOKABLE bool toggleCoinSelectionById(const QString& id)
    {
        for (int i = 0; i < rowCount(); ++i) {
            CoinRow& row = m_rows[static_cast<size_t>(i)];
            if (row.id != id) continue;
            row.selected = !row.selected;
            const QModelIndex model_index = createIndex(i, 0);
            Q_EMIT dataChanged(model_index, model_index, {SelectedRole});
            emitAggregateSignals();
            return true;
        }
        return false;
    }

    Q_INVOKABLE bool isCoinSelected(const QString& id) const
    {
        const auto coin = std::find_if(m_rows.begin(), m_rows.end(), [&id](const CoinRow& row) { return row.id == id; });
        return coin != m_rows.end() && coin->selected;
    }

    Q_INVOKABLE void prependCoinForTest()
    {
        beginInsertRows(QModelIndex{}, 0, 0);
        m_rows.insert(m_rows.begin(), {QStringLiteral("bcrt1qnewcoin"), QStringLiteral("0.00400000 BTC"),
                                      QStringLiteral("new-coin"), false, false, QStringLiteral("new-coin")});
        endInsertRows();
        Q_EMIT coinCountChanged();
    }

    Q_INVOKABLE void removeCoinForTest(const QString& id)
    {
        const auto coin = std::find_if(m_rows.begin(), m_rows.end(), [&id](const CoinRow& row) { return row.id == id; });
        if (coin == m_rows.end()) return;
        const int index = static_cast<int>(std::distance(m_rows.begin(), coin));
        beginRemoveRows(QModelIndex{}, index, index);
        m_rows.erase(coin);
        endRemoveRows();
        Q_EMIT coinCountChanged();
    }

    Q_INVOKABLE void reset()
    {
        beginResetModel();
        m_rows = {
            {QStringLiteral("bcrt1qcoin1"), QStringLiteral("0.00100000 BTC"), QStringLiteral("utxo-1"), false, false, QStringLiteral("coin-1")},
            {QStringLiteral("bcrt1qcoin2"), QStringLiteral("0.00200000 BTC"), QStringLiteral("utxo-2"), false, false, QStringLiteral("coin-2")},
            {QStringLiteral("bcrt1qcoin3"), QStringLiteral("0.00300000 BTC"), QStringLiteral(""), true, false, QStringLiteral("coin-3")},
        };
        endResetModel();
        emitAggregateSignals();
    }

    Q_INVOKABLE void update() {}

Q_SIGNALS:
    void marksChanged();
    void viewChanged();
    void selectedCoinsCountChanged();
    void coinCountChanged();
    void totalSelectedChanged();
    void overRequiredAmountChanged();
    void changeAmountChanged();

private:
    void emitAggregateSignals()
    {
        Q_EMIT selectedCoinsCountChanged();
        Q_EMIT totalSelectedChanged();
        Q_EMIT overRequiredAmountChanged();
        Q_EMIT changeAmountChanged();
    }

    std::vector<CoinRow> m_rows{}, m_snapshot{};
    QString m_search, m_sort{"date"}, m_filter{"all"}, m_group{"month"};
    bool m_descending{true};
};

class MockBackupFileOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int phase MEMBER m_phase NOTIFY changed)
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired MEMBER m_confirmation NOTIFY changed)
    Q_PROPERTY(bool replacing MEMBER m_replacing NOTIFY changed)
    Q_PROPERTY(QString finalPath MEMBER m_path NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
public:
    explicit MockBackupFileOperation(QObject* parent = nullptr) : QObject(parent) {}
    enum Phase { Queued, Inspecting, AwaitingConfirmation, Ready, Writing, Succeeded, Failed, Declined, Retired };
    Q_ENUM(Phase)
    int m_phase{Succeeded};
    bool m_pending{false}, m_confirmation{false}, m_replacing{false};
    QString m_path, m_error;
    Q_INVOKABLE bool approve() { m_phase = Succeeded; m_pending = m_confirmation = false; Q_EMIT changed(); return true; }
    Q_INVOKABLE bool refuse() { m_phase = Declined; m_pending = m_confirmation = false; Q_EMIT changed(); return true; }
Q_SIGNALS:
    void changed();
};

class MockImageExportOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool saved MEMBER m_saved NOTIFY changed)
    Q_PROPERTY(QString errorText MEMBER m_error NOTIFY changed)
    Q_PROPERTY(QUrl path MEMBER m_path CONSTANT)
    Q_PROPERTY(int retireCalls MEMBER m_retired NOTIFY changed)
public:
    explicit MockImageExportOperation(QObject* parent = nullptr) : QObject(parent) {}
    bool m_pending{true}, m_saved{false};
    int m_retired{0};
    QUrl m_path;
    QString m_error;
    Q_INVOKABLE void retire() { ++m_retired; Q_EMIT changed(); }
    Q_INVOKABLE void complete(bool saved, const QString& error) {
        m_pending = false; m_saved = saved; m_error = error; Q_EMIT changed();
    }
Q_SIGNALS:
    void changed();
};

// Presentation fixture only. Public-interface effects and crypto are covered
// by SignVerifyMessageModelTests and independent Core/RPC integration tests.
class MockSignVerifyMessageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString signature MEMBER m_signature NOTIFY signatureChanged)
    Q_PROPERTY(QString signingError MEMBER m_signing_error NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingNeedsUnlock MEMBER m_needs_unlock NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingBusy MEMBER m_sign_busy NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingSucceeded READ signingSucceeded NOTIFY signingStateChanged)
    Q_PROPERTY(quint64 signingId MEMBER m_sign_id NOTIFY signingStateChanged)
    Q_PROPERTY(QString signingAddress READ signingAddress WRITE setSigningAddress NOTIFY inputsChanged)
    Q_PROPERTY(QString signingMessage READ signingMessage WRITE setSigningMessage NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationAddress READ verificationAddress WRITE setVerificationAddress NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationMessage READ verificationMessage WRITE setVerificationMessage NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationSignature READ verificationSignature WRITE setVerificationSignature NOTIFY inputsChanged)
    Q_PROPERTY(bool signingAddressValid READ signingAddressValid NOTIFY inputsChanged)
    Q_PROPERTY(bool verificationAddressValid READ verificationAddressValid NOTIFY inputsChanged)
    Q_PROPERTY(bool addressValidationPending READ addressValidationPending CONSTANT)
    Q_PROPERTY(bool verificationBusy MEMBER m_verify_busy NOTIFY verificationChanged)
    Q_PROPERTY(bool verificationValid MEMBER m_verify_valid NOTIFY verificationChanged)
    Q_PROPERTY(QString verificationText MEMBER m_verify_text NOTIFY verificationChanged)
    Q_PROPERTY(quint64 verificationId MEMBER m_verify_id NOTIFY verificationChanged)
public:
    QString signingAddress() const { return m_sign_address; }
    QString signingMessage() const { return m_sign_message; }
    QString verificationAddress() const { return m_verify_address; }
    QString verificationMessage() const { return m_verify_message; }
    QString verificationSignature() const { return m_verify_signature; }
    bool signingSucceeded() const { return !m_signature.isEmpty(); }
    bool signingAddressValid() const { return !m_sign_address.isEmpty(); }
    bool verificationAddressValid() const { return !m_verify_address.isEmpty(); }
    bool addressValidationPending() const { return false; }
    void setSigningAddress(const QString& value) { if (m_sign_address == value) return; m_sign_address = value; clearSigningStatus(); Q_EMIT inputsChanged(); }
    void setSigningMessage(const QString& value) { if (m_sign_message == value) return; m_sign_message = value; clearSigningStatus(); Q_EMIT inputsChanged(); }
    void setVerificationAddress(const QString& value) { if (m_verify_address == value) return; m_verify_address = value; clearVerification(); Q_EMIT inputsChanged(); }
    void setVerificationMessage(const QString& value) { if (m_verify_message == value) return; m_verify_message = value; clearVerification(); Q_EMIT inputsChanged(); }
    void setVerificationSignature(const QString& value) { if (m_verify_signature == value) return; m_verify_signature = value; clearVerification(); Q_EMIT inputsChanged(); }
    Q_INVOKABLE void clear() { clearSigningStatus(); clearVerification(); }
    Q_INVOKABLE void clearSigningStatus() { ++m_sign_revision; m_signature.clear(); m_signing_error.clear(); m_needs_unlock = false; Q_EMIT signatureChanged(); Q_EMIT signingStateChanged(); }
    Q_INVOKABLE void clearVerification() { ++m_verify_revision; m_verify_valid = false; m_verify_text.clear(); Q_EMIT verificationChanged(); }
    Q_INVOKABLE quint64 requestSignMessage(const QString& address, const QString& message)
    {
        if (m_sign_busy) return 0;
        setSigningAddress(address); setSigningMessage(message);
        const auto id = ++m_sign_id, revision = m_sign_revision;
        m_sign_busy = true; Q_EMIT signingStateChanged();
        QTimer::singleShot(0, this, [this, id, revision] {
            if (id != m_sign_id) return;
            m_sign_busy = false;
            if (revision == m_sign_revision) m_signature = QStringLiteral("test-signature");
            Q_EMIT signatureChanged(); Q_EMIT signingStateChanged();
        });
        return id;
    }
    Q_INVOKABLE quint64 requestSignMessageWithPassphrase(const QString& address, const QString& message, const QString&)
    { return requestSignMessage(address, message); }
    Q_INVOKABLE quint64 requestVerifyMessage(const QString& address, const QString& message, const QString& signature)
    {
        if (m_verify_busy) return 0;
        setVerificationAddress(address); setVerificationMessage(message); setVerificationSignature(signature);
        const auto id = ++m_verify_id, revision = m_verify_revision;
        m_verify_busy = true; Q_EMIT verificationChanged();
        QTimer::singleShot(0, this, [this, id, revision, signature] {
            if (id != m_verify_id) return;
            m_verify_busy = false;
            if (revision == m_verify_revision) {
                m_verify_valid = signature == QStringLiteral("test-signature");
                m_verify_text = m_verify_valid ? QStringLiteral("Message verified successfully.") : QStringLiteral("Message verification failed.");
            }
            Q_EMIT verificationChanged();
        });
        return id;
    }
Q_SIGNALS:
    void signatureChanged();
    void signingStateChanged();
    void verificationChanged();
    void inputsChanged();
private:
    QString m_signature, m_signing_error, m_sign_address, m_sign_message, m_verify_address, m_verify_message, m_verify_signature, m_verify_text;
    bool m_needs_unlock{false}, m_sign_busy{false}, m_verify_busy{false}, m_verify_valid{false};
    quint64 m_sign_id{0}, m_verify_id{0}, m_sign_revision{0}, m_verify_revision{0};
};

// Presentation fixture only. Native executor/ownership behavior is exercised by
// test_flowdetailsmodel.cpp; this supplies prepared rows to QML component tests.
class MockFlowRows : public QAbstractListModel
{
public:
    explicit MockFlowRows(QObject* parent) : QAbstractListModel(parent) {}
    QVariantList values;
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : values.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}}; }
    QVariant data(const QModelIndex& index, int role) const override { return index.isValid() && index.row() < values.size() && (role == Qt::UserRole || role == Qt::DisplayRole) ? values[index.row()] : QVariant{}; }
    void set(QVariantList rows) { beginResetModel(); values = std::move(rows); endResetModel(); }
};
class MockFlowDetails : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* context READ context NOTIFY snapshotChanged)
    Q_PROPERTY(quint64 revision MEMBER m_revision NOTIFY snapshotChanged)
    Q_PROPERTY(bool pending READ pending CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
    Q_PROPERTY(QAbstractItemModel* inputRows READ inputRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* outputRows READ outputRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* recipientRows READ recipientRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* entryDocument READ entryDocument CONSTANT)
    Q_PROPERTY(bool entryDetailsBusy MEMBER m_document_pending NOTIFY entryDetailsChanged)
    Q_PROPERTY(QString entryDetailsError READ error CONSTANT)
public:
    explicit MockFlowDetails(QObject* parent) : QObject(parent) {}
    bool pending() const { return false; } QString error() const { return {}; } QObject* context() const { return m_context; }
    QAbstractItemModel* inputRows() { return &m_inputs; } QAbstractItemModel* outputRows() { return &m_outputs; }
    QAbstractItemModel* recipientRows() { return &m_recipients; } QAbstractItemModel* entryDocument() { return &m_document; }
    void setSource(QVariantMap flow, QVariantList recipients, QObject* context, quint64 revision) {
        m_flow = std::move(flow); m_full_recipients = std::move(recipients); m_context = context; m_revision = revision;
        QVariantList inputs, outputs, values;
        for (const auto& row : m_flow.value("inputs").toList()) inputs.append(BoundedFlowEntry(row.toMap()));
        for (const auto& row : m_flow.value("outputs").toList()) outputs.append(BoundedFlowEntry(row.toMap()));
        for (const auto& row : m_full_recipients) values.append(BoundedFlowEntry(row.toMap()));
        m_inputs.set(inputs); m_outputs.set(outputs); m_recipients.set(values); m_document.set({}); m_document_pending = false;
        Q_EMIT snapshotChanged(); Q_EMIT entryDetailsChanged();
    }
    Q_INVOKABLE quint64 requestEntryDetails(const QString& side, int row, QObject* context, quint64 revision) {
        if (context != m_context || revision != m_revision || row < 0) return 0;
        const auto values = side == "recipients" ? m_full_recipients : m_flow.value(side).toList(); if (row >= values.size()) return 0;
        const auto& input = values[row]; const auto ticket = ++m_ticket; m_document_pending = true; Q_EMIT entryDetailsChanged();
        QTimer::singleShot(0, this, [this, input, ticket, revision] {
            if (ticket != m_ticket || revision != m_revision) return;
            m_document.set(FlowEntryDocument(input)); m_document_pending = false; Q_EMIT entryDetailsChanged();
        }); return ticket;
    }
Q_SIGNALS:
    void snapshotChanged(); void entryDetailsChanged();
private:
    QPointer<QObject> m_context;
    quint64 m_revision{0}, m_ticket{0}; bool m_document_pending{false};
    QVariantMap m_flow; QVariantList m_full_recipients;
    MockFlowRows m_inputs{this}, m_outputs{this}, m_recipients{this}, m_document{this};
};

// PSBT document presentation owned by the original mock wallet. The async
// command below owns its result separately from this observation object.
class MockPsbtDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit MockPsbtDocument(QObject* parent=nullptr):QObject(parent){}
    QString error()const{return m_error;}
    void setError(QString error){if(m_error==error)return;m_error=std::move(error);Q_EMIT changed();}
    Q_INVOKABLE void clear(){setError({});}
Q_SIGNALS:void changed();
private:QString m_error;
};

class MockWalletQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recipientLabelsPending READ recipientLabelsPending NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsError READ recipientLabelsError NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsTransactionId READ recipientLabelsTransactionId NOTIFY recipientLabelsChanged)
    Q_PROPERTY(qint64 estimatedFeeSatoshi READ estimatedFeeSatoshi NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(QString estimatedFeeRate READ estimatedFeeRate NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(int estimatedInputCount READ estimatedInputCount NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(qint64 sendTotalSatoshi READ sendTotalSatoshi NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(qint64 availableSendBalanceSatoshi READ availableSendBalanceSatoshi NOTIFY balanceChanged)
    Q_PROPERTY(QObject* maximumRecipient READ maximumRecipient NOTIFY maximumRecipientChanged)
    Q_PROPERTY(QString name MEMBER m_name NOTIFY nameChanged)
    Q_PROPERTY(QString displayName MEMBER m_name NOTIFY nameChanged)
    Q_PROPERTY(bool available MEMBER m_available NOTIFY availableChanged)
    Q_PROPERTY(bool capabilitiesKnown MEMBER m_capabilities_known NOTIFY walletInfoChanged)
    Q_PROPERTY(int ensureReceivingAddressCalls MEMBER m_ensure_receiving_calls NOTIFY walletInfoChanged)
    Q_PROPERTY(QString closeQuestionToken MEMBER m_close_question_token NOTIFY closeQuestionChanged)
    Q_PROPERTY(QString balance MEMBER m_balance NOTIFY balanceChanged)
    Q_PROPERTY(bool balanceKnown MEMBER m_balance_known NOTIFY balanceChanged)
    Q_PROPERTY(QObject* transactionActivityModel READ transactionActivityModel CONSTANT)
    Q_PROPERTY(QObject* bumpModel READ bumpModel CONSTANT)
    Q_PROPERTY(QObject* signVerifyMessageModel READ signVerifyMessageModel CONSTANT)
    Q_PROPERTY(QObject* recipients READ recipients CONSTANT)
    Q_PROPERTY(QObject* coinsListModel READ coinsListModel CONSTANT)
    Q_PROPERTY(QObject* currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)
    Q_PROPERTY(QObject* currentTransactionFlowDetails READ currentTransactionFlowDetails CONSTANT)
    Q_PROPERTY(quint64 currentTransactionFlowRevision READ currentTransactionFlowRevision NOTIFY currentTransactionChanged)
    Q_PROPERTY(QVariantMap currentTransactionFlow READ currentTransactionFlow WRITE setCurrentTransactionFlow NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionIsImportedPsbt MEMBER m_current_transaction_is_imported_psbt NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanSend MEMBER m_current_transaction_can_send NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool transactionFeePending MEMBER m_transaction_fee_pending NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanBroadcast MEMBER m_current_transaction_can_broadcast NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString currentTransactionReviewMessage MEMBER m_current_transaction_review_message NOTIFY currentTransactionChanged)
    Q_PROPERTY(QObject* currentPaymentRequest READ currentPaymentRequest CONSTANT)
    Q_PROPERTY(QObject* receivingAddress READ receivingAddress CONSTANT)
    Q_PROPERTY(bool ensureReceivingAddressResult MEMBER m_ensure_receiving_address_result)
    Q_PROPERTY(QObject* detailPaymentRequest READ detailPaymentRequest CONSTANT)
    Q_PROPERTY(QObject* receiveRequests READ receiveRequests CONSTANT)
    Q_PROPERTY(bool receiveRequestReconciliationPending MEMBER m_receive_request_reconciliation_pending NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(QString receiveRequestReconciliationError MEMBER m_receive_reconciliation_error NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(int receiveRequestReloadCalls MEMBER m_receive_reload_calls NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(MockAddressListModel* addressListModel READ addressListModel CONSTANT)
    Q_PROPERTY(bool hasExternalSigner MEMBER m_has_external_signer NOTIFY walletInfoChanged)
    Q_PROPERTY(int displayUnit MEMBER m_display_unit NOTIFY displayUnitChanged)
    Q_PROPERTY(int targetBlocks READ targetBlocks WRITE setTargetBlocks NOTIFY targetBlocksChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool customFeeEnabled READ customFeeEnabled WRITE setCustomFeeEnabled NOTIFY customFeeEnabledChanged)
    Q_PROPERTY(QString customFeeRate READ customFeeRate WRITE setCustomFeeRate NOTIFY customFeeRateChanged)
    Q_PROPERTY(bool customFeeRateValid READ customFeeRateValid NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(double minimumCustomFeeRate MEMBER m_minimum_custom_fee_rate NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(bool feeEstimatePending MEMBER m_fee_estimate_pending NOTIFY feeEstimatePendingChanged)
    Q_PROPERTY(int feeEstimateRevision MEMBER m_fee_estimate_revision NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool sendAmountExhaustsBalance READ sendAmountExhaustsBalance WRITE setSendAmountExhaustsBalance NOTIFY sendAmountExhaustsBalanceChanged)
    Q_PROPERTY(bool prepareTransactionResult MEMBER m_prepare_transaction_result NOTIFY prepareTransactionResultChanged)
    Q_PROPERTY(bool sendTransactionResult MEMBER m_send_transaction_result NOTIFY sendTransactionResultChanged)
    Q_PROPERTY(int prepareTransactionCalls READ prepareTransactionCalls NOTIFY prepareTransactionCallsChanged)
    Q_PROPERTY(int preparationState MEMBER m_preparation_state NOTIFY preparationChanged)
    Q_PROPERTY(quint64 preparationId MEMBER m_preparation_ticket NOTIFY preparationChanged)
    Q_PROPERTY(bool preparationBusy READ preparationBusy NOTIFY preparationChanged)
    Q_PROPERTY(bool customFeeTargetPending READ customFeeTargetPending CONSTANT)
    Q_PROPERTY(bool coinSelectionPending READ coinSelectionPending CONSTANT)
    Q_PROPERTY(bool holdPreparationForTest MEMBER m_hold_preparation)
    Q_PROPERTY(int scheduleFeeEstimatesCalls READ scheduleFeeEstimatesCalls NOTIFY scheduleFeeEstimatesCallsChanged)
    Q_PROPERTY(int sendTransactionCalls READ sendTransactionCalls NOTIFY sendTransactionCallsChanged)
    Q_PROPERTY(int sendState READ sendState NOTIFY sendOperationChanged)
    Q_PROPERTY(quint64 sendOperationId MEMBER m_send_operation_id NOTIFY sendOperationChanged)
    Q_PROPERTY(int signerApprovalState MEMBER m_approval_state NOTIFY signerApprovalChanged)
    Q_PROPERTY(QString signerApprovalError MEMBER m_approval_error NOTIFY signerApprovalChanged)
    Q_PROPERTY(bool sendBusy READ sendBusy NOTIFY sendOperationChanged)
    Q_PROPERTY(QString sendResultTxid READ sendResultTxid NOTIFY sendOperationChanged)
    Q_PROPERTY(int sendSubmission MEMBER m_send_submission NOTIFY sendOperationChanged)
    Q_PROPERTY(bool holdSendForTest MEMBER m_hold_send)
    Q_PROPERTY(int sendTransactionWithPassphraseCalls MEMBER m_send_transaction_with_passphrase_calls NOTIFY sendTransactionCallsChanged)
    Q_PROPERTY(bool sendDraftSweepsWallet MEMBER m_send_draft_sweeps_wallet NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool currentTransactionSweepsWallet MEMBER m_current_transaction_sweeps_wallet NOTIFY currentTransactionChanged)
    Q_PROPERTY(int useMaximumCalls MEMBER m_use_maximum_calls NOTIFY walletInfoChanged)
    Q_PROPERTY(int broadcastCurrentTransactionCalls READ broadcastCurrentTransactionCalls NOTIFY broadcastCurrentTransactionCallsChanged)
    Q_PROPERTY(QString lastSavedPsbtPath MEMBER m_last_saved_psbt_path NOTIFY lastSavedPsbtPathChanged)
    Q_PROPERTY(PsbtActionState psbtActionState READ psbtActionState NOTIFY psbtActionChanged)
    Q_PROPERTY(quint64 psbtActionId READ psbtActionId NOTIFY psbtActionChanged)
    Q_PROPERTY(bool psbtActionBusy READ psbtActionBusy NOTIFY psbtActionChanged)
    Q_PROPERTY(PsbtImportResult psbtImportResult READ psbtImportResult NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtActionError READ psbtActionError NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSavedPath READ psbtSavedPath NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSubmittedTxid READ psbtSubmittedTxid NOTIFY psbtActionChanged)
    Q_PROPERTY(QObject* importedPsbt READ importedPsbt CONSTANT)
    Q_PROPERTY(bool holdPsbtPublicationForTest MEMBER m_hold_psbt_publication)
    Q_PROPERTY(PsbtImportResult psbtImportResultForTest MEMBER m_next_psbt_import_result)
    Q_PROPERTY(int discardCurrentTransactionCalls READ discardCurrentTransactionCalls NOTIFY discardCurrentTransactionCallsChanged)
    Q_PROPERTY(bool isEncrypted MEMBER m_is_encrypted NOTIFY securityStateChanged)
    Q_PROPERTY(bool isLocked MEMBER m_is_locked NOTIFY securityStateChanged)
    Q_PROPERTY(QString keyScheme MEMBER m_key_scheme NOTIFY walletInfoChanged)
    Q_PROPERTY(int keySchemeKind MEMBER m_key_scheme_kind NOTIFY walletInfoChanged)
    Q_PROPERTY(QString privateKeysStatus MEMBER m_private_keys_status NOTIFY walletInfoChanged)
    Q_PROPERTY(QString externalSignerStatus MEMBER m_external_signer_status NOTIFY walletInfoChanged)
    Q_PROPERTY(QVariantList receiveAddressTypes READ availableReceiveAddressTypes NOTIFY walletInfoChanged)
    Q_PROPERTY(bool canManagePassphrase MEMBER m_can_manage_passphrase NOTIFY walletInfoChanged)
    Q_PROPERTY(QString settingsError MEMBER m_settings_error NOTIFY settingsErrorChanged)
    Q_PROPERTY(int securityState MEMBER m_security_state NOTIFY securityOperationChanged)
    Q_PROPERTY(bool securityBusy READ securityBusy NOTIFY securityOperationChanged)
    Q_PROPERTY(QString securityError MEMBER m_settings_error NOTIFY securityOperationChanged)
    Q_PROPERTY(MockBackupFileOperation* backupOperation MEMBER m_backup_operation NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(int backupWalletCalls READ backupWalletCalls NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(QString transactionError MEMBER m_transaction_error NOTIFY transactionErrorChanged)
    Q_PROPERTY(bool transactionNeedsUnlock MEMBER m_transaction_needs_unlock NOTIFY transactionNeedsUnlockChanged)
    Q_PROPERTY(QString lastCommitAddressType MEMBER m_last_commit_address_type NOTIFY lastCommitAddressTypeChanged)
    Q_PROPERTY(QString lastLoadedPaymentRequestId MEMBER m_last_loaded_payment_request_id NOTIFY lastLoadedPaymentRequestIdChanged)
    Q_PROPERTY(QString lastLoadedPaymentRequestDetailId MEMBER m_last_loaded_payment_request_detail_id NOTIFY lastLoadedPaymentRequestDetailIdChanged)
    Q_PROPERTY(QString lastTemplateRequestId MEMBER m_last_template_request_id NOTIFY lastTemplateRequestIdChanged)
    Q_PROPERTY(QString lastRemovedRequestId MEMBER m_last_removed_request_id NOTIFY lastRemovedRequestIdChanged)
    Q_PROPERTY(bool removeReceiveRequestResult MEMBER m_remove_receive_request_result NOTIFY removeReceiveRequestResultChanged)

    Q_PROPERTY(bool receiveRequestsLoaded MEMBER m_requests_loaded NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receiveRequestsBusy MEMBER m_requests_busy NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(QString receiveRequestStorageError MEMBER m_requests_error NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receivingAddressReady MEMBER m_receiving_ready NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(bool receivingAddressPending MEMBER m_receiving_pending NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(bool ensureReceivingAddressNeedsUnlock MEMBER m_ensure_needs_unlock)
public:
    MockWalletQmlModel() { connect(this, &MockWalletQmlModel::currentTransactionChanged, this, [this] {
        ++m_flow_revision;
        m_flow_details.setSource(m_current_transaction_flow, m_current_transaction ? m_current_transaction->property("reviewedRecipients").toList() : QVariantList{}, m_current_transaction, m_flow_revision);
    }); }
    QObject* currentTransactionFlowDetails() { return &m_flow_details; }
    quint64 currentTransactionFlowRevision() const { return m_flow_revision; }

    QVariantMap currentTransactionFlow() const { return {{"presentation", PrepareTransactionFlowPresentation(m_current_transaction_flow)}}; }
    void setCurrentTransactionFlow(const QVariantMap& flow) { m_current_transaction_flow = flow; Q_EMIT currentTransactionChanged(); }

    bool recipientLabelsPending() const { return false; }
    QString recipientLabelsError() const { return m_recipient_labels_error; }
    QString recipientLabelsTransactionId() const { return {}; }
    Q_INVOKABLE void setRecipientLabelsErrorForTest(const QString& error) { m_recipient_labels_error = error; Q_EMIT recipientLabelsChanged(); }

    bool m_receiving_ready{true}, m_receiving_pending{false}, m_ensure_needs_unlock{false};
    quint64 m_address_operation_sequence{0};

    bool m_requests_loaded{true}, m_requests_busy{false};
    QString m_requests_error;

    enum SendSubmission { SubmissionUnknown, SubmissionMempool, SubmissionConfirmed, SubmissionNotInMempool, SubmissionConflicted, SubmissionAbandoned, SubmissionAccepted };
    Q_ENUM(SendSubmission)
    int m_send_submission{SubmissionMempool};
    enum SendState { SendIdle, SendPending, SendNeedsUnlock, SendFailed, SendCommitted, SendUncertain, SendClosed };
    enum PreparationState { PreparationIdle, PreparationPending, PreparationNeedsUnlock, PreparationReady, PreparationFailed, PreparationStale, PreparationClosed, PreparationUncertain };
    Q_ENUM(PreparationState)
    int m_preparation_state{PreparationIdle};
    bool m_hold_preparation{false}, m_pending_preparation_success{false};
    quint64 m_preparation_ticket{0};
    bool preparationBusy() const { return m_preparation_state == PreparationPending; }
    bool customFeeTargetPending() const { return false; }
    bool coinSelectionPending() const { return false; }
    Q_INVOKABLE quint64 requestPrepareTransaction()
    {
        if (preparationBusy() || psbtActionBusy()) return 0;
        m_pending_preparation_success = prepareTransaction();
        m_preparation_state = PreparationPending;
        const auto ticket = ++m_preparation_ticket;
        Q_EMIT preparationChanged();
        if (!m_hold_preparation) QTimer::singleShot(0, this, [this, ticket] {
            if (ticket == m_preparation_ticket) finishPreparationForTest();
        });
        return ticket;
    }
    Q_INVOKABLE quint64 requestPrepareTransactionWithPassphrase(const QString&) { return requestPrepareTransaction(); }
    Q_INVOKABLE void finishPreparationForTest()
    {
        if (!preparationBusy()) return;
        m_preparation_state = m_pending_preparation_success ? PreparationReady
            : m_transaction_needs_unlock ? PreparationNeedsUnlock : PreparationFailed;
        Q_EMIT preparationChanged();
    }
    enum ApprovalState { ApprovalInitial, ApprovalWaiting, ApprovalSigned, ApprovalPartiallySigned, ApprovalFailed, ApprovalClosed, ApprovalOtherReview, ApprovalUncertain };
    Q_ENUM(ApprovalState)
    int m_approval_state{ApprovalInitial};
    QString m_approval_error;
    Q_INVOKABLE void approveExternalSignerTransaction()
    {
        if (m_approval_state == ApprovalWaiting) return;
        m_approval_state = ApprovalWaiting;
        m_approval_error.clear();
        Q_EMIT signerApprovalChanged();
    }
    Q_ENUM(SendState)
    int m_send_state{SendIdle};
    quint64 m_send_operation_id{0};
    bool m_hold_send{false};
    QPointer<QObject> m_send_review;
    int sendState() const { return m_send_state; }
    bool sendBusy() const { return m_send_state == SendPending; }
    QString sendResultTxid() const { return m_current_transaction ? m_current_transaction->property("txid").toString() : QString{}; }
    Q_INVOKABLE void resetSendForTest() { resetPsbtForTest();m_hold_send = false; m_send_state = SendIdle; m_send_submission = SubmissionMempool; m_send_review.clear(); Q_EMIT sendOperationChanged(); }
    Q_INVOKABLE void finishSendForTest() {
        if (!sendBusy()) return;
        m_send_state = m_send_review != m_current_transaction ? SendClosed : m_send_transaction_result ? SendCommitted : m_transaction_needs_unlock ? SendNeedsUnlock : SendFailed;
        Q_EMIT sendOperationChanged();
    }
    Q_INVOKABLE quint64 requestSendTransaction() {
        if (sendBusy() || psbtActionBusy()) return 0; ++m_send_operation_id; m_send_review = m_current_transaction; sendTransaction(); m_send_state = SendPending; Q_EMIT sendOperationChanged();
        if (!m_hold_send) QTimer::singleShot(0, this, [this] { finishSendForTest(); });
        return m_send_operation_id;
    }
    Q_INVOKABLE quint64 requestSendTransactionWithPassphrase(const QString& passphrase) {
        if (sendBusy() || psbtActionBusy()) return 0; ++m_send_operation_id; m_send_review = m_current_transaction; sendTransactionWithPassphrase(passphrase); m_send_state = SendPending; Q_EMIT sendOperationChanged();
        if (!m_hold_send) QTimer::singleShot(0, this, [this] { finishSendForTest(); });
        return m_send_operation_id;
    }
    enum SecurityState { SecurityIdle, SecurityPending, SecuritySucceeded, SecurityFailed, SecurityUncertain };
    Q_ENUM(SecurityState)
    enum class KeyScheme {
        SingleKey = 0,
        WatchOnly,
        MultiKey,
        ExternalSigner,
    };
    Q_ENUM(KeyScheme)
    enum class PsbtImportResult {
        PsbtUnsupported = 0,
        WalletCanSign,
        WalletCannotSign,
        TransactionAlreadyKnown,
        ReviewPending,
    };
    Q_ENUM(PsbtImportResult)
    enum class PsbtActionState { PsbtIdle, PsbtPending, PsbtImported, PsbtKnown, PsbtSaved, PsbtSubmitted, PsbtFailed, PsbtUncertain, PsbtStale, PsbtClosed };
    Q_ENUM(PsbtActionState)
    enum class PsbtKind { Import,Save,Broadcast };
    struct PsbtPacket {
        PsbtKind kind;quint64 ticket{0},review_revision{0};QPointer<QObject> review;
        QString path,txid,error;PsbtImportResult imported{PsbtImportResult::PsbtUnsupported};
        PsbtActionState terminal{PsbtActionState::PsbtFailed};bool allowed{false},ready{false},saved{false},submitted{false};
    };
    PsbtActionState psbtActionState()const{return m_psbt_action_state;}
    quint64 psbtActionId()const{return m_psbt_action_id;}
    bool psbtActionBusy()const{return m_psbt_action_state==PsbtActionState::PsbtPending;}
    PsbtImportResult psbtImportResult()const{return m_psbt_import_result;}
    QString psbtActionError()const{return m_psbt_action_error;}
    QString psbtSavedPath()const{return m_saved_psbt_receipt&&m_saved_psbt_receipt->saved?m_saved_psbt_receipt->path:QString{};}
    QString psbtSubmittedTxid()const{return m_submitted_psbt_receipt&&m_submitted_psbt_receipt->submitted?m_submitted_psbt_receipt->txid:QString{};}
    QObject* importedPsbt(){return &m_imported_psbt;}
    Q_INVOKABLE quint64 requestImportPsbtFromFile(const QString& path){return requestPsbtAction(PsbtKind::Import,path);}
    Q_INVOKABLE quint64 requestSaveCurrentTransactionAsPsbt(const QString& path){return requestPsbtAction(PsbtKind::Save,path);}
    Q_INVOKABLE quint64 requestBroadcastCurrentTransaction(){return requestPsbtAction(PsbtKind::Broadcast,{});}
    Q_INVOKABLE void finishPsbtPublicationForTest(){if(m_psbt_operation&&m_psbt_operation->ready)publishPsbtPacket(m_psbt_operation);}
    Q_INVOKABLE void resetPsbtForTest(){
        m_hold_psbt_publication=false;m_psbt_operation.reset();m_saved_psbt_receipt.reset();m_submitted_psbt_receipt.reset();
        m_psbt_action_state=PsbtActionState::PsbtIdle;m_psbt_import_result=PsbtImportResult::PsbtUnsupported;m_next_psbt_import_result=PsbtImportResult::WalletCanSign;
        m_psbt_action_error.clear();m_imported_psbt.clear();Q_EMIT psbtActionChanged();
    }
    static void completePsbtPacket(const std::shared_ptr<PsbtPacket>& packet){
        if(packet->ready)return;packet->ready=true;
        if(!packet->allowed){packet->terminal=PsbtActionState::PsbtFailed;packet->error=QStringLiteral("The original PSBT action could not be completed.");return;}
        if(packet->kind==PsbtKind::Save){packet->saved=true;packet->terminal=PsbtActionState::PsbtSaved;}
        else if(packet->kind==PsbtKind::Broadcast){packet->submitted=true;packet->terminal=PsbtActionState::PsbtSubmitted;}
        else if(packet->imported==PsbtImportResult::TransactionAlreadyKnown)packet->terminal=PsbtActionState::PsbtKnown;
        else if(packet->imported==PsbtImportResult::PsbtUnsupported){packet->terminal=PsbtActionState::PsbtFailed;packet->error=QStringLiteral("This PSBT is not supported yet.");}
        else packet->terminal=PsbtActionState::PsbtImported;
    }
    quint64 requestPsbtAction(PsbtKind kind,const QString& path){
        if(!m_available||sendBusy()||preparationBusy()||(psbtActionBusy()&&!(kind==PsbtKind::Import&&m_psbt_operation&&m_psbt_operation->kind==PsbtKind::Import)))return 0;
        const QPointer<MockWalletQmlModel> observer{this};
        auto packet=std::make_shared<PsbtPacket>();packet->kind=kind;packet->ticket=++m_psbt_action_id;packet->review_revision=m_review_revision;
        packet->review=m_current_transaction;packet->path=path;packet->txid=m_current_transaction?m_current_transaction->property("txid").toString():QString{};
        packet->imported=kind==PsbtKind::Import?m_next_psbt_import_result:PsbtImportResult::PsbtUnsupported;packet->allowed=kind==PsbtKind::Import?!path.isEmpty():m_current_transaction&&(kind==PsbtKind::Save?!path.isEmpty():m_current_transaction_can_broadcast&&!m_transaction_fee_pending);
        m_psbt_operation=packet;m_psbt_action_state=PsbtActionState::PsbtPending;m_psbt_action_error.clear();m_psbt_import_result=PsbtImportResult::PsbtUnsupported;
        if(kind==PsbtKind::Save){m_saved_psbt_receipt=packet;m_last_saved_psbt_path=path;}
        if(kind==PsbtKind::Broadcast)m_submitted_psbt_receipt=packet;
        // The first turn records the actual original outcome independently of
        // any view. A separate turn publishes only into the matching owner.
        QTimer::singleShot(0,QCoreApplication::instance(),[packet,observer]{
            completePsbtPacket(packet);
            if(observer&&packet->kind==PsbtKind::Broadcast){++observer->m_broadcast_current_transaction_calls;if(observer->m_available)Q_EMIT observer->broadcastCurrentTransactionCallsChanged();}
            QTimer::singleShot(0,QCoreApplication::instance(),[packet,observer]{if(observer&&!observer->m_hold_psbt_publication)observer->publishPsbtPacket(packet);});
        });
        if(kind==PsbtKind::Save){Q_EMIT lastSavedPsbtPathChanged();if(!observer)return packet->ticket;}
        if(m_available&&m_psbt_operation==packet)Q_EMIT psbtActionChanged();return packet->ticket;
    }
    void publishPsbtPacket(const std::shared_ptr<PsbtPacket>& packet){
        if(m_psbt_operation!=packet||!packet->ready)return;
        if(!m_available){m_psbt_action_state=PsbtActionState::PsbtClosed;return;}
        const bool same=packet->review_revision==m_review_revision&&(packet->kind==PsbtKind::Import||packet->review==m_current_transaction);
        m_psbt_action_state=same?packet->terminal:PsbtActionState::PsbtStale;
        m_psbt_action_error=same?packet->error:QStringLiteral("The transaction review changed before publication.");
        m_psbt_import_result=packet->imported;const QPointer<MockWalletQmlModel> observer{this};
        m_imported_psbt.setError(m_psbt_action_error);if(!observer||!m_available||m_psbt_operation!=packet)return;
        if(same&&packet->kind==PsbtKind::Import&&packet->terminal==PsbtActionState::PsbtImported){
            ++m_review_revision;m_current_transaction_is_imported_psbt=true;m_current_transaction_can_send=packet->imported==PsbtImportResult::WalletCanSign;
            m_transaction_fee_pending=packet->imported==PsbtImportResult::ReviewPending;m_current_transaction_can_broadcast=false;Q_EMIT currentTransactionChanged();
        }else if(same&&packet->submitted){m_current_transaction_can_send=false;m_current_transaction_can_broadcast=false;Q_EMIT currentTransactionChanged();}
        if(observer&&m_available&&m_psbt_operation==packet)Q_EMIT psbtActionChanged();
    }



    int m_security_state{SecurityIdle};
    QString m_name{QStringLiteral("testwallet")};
    QString m_balance{QStringLiteral("1.00000000 BTC")};
    bool m_balance_known{true};
    bool m_available{true};
    Q_INVOKABLE void closeBeforeAvailabilityNotificationForTest() { m_available = false; }
    Q_INVOKABLE void checkBeforeReconciliationNotificationForTest() { m_receive_request_reconciliation_pending = true; }
    Q_INVOKABLE MockImageExportOperation* savePaymentRequestImage(MockPaymentRequest*, const QVariant&, const QUrl& path) {
        auto* operation = new MockImageExportOperation(this); operation->m_path = path; return operation;
    }
    QString m_close_question_token;
    quint64 m_close_sequence{0};
    QObject* m_transaction_activity_model{nullptr};
    QObject* m_bump_model{nullptr};
    QObject* m_sign_verify_model{nullptr};
    QObject* m_recipients{nullptr};
    QObject* m_maximum_recipient{nullptr};
    QMetaObject::Connection m_maximum_amount_connection;
    QObject* m_coins_list_model{nullptr};
    QObject* m_current_transaction{nullptr};
    QObject* m_current_payment_request{nullptr};
    MockReceiveRequests m_receive_requests{};
    bool m_has_external_signer{false};
    int m_display_unit{0};
    QString m_default_receive_address_type{QStringLiteral("bech32")};
    QString m_last_commit_address_type;
    QString m_last_loaded_payment_request_id;
    QString m_last_loaded_payment_request_detail_id;
    QString m_last_template_request_id;
    QString m_last_removed_request_id;
    bool m_remove_receive_request_result{true};
    bool m_receive_request_reconciliation_pending{false};
    QString m_receive_reconciliation_error;
    int m_receive_reload_calls{0};
    Q_INVOKABLE void reloadReceiveRequests() { ++m_receive_reload_calls; Q_EMIT receiveRequestReconciliationPendingChanged(); }
    QString m_saved_payment_request_label;
    QString m_saved_payment_request_message;
    QString m_saved_payment_request_note_self;
    qint64 m_saved_payment_request_amount_sat{0};
    QString m_saved_payment_request_address_type;
    QString m_saved_payment_request_address;
    int m_target_blocks{2};
    bool m_prepare_transaction_result{true};
    bool m_current_transaction_can_send{true};
    QVariantMap m_current_transaction_flow;
    bool m_current_transaction_is_imported_psbt{false};
    bool m_current_transaction_can_broadcast{false};
    bool m_transaction_fee_pending{false};
    QString m_current_transaction_review_message;
    QString m_last_saved_psbt_path;

    QObject* transactionActivityModel() const { return m_transaction_activity_model; }
    QObject* bumpModel() const { return m_bump_model; }
    QObject* signVerifyMessageModel() const { return m_sign_verify_model; }
    QObject* recipients() const { return m_recipients; }
    QObject* coinsListModel() const { return m_coins_list_model; }
    QObject* currentTransaction() const { return m_current_transaction; }
    QObject* currentPaymentRequest() const { return m_current_payment_request; }
    MockPaymentRequest m_receiving_address;
    QObject* receivingAddress() { return &m_receiving_address; }
    int m_receiving_sequence{0}, m_ensure_receiving_calls{0};
    bool m_capabilities_known{true};
    bool m_ensure_receiving_address_result{true};
    Q_INVOKABLE MockReceiveRequestOperation* ensureReceivingAddress(bool next = false, const QString& type = {})
    {
        ++m_ensure_receiving_calls;
        if (!m_ensure_receiving_address_result) return nullptr;
        const QString selected = type.isEmpty() ? m_default_receive_address_type : type;
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_token = QString::number(++m_address_operation_sequence);
        operation->m_view_revision = m_receiving_address.m_revision;
        operation->m_next = next; operation->m_type = selected;
        if (m_ensure_needs_unlock) {
            m_receiving_ready = false; Q_EMIT receivingAddressStateChanged();
            operation->m_error_code = MockReceiveRequestOperation::NeedsUnlock;
            return operation;
        }
        if (next || m_receiving_address.m_address.isEmpty() || m_receiving_address.m_payment_received || selected != m_receiving_address.m_address_type) {
            m_receiving_address.clear();
            m_receiving_address.m_address = QStringLiteral("bcrt1qreceivingaddress%1").arg(++m_receiving_sequence);
            m_receiving_address.m_address_type = selected;
            m_default_receive_address_type = selected;
            Q_EMIT m_receiving_address.addressChanged();
            Q_EMIT m_receiving_address.addressTypeChanged();
        }
        m_receiving_ready = true; Q_EMIT receivingAddressStateChanged();
        operation->m_address_ready = true; operation->m_view_revision = m_receiving_address.m_revision;
        return operation;
    }
    Q_INVOKABLE MockReceiveRequestOperation* ensureReceivingAddressWithPassphrase(const QString&, bool next = false, const QString& type = {})
    { return ensureReceivingAddress(next, type); }
    Q_INVOKABLE MockReceiveRequestOperation* resumeReceivingAddress(MockReceiveRequestOperation* original, const QString&)
    {
        if (!original) return nullptr;
        m_ensure_needs_unlock = false;
        return ensureReceivingAddress(original->m_next, original->m_type);
    }
    Q_INVOKABLE MockReceiveRequestOperation* retryReceivingAddress(MockReceiveRequestOperation* original)
    { return original ? ensureReceivingAddress(original->m_next, original->m_type) : nullptr; }
    Q_INVOKABLE QString receiveAddressTypeLabel(const QString& type) const { return type; }
    Q_INVOKABLE MockReceiveRequestOperation* pendingReceiveOperation(const QString& id)
    {
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_id = id; operation->m_pending = true; return operation;
    }
    Q_INVOKABLE MockReceiveRequestOperation* commitReceivingPaymentRequest()
    {
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request || m_receiving_address.m_payment_received || m_receiving_address.m_address.isEmpty()) return nullptr;
        request->m_address = m_receiving_address.m_address;
        request->m_address_type = m_receiving_address.m_address_type;
        if (!commitPaymentRequest()) return nullptr;
        m_receiving_address.clear();
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_saved = true; operation->m_id = request->m_id;
        return operation;
    }
    MockPaymentRequest m_detail_payment_request;
    QObject* detailPaymentRequest() { return &m_detail_payment_request; }
    QObject* receiveRequests() { return &m_receive_requests; }
    MockAddressListModel* addressListModel() { return &m_address_list_model; }
    Q_INVOKABLE QVariantList availableReceiveAddressTypes() const
    {
        return {
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("bech32m")},
                {QStringLiteral("label"), QStringLiteral("Bech32m (Taproot)")},
                {QStringLiteral("description"), QStringLiteral("Lower fees")},
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("bech32")},
                {QStringLiteral("label"), QStringLiteral("Bech32 (SegWit)")},
                {QStringLiteral("description"), QStringLiteral("Widely supported")},
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("p2sh-segwit")},
                {QStringLiteral("label"), QStringLiteral("Base58 (P2SH-SegWit)")},
                {QStringLiteral("description"), QStringLiteral("Backward compatible")},
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("legacy")},
                {QStringLiteral("label"), QStringLiteral("Base58 (Legacy)")},
                {QStringLiteral("description"), QStringLiteral("Older wallet compatibility")},
            },
        };
    }
    Q_INVOKABLE QString defaultReceiveAddressType() const { return m_default_receive_address_type; }
    qint64 estimatedFeeSatoshi() const { return m_custom_fee_enabled ? 600 : m_target_blocks == 2 ? 750 : m_target_blocks == 6 ? 500 : 250; }
    QString estimatedFeeRate() const { return m_custom_fee_enabled ? m_custom_fee_rate : QStringLiteral("2.5"); }
    int estimatedInputCount() const { return 1; }
    qint64 sendTotalSatoshi() const { const auto* r = qobject_cast<MockRecipientsModel*>(m_recipients); return r ? r->totalAmountSatoshi() + estimatedFeeSatoshi() : 0; }
    qint64 availableSendBalanceSatoshi() const { return 1000000; }
    QObject* maximumRecipient() const { return m_maximum_recipient; }
    Q_INVOKABLE void clearMaximum()
    {
        if (!m_maximum_recipient) return;
        QObject::disconnect(m_maximum_amount_connection);
        m_maximum_recipient = nullptr;
        Q_EMIT maximumRecipientChanged();
    }
    Q_INVOKABLE void clearSelectedCoins() { clearMaximum(); if (auto* c = qobject_cast<MockCoinsListModel*>(m_coins_list_model)) c->reset(); }
    bool m_send_draft_sweeps_wallet{false};
    bool m_current_transaction_sweeps_wallet{false};
    int m_use_maximum_calls{0};
    Q_INVOKABLE void useMaximum()
    {
        ++m_use_maximum_calls;
        auto* recipients = qobject_cast<MockRecipientsModel*>(m_recipients);
        auto* recipient = recipients ? qobject_cast<MockSendRecipient*>(recipients->current()) : nullptr;
        if (recipient && recipient != m_maximum_recipient) {
            clearMaximum();
            m_maximum_recipient = recipient;
            m_maximum_amount_connection = connect(&recipient->m_amount, &MockBitcoinAmount::amountChanged, this, [this] { clearMaximum(); });
            Q_EMIT maximumRecipientChanged();
        }
        Q_EMIT walletInfoChanged();
    }
    Q_INVOKABLE void setCustomFeeTarget(int target) { setTargetBlocks(target); setCustomFeeEnabled(true); setCustomFeeRate("2.5"); }
    int targetBlocks() const { return m_target_blocks; }
    QString estimatedFee() const
    {
        return m_custom_fee_enabled ? m_custom_fee_estimate : estimatedFeeForTarget(m_target_blocks);
    }
    bool customFeeEnabled() const { return m_custom_fee_enabled; }
    QString customFeeRate() const { return m_custom_fee_rate; }
    bool customFeeRateValid() const
    {
        static const QRegularExpression pattern{
            QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)")
        };
        return pattern.match(m_custom_fee_rate).hasMatch() && m_custom_fee_rate.toDouble() >= m_minimum_custom_fee_rate;
    }
    double m_minimum_custom_fee_rate{0.1};
    int prepareTransactionCalls() const { return m_prepare_transaction_calls; }
    int scheduleFeeEstimatesCalls() const { return m_schedule_fee_estimates_calls; }
    int sendTransactionCalls() const { return m_send_transaction_calls; }
    int broadcastCurrentTransactionCalls() const { return m_broadcast_current_transaction_calls; }
    int discardCurrentTransactionCalls() const { return m_discard_current_transaction_calls; }
    bool sendAmountExhaustsBalance() const { return m_send_amount_exhausts_balance; }
    QString lastBackupPath() const { return m_last_backup_path; }
    int backupWalletCalls() const { return m_backup_wallet_calls; }
    Q_INVOKABLE QString estimatedFeeForTarget(const int target) const
    {
        QString estimate = m_fee_estimates.value(target);
        if (!estimate.isEmpty()) {
            return estimate;
        }

        return {};
    }
    Q_INVOKABLE int feeTargetIndex(const int target) const
    {
        switch (target) {
        case 1: return 0;
        case 6: return 2;
        case 2:
        default:
            return 1;
        }
    }
    void setTransactionActivityModel(QObject* model) { m_transaction_activity_model = model; }
    void setBumpModel(QObject* model) { m_bump_model = model; }
    void setSignVerifyMessageModel(QObject* model) { m_sign_verify_model = model; }
    void setRecipients(QObject* model) { m_recipients = model; }
    void setCoinsListModel(QObject* model) { m_coins_list_model = model; }
    void setCurrentTransaction(QObject* transaction) {
        ++m_review_revision;QObject::disconnect(m_review_rows_connection); m_current_transaction = transaction;
        if (transaction) m_review_rows_connection = connect(transaction, SIGNAL(reviewedRecipientsChanged()), this, SIGNAL(currentTransactionChanged()));
        Q_EMIT currentTransactionChanged();
    }
    void setCurrentPaymentRequest(QObject* request) { m_current_payment_request = request; }
    Q_INVOKABLE void loadPaymentRequest(const QString& request_id)
    {
        m_last_loaded_payment_request_id = request_id;
        Q_EMIT lastLoadedPaymentRequestIdChanged();
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_id = request_id;
        Q_EMIT request->idChanged();
    }
    Q_INVOKABLE bool loadPaymentRequestDetail(const QString& request_id)
    {
        m_last_loaded_payment_request_detail_id = request_id;
        Q_EMIT lastLoadedPaymentRequestDetailIdChanged();
        auto* request = &m_detail_payment_request;
        request->clear();
        if (!request) return false;
        request->m_id = request_id;
        request->m_label = request_id == "1" ? m_saved_payment_request_label : QStringLiteral("Alice");
        request->m_message = request_id == "1" ? m_saved_payment_request_message : QStringLiteral("Coffee");
        request->m_note_self = request_id == "1" ? m_saved_payment_request_note_self : QStringLiteral("Counter");
        request->m_address = request_id == "1" ? m_saved_payment_request_address : QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        request->m_created_iso = QStringLiteral("2026-01-02T00:00:00Z");
        request->m_is_editing = false;
        request->m_amount.setUnit(MockBitcoinAmount::BTC);
        request->m_amount.setSatoshi(request_id == "1" ? m_saved_payment_request_amount_sat : 100'000);
        request->m_address_type = m_saved_payment_request_address_type;
        Q_EMIT request->idChanged();
        Q_EMIT request->labelChanged();
        Q_EMIT request->messageChanged();
        Q_EMIT request->noteSelfChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->createdIsoChanged();
        Q_EMIT request->isEditingChanged();
        Q_EMIT request->m_amount.displayChanged();
        Q_EMIT request->m_amount.unitChanged();
        Q_EMIT request->m_amount.amountChanged();
        return true;
    }
    void setTargetBlocks(const int value)
    {
        if (m_target_blocks == value) return;
        m_target_blocks = value;
        ++m_fee_estimate_revision;
        Q_EMIT targetBlocksChanged();
        Q_EMIT feeEstimateRevisionChanged();
        scheduleFeeEstimates();
    }
    void setCustomFeeEnabled(const bool value)
    {
        if (m_custom_fee_enabled == value) return;
        m_custom_fee_enabled = value;
        ++m_fee_estimate_revision;
        Q_EMIT customFeeEnabledChanged();
        Q_EMIT feeEstimateRevisionChanged();
        scheduleFeeEstimates();
    }
    void setCustomFeeRate(const QString& value)
    {
        const bool was_valid = customFeeRateValid();
        if (m_custom_fee_rate == value) return;
        m_custom_fee_rate = value;
        m_custom_fee_estimate = customFeeRateValid() ? QStringLiteral("0.00000400 BTC") : QString{};
        ++m_fee_estimate_revision;
        Q_EMIT customFeeRateChanged();
        if (was_valid != customFeeRateValid()) {
            Q_EMIT customFeeRateValidChanged();
        }
        Q_EMIT feeEstimateRevisionChanged();
        scheduleFeeEstimates();
    }
    void setSendAmountExhaustsBalance(const bool value)
    {
        if (m_send_amount_exhausts_balance == value) return;
        m_send_amount_exhausts_balance = value;
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
    Q_INVOKABLE bool prepareTransaction()
    {
        ++m_prepare_transaction_calls;
        Q_EMIT prepareTransactionCallsChanged();
        if (m_prepare_transaction_result) {
            setTransactionStatus({}, false);
        } else {
            const bool selected_inputs_active{
                m_coins_list_model && m_coins_list_model->property("selectedCoinsCount").toInt() > 0};
            setTransactionStatus(selected_inputs_active
                ? QStringLiteral("Selected inputs do not cover the amount plus fee")
                : QStringLiteral("Amount plus fee exceeds available balance"), false);
        }
        return m_prepare_transaction_result;
    }
    bool securityBusy() const { return m_security_state == SecurityPending; }
    Q_INVOKABLE bool encryptWallet(const QString&) { return startSecurityForTest(); }
    Q_INVOKABLE bool changeWalletPassphrase(const QString&, const QString&) { return startSecurityForTest(); }
    bool startSecurityForTest()
    {
        if (securityBusy() || !m_available) return false;
        m_security_state = SecurityPending;
        Q_EMIT securityOperationChanged();
        return true;
    }
    Q_INVOKABLE void finishSecurityForTest(bool success)
    {
        if (!securityBusy()) return;
        m_security_state = success ? SecuritySucceeded : SecurityFailed;
        m_settings_error = success ? QString{} : QStringLiteral("Security operation failed");
        Q_EMIT settingsErrorChanged();
        Q_EMIT securityOperationChanged();
    }
    Q_INVOKABLE void requestClose()
    {
        if (!m_available) return;
        m_close_question_token = m_name + QStringLiteral(":%1").arg(++m_close_sequence);
        Q_EMIT closeQuestionChanged();
    }
    Q_INVOKABLE void answerClose(const QString& token, bool accept)
    {
        if (!m_available || token.isEmpty() || token != m_close_question_token) return;
        m_close_question_token.clear();
        Q_EMIT closeQuestionChanged();
        if (accept) Q_EMIT closeConfirmed();
    }
    Q_INVOKABLE bool prepareTransactionWithPassphrase(const QString&)
    {
        return prepareTransaction();
    }
    Q_INVOKABLE void scheduleFeeEstimates()
    {
        ++m_schedule_fee_estimates_calls;
        Q_EMIT scheduleFeeEstimatesCallsChanged();
    }
    Q_INVOKABLE void setFeeEstimate(const int target, const QString& estimate)
    {
        if (estimate.isEmpty()) {
            m_fee_estimates.remove(target);
        } else {
            m_fee_estimates.insert(target, estimate);
        }
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
    Q_INVOKABLE void clearFeeEstimates()
    {
        m_fee_estimates.clear();
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
    Q_INVOKABLE bool sendTransaction()
    {
        ++m_send_transaction_calls;
        Q_EMIT sendTransactionCallsChanged();
        if (m_send_transaction_result) {
            setTransactionStatus({}, false);
        }
        return m_send_transaction_result;
    }
    Q_INVOKABLE bool sendTransactionWithPassphrase(const QString&)
    {
        ++m_send_transaction_with_passphrase_calls;
        Q_EMIT sendTransactionCallsChanged();
        return m_send_transaction_result;
    }
    /** Stand in for a new review or PSBT import replacing the transaction. */
    Q_INVOKABLE void replaceCurrentTransactionForTest(QObject* transaction)
    {
        setCurrentTransaction(transaction);
    }
    Q_INVOKABLE bool broadcastCurrentTransaction()
    {
        ++m_broadcast_current_transaction_calls;
        Q_EMIT broadcastCurrentTransactionCallsChanged();
        return m_current_transaction_can_broadcast;
    }
    Q_INVOKABLE QString saveCurrentTransactionAsPsbt(const QString& path)
    {
        m_last_saved_psbt_path = path;
        Q_EMIT lastSavedPsbtPathChanged();
        return {};
    }
    Q_INVOKABLE void discardCurrentTransaction()
    {
        ++m_review_revision;++m_discard_current_transaction_calls;
        m_current_transaction_can_send = false;
        m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear();
        if (m_recipients) {
            QMetaObject::invokeMethod(m_recipients, "clearToFront");
        }
        Q_EMIT currentTransactionChanged();
        Q_EMIT discardCurrentTransactionCallsChanged();
    }
    Q_INVOKABLE bool commitPaymentRequest()
    {
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return false;
        m_last_commit_address_type = request->m_address_type;
        Q_EMIT lastCommitAddressTypeChanged();
        m_saved_payment_request_label = request->m_label;
        m_saved_payment_request_message = request->m_message;
        m_saved_payment_request_note_self = request->m_note_self;
        m_saved_payment_request_amount_sat = request->m_amount.satoshi();
        m_saved_payment_request_address_type = request->m_address_type;
        request->m_id = QStringLiteral("1");
        if (request->m_address.isEmpty()) request->m_address = QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        m_saved_payment_request_address = request->m_address;
        request->m_is_editing = false;
        Q_EMIT request->idChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->isEditingChanged();
        return true;
    }
    Q_INVOKABLE MockReceiveRequestOperation* updatePaymentRequest(const QString& id, qint64 amount, const QString& label, const QString& message, const QString& note)
    {
        auto* request = &m_detail_payment_request;
        if (request->m_id != id) return nullptr;
        if (request->m_payment_received && (amount != request->m_amount.satoshi() || label != request->m_label || message != request->m_message)) return nullptr;
        request->m_amount.setSatoshi(amount);
        request->setProperty("label", label);
        request->setProperty("message", message);
        request->setProperty("noteSelf", note);
        auto* operation = new MockReceiveRequestOperation(this); operation->m_saved = true; operation->m_id = id;
        return operation;
    }
    Q_INVOKABLE bool commitPaymentRequestWithPassphrase(const QString&)
    {
        return commitPaymentRequest();
    }
    Q_INVOKABLE void clearSettingsError()
    {
        if (m_settings_error.isEmpty()) return;
        m_settings_error.clear();
        Q_EMIT settingsErrorChanged();
    }
    Q_INVOKABLE bool backupWallet(const QString& path)
    {
        m_last_backup_path = path;
        ++m_backup_wallet_calls;
        clearSettingsError();
        Q_EMIT backupWalletCallsChanged();
        return true;
    }
    Q_INVOKABLE void prepareBackupConfirmationTestState(const QString& path, bool replacing)
    {
        auto* operation = new MockBackupFileOperation(this);
        operation->m_phase = MockBackupFileOperation::AwaitingConfirmation;
        operation->m_pending = operation->m_confirmation = true;
        operation->m_replacing = replacing;
        operation->m_path = path;
        if (m_backup_operation) m_backup_operation->deleteLater();
        m_backup_operation = operation;
        Q_EMIT backupWalletCallsChanged();
    }
    Q_INVOKABLE void resetWalletSettingsTestState()
    {
        m_security_state = SecurityIdle;
        m_available = true;
        Q_EMIT securityOperationChanged();
        Q_EMIT availableChanged();
        if (m_backup_operation) m_backup_operation->deleteLater();
        m_backup_operation = nullptr;
        m_last_backup_path.clear();
        m_backup_wallet_calls = 0;
        m_key_scheme = QStringLiteral("Descriptor");
        m_private_keys_status = QStringLiteral("Enabled");
        m_external_signer_status = QStringLiteral("Not used");
        m_can_manage_passphrase = true;
        clearSettingsError();
        Q_EMIT backupWalletCallsChanged();
        Q_EMIT walletInfoChanged();
    }
    Q_INVOKABLE void setExternalSignerWalletSettingsTestState()
    {
        m_key_scheme = QStringLiteral("Watch-only");
        m_private_keys_status = QStringLiteral("Disabled");
        m_external_signer_status = QStringLiteral("Enabled");
        m_can_manage_passphrase = false;
        Q_EMIT walletInfoChanged();
    }
    Q_INVOKABLE void usePaymentRequestAsTemplate(const QString& request_id)
    {
        m_receiving_address.clear();
        m_last_template_request_id = request_id;
        Q_EMIT lastTemplateRequestIdChanged();
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_id.clear();
        request->m_label = m_saved_payment_request_label;
        request->m_message = m_saved_payment_request_message;
        request->m_note_self = m_saved_payment_request_note_self;
        request->m_address.clear();
        request->m_address_type = m_saved_payment_request_address_type;
        request->m_is_editing = true;
        request->m_amount.setSatoshi(m_saved_payment_request_amount_sat);
        Q_EMIT request->idChanged();
        Q_EMIT request->labelChanged();
        Q_EMIT request->messageChanged();
        Q_EMIT request->noteSelfChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->addressTypeChanged();
        Q_EMIT request->isEditingChanged();
        Q_EMIT request->m_amount.displayChanged();
    }
    Q_INVOKABLE MockReceiveRequestOperation* removeReceiveRequest(const QString& request_id)
    {
        if (!m_remove_receive_request_result) return nullptr;
        m_last_removed_request_id = request_id;
        Q_EMIT lastRemovedRequestIdChanged();
        auto* operation = new MockReceiveRequestOperation(this); operation->m_erased = true; operation->m_id = request_id;
        return operation;
    }

Q_SIGNALS:
    void psbtActionChanged();
    void recipientLabelsChanged();
    void receivingAddressStateChanged();
    void receiveRequestStorageChanged();
    void securityOperationChanged();
    void closeQuestionChanged();
    void closeConfirmed();
    void availableChanged();
    void nameChanged();
    void balanceChanged();
    void targetBlocksChanged();
    void customFeeEnabledChanged();
    void customFeeRateChanged();
    void customFeeRateValidChanged();
    void feeEstimatePendingChanged();
    void feeEstimateRevisionChanged();
    void maximumRecipientChanged();
    void sendAmountExhaustsBalanceChanged();
    void prepareTransactionResultChanged();
    void sendTransactionResultChanged();
    void prepareTransactionCallsChanged();
    void preparationChanged();
    void scheduleFeeEstimatesCallsChanged();
    void sendTransactionCallsChanged();
    void sendOperationChanged();
    void broadcastCurrentTransactionCallsChanged();
    void discardCurrentTransactionCallsChanged();
    void currentTransactionChanged();
    void lastSavedPsbtPathChanged();
    void externalSignerApprovalSucceeded();
    void signerApprovalChanged();
    void externalSignerApprovalPartiallySucceeded();
    void externalSignerApprovalFailed(const QString& message, bool signerNotFound);
    void securityStateChanged();
    void walletInfoChanged();
    void settingsErrorChanged();
    void backupWalletCallsChanged();
    void transactionErrorChanged();
    void transactionNeedsUnlockChanged();
    void displayUnitChanged();
    void lastCommitAddressTypeChanged();
    void lastLoadedPaymentRequestIdChanged();
    void lastLoadedPaymentRequestDetailIdChanged();
    void lastTemplateRequestIdChanged();
    void lastRemovedRequestIdChanged();
    void removeReceiveRequestResultChanged();
    void receiveRequestReconciliationPendingChanged();

private:
    MockPsbtDocument m_imported_psbt{this};
    PsbtActionState m_psbt_action_state{PsbtActionState::PsbtIdle};
    PsbtImportResult m_psbt_import_result{PsbtImportResult::PsbtUnsupported},m_next_psbt_import_result{PsbtImportResult::WalletCanSign};
    quint64 m_psbt_action_id{0},m_review_revision{0};bool m_hold_psbt_publication{false};QString m_psbt_action_error;
    std::shared_ptr<PsbtPacket> m_psbt_operation,m_saved_psbt_receipt,m_submitted_psbt_receipt;
    MockFlowDetails m_flow_details{this};
    quint64 m_flow_revision{0};
    QMetaObject::Connection m_review_rows_connection;

    QString m_recipient_labels_error;
    void setTransactionStatus(const QString& error, bool needs_unlock)
    {
        if (m_transaction_error != error) {
            m_transaction_error = error;
            Q_EMIT transactionErrorChanged();
        }
        if (m_transaction_needs_unlock != needs_unlock) {
            m_transaction_needs_unlock = needs_unlock;
            Q_EMIT transactionNeedsUnlockChanged();
        }
    }

    QHash<int, QString> m_fee_estimates{{1, QStringLiteral("0.00000750 BTC")}, {2, QStringLiteral("0.00000500 BTC")}, {6, QStringLiteral("0.00000250 BTC")}};
    bool m_custom_fee_enabled{false};
    QString m_custom_fee_rate;
    QString m_custom_fee_estimate;
    bool m_fee_estimate_pending{false};
    bool m_send_amount_exhausts_balance{false};
    bool m_send_transaction_result{true};
    bool m_is_encrypted{false};
    bool m_is_locked{false};
    QString m_key_scheme{QStringLiteral("Descriptor")};
    int m_key_scheme_kind{0};
    QString m_private_keys_status{QStringLiteral("Enabled")};
    QString m_external_signer_status{QStringLiteral("Not used")};
    bool m_can_manage_passphrase{true};
    QString m_settings_error;
    MockBackupFileOperation* m_backup_operation{nullptr};
    QString m_last_backup_path;
    int m_backup_wallet_calls{0};
    QString m_transaction_error;
    bool m_transaction_needs_unlock{false};
    int m_fee_estimate_revision{1};
    int m_prepare_transaction_calls{0};
    int m_schedule_fee_estimates_calls{0};
    int m_send_transaction_calls{0};
    int m_send_transaction_with_passphrase_calls{0};
    int m_broadcast_current_transaction_calls{0};
    int m_discard_current_transaction_calls{0};
    MockAddressListModel m_address_list_model;
};

class MockWalletQmlModelTransaction : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address MEMBER m_address CONSTANT)
    Q_PROPERTY(QString label MEMBER m_label CONSTANT)
    Q_PROPERTY(QString amount MEMBER m_amount CONSTANT)
    Q_PROPERTY(QString fee MEMBER m_fee CONSTANT)
    Q_PROPERTY(QString total MEMBER m_total CONSTANT)
    Q_PROPERTY(QString txid MEMBER m_txid CONSTANT)
    Q_PROPERTY(QVariantList reviewedRecipients MEMBER m_reviewed_recipients NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(int reviewTargetBlocks MEMBER m_review_target_blocks NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(QString reviewFeeRate MEMBER m_review_fee_rate NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(QObject* amountAmount READ amountAmount CONSTANT)
    Q_PROPERTY(QObject* feeAmount READ feeAmount CONSTANT)
    Q_PROPERTY(QObject* totalAmount READ totalAmount CONSTANT)

public:
    MockWalletQmlModelTransaction()
    {
        m_amount_amount.setDisplay(QStringLiteral("0.01000000"));
        m_fee_amount.setDisplay(QStringLiteral("0.00001000"));
        m_total_amount.setDisplay(QStringLiteral("0.01001000"));
    }

    QString m_address{QStringLiteral("bcrt1qexampleaddress")};
    QString m_label{QStringLiteral("example-label")};
    QString m_amount{QStringLiteral("0.01000000 BTC")};
    QString m_fee{QStringLiteral("0.00001000 BTC")};
    QString m_total{QStringLiteral("0.01001000 BTC")};
    QString m_txid{QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")};
    QVariantList m_reviewed_recipients;
    int m_review_target_blocks{6};
    QString m_review_fee_rate{QStringLiteral("2.5")};
    MockBitcoinAmount m_amount_amount;
    MockBitcoinAmount m_fee_amount;
    MockBitcoinAmount m_total_amount;

    QObject* amountAmount() { return &m_amount_amount; }
    QObject* feeAmount() { return &m_fee_amount; }
    QObject* totalAmount() { return &m_total_amount; }

Q_SIGNALS:
    void reviewedRecipientsChanged();
};

class MockWalletProvisioningCheck : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY inputChanged)
    Q_PROPERTY(QString xpub READ xpub WRITE setXpub NOTIFY inputChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY inputChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString nameError READ nameError NOTIFY changed)
    Q_PROPERTY(bool xpubValid READ xpubValid NOTIFY changed)
    Q_PROPERTY(bool pathExists READ pathExists NOTIFY changed)
    Q_PROPERTY(QString normalizedPath READ path NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit MockWalletProvisioningCheck(QObject* parent = nullptr) : QObject(parent) { refresh(); }
    QString name() const { return m_name; }
    QString xpub() const { return m_xpub; }
    QString path() const { return m_path; }
    bool pending() const { return m_pending; }
    bool ready() const { return !m_pending && !m_closed; }
    QString nameError() const { return m_name.trimmed().isEmpty() ? QStringLiteral("Enter a wallet name.") : QString{}; }
    bool xpubValid() const { const auto key = m_xpub.trimmed(); return ready() && key.size() >= 100 && (key.startsWith("xpub") || key.startsWith("tpub")); }
    bool pathExists() const { return ready() && !m_path.isEmpty(); }
    QString error() const { return {}; }
    void setName(const QString& value) { if (m_name == value) return; m_name = value; refresh(); Q_EMIT inputChanged(); }
    void setXpub(const QString& value) { if (m_xpub == value) return; m_xpub = value; refresh(); Q_EMIT inputChanged(); }
    void setPath(const QString& value) { if (m_path == value) return; m_path = value; refresh(); Q_EMIT inputChanged(); }
    Q_INVOKABLE void closeAdmission() { m_closed = true; ++m_revision; }
    Q_INVOKABLE void refresh() {
        if (m_closed) return;
        const auto revision = ++m_revision; m_pending = true; Q_EMIT changed();
        QTimer::singleShot(0, this, [this, revision] { if (!m_closed && revision == m_revision) { m_pending = false; Q_EMIT changed(); } });
    }
Q_SIGNALS:
    void inputChanged();
    void changed();
private:
    QString m_name, m_xpub, m_path;
    quint64 m_revision{0};
    bool m_pending{false}, m_closed{false};
};

class MockOpenWalletListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex& index, int role) const override {
        return index.isValid() && index.row() >= 0 && index.row() < m_rows.size() && role == Qt::UserRole + 1 ? QVariant::fromValue(m_rows[index.row()]) : QVariant{};
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "wallet"}}; }
    int rowForWallet(QObject* wallet) const { return m_rows.indexOf(wallet); }
    void setWallets(const QList<QObject*>& wallets) {
        for (const auto& connection : m_connections) QObject::disconnect(connection);
        m_connections.clear(); m_wallets = wallets;
        for (auto* wallet : wallets) if (wallet) {
            m_connections.push_back(connect(wallet, SIGNAL(availableChanged()), this, SLOT(refresh())));
            m_connections.push_back(connect(wallet, &QObject::destroyed, this, [this, wallet] { m_wallets.removeAll(wallet); refresh(); }));
        }
        refresh();
    }
public Q_SLOTS:
    void refresh() {
        QList<QObject*> visible;
        for (auto* wallet : m_wallets) if (wallet && wallet->property("available").toBool()) visible.push_back(wallet);
        for (int row = m_rows.size() - 1; row >= 0; --row) if (!visible.contains(m_rows[row])) { beginRemoveRows({}, row, row); m_rows.removeAt(row); endRemoveRows(); }
        for (auto* wallet : visible) if (!m_rows.contains(wallet)) { const int row = m_rows.size(); beginInsertRows({}, row, row); m_rows.push_back(wallet); endInsertRows(); }
    }
private:
    QList<QObject*> m_wallets, m_rows;
    QList<QMetaObject::Connection> m_connections;
};

class MockWalletErrorLines : public QAbstractListModel
{
public:
    explicit MockWalletErrorLines(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int((m_text.size() + 1023) / 1024); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "content"}}; }
    QVariant data(const QModelIndex& index, int role) const override { return index.isValid() && role == Qt::UserRole ? m_text.mid(qsizetype(index.row()) * 1024, 1024) : QVariant{}; }
    void setText(QString text) { beginResetModel(); m_text = std::move(text); endResetModel(); }
private:
    QString m_text;
};

class MockWalletController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool initialized MEMBER m_initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded MEMBER m_is_wallet_loaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound MEMBER m_no_wallets_found NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(bool walletLoadInProgress MEMBER m_wallet_load_in_progress NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString walletLoadError MEMBER m_wallet_load_error NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(bool walletLoadHasError READ walletLoadHasError NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadErrorDisplay READ walletLoadErrorDisplay NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadWarnings MEMBER m_wallet_load_warnings NOTIFY walletLoadWarningsChanged)
    Q_PROPERTY(QString walletImportErrorTitle READ walletImportErrorTitle NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletImportErrorDescription READ walletImportErrorDescription NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletImportErrorHelpText READ walletImportErrorHelpText NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QAbstractItemModel* walletImportErrorHelpModel READ walletImportErrorHelpModel CONSTANT)
    Q_PROPERTY(bool walletImportErrorPending READ walletImportErrorPending CONSTANT)
    Q_PROPERTY(bool walletImportErrorUnavailable READ walletImportErrorUnavailable CONSTANT)
    Q_PROPERTY(QString walletImportErrorProjectionError READ walletImportErrorProjectionError CONSTANT)
    Q_PROPERTY(bool walletImportErrorRetryAvailable READ walletImportErrorRetryAvailable CONSTANT)
    Q_PROPERTY(QString walletCreateError MEMBER m_wallet_create_error NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(bool walletCreateHasError READ walletCreateHasError NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(QString walletCreateErrorDisplay READ walletCreateErrorDisplay NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(QAbstractItemModel* walletCreateErrorHelpModel READ walletCreateErrorHelpModel CONSTANT)
    Q_PROPERTY(bool walletMigrationInProgress MEMBER m_wallet_migration_in_progress NOTIFY walletMigrationInProgressChanged)
    Q_PROPERTY(QString walletMigrationError MEMBER m_wallet_migration_error NOTIFY walletMigrationErrorChanged)
    Q_PROPERTY(QString lastImportedWalletName MEMBER m_last_imported_wallet_name NOTIFY lastImportedWalletInfoChanged)
    Q_PROPERTY(QString lastImportedWalletKeyScheme MEMBER m_last_imported_wallet_key_scheme NOTIFY lastImportedWalletInfoChanged)
    Q_PROPERTY(bool canCreateExternalSignerWallet MEMBER m_can_create_external_signer_wallet NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString externalSignerName MEMBER m_external_signer_name NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString externalSignerError MEMBER m_external_signer_error NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString suggestedExternalSignerWalletName MEMBER m_suggested_external_signer_wallet_name NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString lastSelectedWalletName READ lastSelectedWalletName NOTIFY lastSelectedWalletNameChanged)
    Q_PROPERTY(QString lastClosedWalletName READ lastClosedWalletName NOTIFY lastClosedWalletNameChanged)
    Q_PROPERTY(int closeWalletCalls READ closeWalletCalls NOTIFY closeWalletCallsChanged)
    Q_PROPERTY(QObject* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(QObject* closeTarget READ closeTarget NOTIFY closeTargetChanged)
    Q_PROPERTY(QAbstractItemModel* openWalletModel READ openWalletModel CONSTANT)
    Q_PROPERTY(QList<QObject*> openWallets READ openWallets NOTIFY openWalletsChanged)
    Q_PROPERTY(QObject* openedWallet MEMBER m_opened_wallet NOTIFY openWalletsChanged)
    Q_PROPERTY(QObject* lastLocationWallet MEMBER m_last_location_wallet NOTIFY openWalletLocationCallsChanged)
    Q_PROPERTY(int closePaymentRequestDetailRequests MEMBER m_close_payment_request_detail_requests NOTIFY closePaymentRequestDetailRequestsChanged)
    Q_PROPERTY(int openReceiveRequests MEMBER m_open_receive_requests NOTIFY openReceiveRequestsChanged)
    Q_PROPERTY(QString walletLocationOpenError READ walletLocationOpenError NOTIFY walletLocationOpenErrorChanged)
    Q_PROPERTY(int openWalletLocationCalls READ openWalletLocationCalls NOTIFY openWalletLocationCallsChanged)

public:
    explicit MockWalletController(QObject* parent = nullptr) : QObject(parent) {
        connect(this, &MockWalletController::walletLoadErrorChanged, this, [this] { m_error_lines.setText(m_wallet_load_error); });
        connect(this, &MockWalletController::walletCreateErrorChanged, this, [this] { m_create_error_lines.setText(m_wallet_create_error); });
        connect(this, &MockWalletController::walletLoadStateChanged, this, [this](const QString& name, int state, const QString& error) { if (state == 3) Q_EMIT walletLoadErrorNotice(name, ++m_notice_operation, error); });
        connect(this, &MockWalletController::walletMigrationRequired, this, [this](const QString& name) { m_migration_notice_path = name; });
        connect(this, &MockWalletController::walletMigrationPassphraseRequired, this, [this](const QString& name) { m_migration_notice_path = name; });
        connect(this, &MockWalletController::walletMigrationFailed, this, [this] { Q_EMIT walletMigrationErrorNotice(m_migration_notice_path, ++m_notice_operation, m_wallet_migration_error.contains(QLatin1String("passphrase"), Qt::CaseInsensitive), m_wallet_migration_error.left(1024)); });
    }
    bool walletLoadHasError() const { return !m_wallet_load_error.isEmpty(); }
    bool walletCreateHasError() const { return !m_wallet_create_error.isEmpty(); }
    QString walletLoadErrorDisplay() const { return m_wallet_load_error.left(1024); }
    QString walletCreateErrorDisplay() const { return m_wallet_create_error.left(1024); }
    QAbstractItemModel* walletCreateErrorHelpModel() { return &m_create_error_lines; }
    QAbstractItemModel* walletImportErrorHelpModel() { return &m_error_lines; }
    bool walletImportErrorPending() const { return false; }
    bool walletImportErrorUnavailable() const { return false; }
    QString walletImportErrorProjectionError() const { return {}; }
    bool walletImportErrorRetryAvailable() const { return false; }
    Q_INVOKABLE bool retryWalletImportErrorPresentation() { return false; }
    Q_INVOKABLE QAbstractItemModel* captureMigrationErrorDetails(quint64, int slot) { auto* lines = slot == 0 ? &m_migration_required_details : &m_migration_passphrase_details; lines->setText(m_wallet_migration_error); return lines; }
    Q_INVOKABLE MockWalletProvisioningCheck* createProvisioningCheck() { return new MockWalletProvisioningCheck(this); }
    bool m_initialized{true};
    bool m_is_wallet_loaded{true};
    bool m_no_wallets_found{false};
    bool m_wallet_load_in_progress{false};
    QString m_wallet_load_error;
    QString m_wallet_load_warnings;
    QString m_wallet_create_error;
    bool m_wallet_migration_in_progress{false};
    QString m_wallet_migration_error;
    QString m_last_imported_wallet_name;
    QString m_last_imported_wallet_key_scheme;
    bool m_can_create_external_signer_wallet{false};
    QString m_external_signer_name;
    QString m_external_signer_error;
    QString m_suggested_external_signer_wallet_name{QStringLiteral("external_signer")};
    QObject* m_selected_wallet{nullptr};
    QList<QObject*> m_open_wallets;
    // The wallet reported by create, import and load success; the selected
    // wallet unless a test sets another.
    QObject* m_opened_wallet{nullptr};
    QObject* m_last_location_wallet{nullptr};
    QObject* reportedWallet() const { return m_opened_wallet ? m_opened_wallet : m_selected_wallet; }
    QString m_last_selected_wallet_name;
    QString m_last_closed_wallet_name;
    int m_close_wallet_calls{0};
    int m_close_payment_request_detail_requests{0};
    int m_open_receive_requests{0};
    QString m_wallet_location_open_error;
    bool m_open_wallet_location_result{true};
    QString m_open_wallet_location_error;
    int m_open_wallet_location_calls{0};

    QObject* selectedWallet() const { return m_selected_wallet; }
    QList<QObject*> openWallets() const { return m_open_wallets; }
    QAbstractItemModel* openWalletModel() { return &m_open_model; }
    Q_INVOKABLE int rowForWallet(QObject* wallet) const { return m_open_model.rowForWallet(wallet); }
    QString lastSelectedWalletName() const { return m_last_selected_wallet_name; }
    QString lastClosedWalletName() const { return m_last_closed_wallet_name; }
    int closeWalletCalls() const { return m_close_wallet_calls; }
    QString walletLocationOpenError() const { return m_wallet_location_open_error; }
    int openWalletLocationCalls() const { return m_open_wallet_location_calls; }
    QString walletImportErrorTitle() const { return m_wallet_load_error.isEmpty() ? QString{} : QStringLiteral("Failed to load wallet"); }
    QString walletImportErrorDescription() const { return m_wallet_load_error; }
    QString walletImportErrorHelpText() const { return QString{}; }
    Q_INVOKABLE QString homePath() const { return QStringLiteral("/tmp"); }
    Q_PROPERTY(int externalSignerRefreshCalls MEMBER m_external_signer_refresh_calls NOTIFY externalSignerStatusChanged)
    int m_external_signer_refresh_calls{0};
    Q_INVOKABLE void refreshExternalSignerStatus() { ++m_external_signer_refresh_calls; Q_EMIT externalSignerStatusChanged(); }
    Q_INVOKABLE void clearWalletLoadStatus()
    {
        m_wallet_load_error.clear();
        m_wallet_load_warnings.clear();
        m_wallet_load_in_progress = false;
        Q_EMIT walletLoadErrorChanged();
        Q_EMIT walletLoadWarningsChanged();
        Q_EMIT walletLoadInProgressChanged();
    }
    Q_INVOKABLE void clearWalletCreateStatus()
    {
        m_wallet_create_error.clear();
        Q_EMIT walletCreateErrorChanged();
    }
    Q_INVOKABLE void clearWalletMigrationStatus()
    {
        m_wallet_migration_in_progress = false;
        m_wallet_migration_error.clear();
        Q_EMIT walletMigrationInProgressChanged();
        Q_EMIT walletMigrationErrorChanged();
    }
    Q_INVOKABLE void createWatchOnlyWallet(const QString& /*name*/, const QString& /*xpub*/)
    {
        clearWalletLoadStatus();
        Q_EMIT walletCreateSucceeded(reportedWallet());
    }
    Q_INVOKABLE void createSingleSigWallet(const QString&, const QString&)
    {
        clearWalletCreateStatus();
        clearWalletLoadStatus();
        Q_EMIT walletCreateSucceeded(reportedWallet());
    }
    Q_INVOKABLE bool createExternalSignerWallet(const QString&)
    {
        clearWalletLoadStatus();
        if (!m_can_create_external_signer_wallet) {
            m_wallet_load_error = QStringLiteral("Connect an external signer and try again.");
            Q_EMIT walletLoadErrorChanged();
            return false;
        }
        Q_EMIT walletLoadSucceeded(reportedWallet());
        return true;
    }
    Q_INVOKABLE void importWallet(const QString&)
    {
        clearWalletLoadStatus();
        Q_EMIT walletImportSucceeded(reportedWallet());
    }
    Q_INVOKABLE void migrateWallet(const QString&, const QString& = QString())
    {
        clearWalletMigrationStatus();
        Q_EMIT walletMigrationSucceeded();
    }
    Q_INVOKABLE void requestOpenWalletSettings(QObject* wallet)
    {
        if (wallet) Q_EMIT openWalletSettingsRequested(wallet);
    }
    Q_INVOKABLE void setOpenWallets(const QList<QObject*>& wallets)
    {
        if (m_open_wallets == wallets) return;
        m_open_wallets = wallets; m_open_model.setWallets(wallets);
        Q_EMIT openWalletsChanged();
    }
    Q_INVOKABLE bool isWalletOpen(const QString& name) const
    {
        return std::any_of(m_open_wallets.begin(), m_open_wallets.end(), [&](const QObject* wallet) {
            return wallet && wallet->property("name").toString() == name;
        });
    }
    Q_INVOKABLE void setSelectedWalletObject(QObject* wallet)
    {
        if (m_selected_wallet == wallet) return;
        m_selected_wallet = wallet;
        Q_EMIT selectedWalletChanged();
    }
    Q_INVOKABLE void setSelectedWallet(const QString& name, const QString& format = QString())
    {
        Q_UNUSED(format);
        if (m_last_selected_wallet_name == name) return;
        m_last_selected_wallet_name = name;
        Q_EMIT lastSelectedWalletNameChanged();
        Q_EMIT selectedWalletChanged();
    }
    QObject* closeTarget() const { return m_close_target; }
    Q_INVOKABLE QObject* walletForName(const QString& name) const
    {
        for (auto* wallet : m_open_wallets) if (wallet && wallet->property("name").toString() == name) return wallet;
        return nullptr;
    }
    Q_INVOKABLE bool selectWallet(QObject* wallet)
    {
        if (!wallet || !wallet->property("available").toBool() || !m_open_wallets.contains(wallet)) return false;
        setSelectedWalletObject(wallet);
        setSelectedWallet(wallet->property("name").toString());
        return true;
    }
    Q_INVOKABLE void requestCloseWallet(QObject* wallet)
    {
        auto* model = qobject_cast<MockWalletQmlModel*>(wallet);
        if (!model || !wallet->property("available").toBool() || !m_open_wallets.contains(wallet)) return;
        QObject::disconnect(m_close_connection);
        if (m_close_target && m_close_target != wallet) m_close_target->setProperty("closeQuestionToken", QString{});
        m_close_target = wallet;
        m_close_connection = connect(model, &MockWalletQmlModel::closeConfirmed, this, [this, model] { closeWallet(model->property("name").toString()); });
        Q_EMIT closeTargetChanged();
        model->requestClose();
    }
    Q_INVOKABLE void closeWallet(const QString& name)
    {
        m_last_closed_wallet_name = name;
        ++m_close_wallet_calls;
        Q_EMIT lastClosedWalletNameChanged();
        Q_EMIT closeWalletCallsChanged();
    }
    Q_INVOKABLE void setInitialized(bool initialized)
    {
        if (m_initialized == initialized) return;
        m_initialized = initialized;
        Q_EMIT initializedChanged();
    }
    Q_INVOKABLE void setWalletLoaded(bool loaded)
    {
        if (m_is_wallet_loaded == loaded) return;
        m_is_wallet_loaded = loaded;
        Q_EMIT isWalletLoadedChanged();
    }
    Q_INVOKABLE void setNoWalletsFound(bool no_wallets_found)
    {
        if (m_no_wallets_found == no_wallets_found) return;
        m_no_wallets_found = no_wallets_found;
        Q_EMIT noWalletsFoundChanged();
    }
    Q_INVOKABLE void reset()
    {
        QObject::disconnect(m_close_connection);
        if (m_close_target) m_close_target->setProperty("closeQuestionToken", QString{});
        m_close_target = nullptr;
        Q_EMIT closeTargetChanged();
        setOpenWallets({});
        m_opened_wallet = nullptr;
        m_last_location_wallet = nullptr;
        m_last_selected_wallet_name.clear();
        m_last_closed_wallet_name.clear();
        m_close_wallet_calls = 0;
        m_close_payment_request_detail_requests = 0;
        m_open_receive_requests = 0;
        m_open_wallet_location_calls = 0;
        m_open_wallet_location_result = true;
        m_open_wallet_location_error.clear();
        clearWalletLocationOpenError();
        clearWalletLoadStatus();
        clearWalletCreateStatus();
        clearWalletMigrationStatus();
        m_last_imported_wallet_name.clear();
        m_last_imported_wallet_key_scheme.clear();
        m_external_signer_refresh_calls = 0;
        m_can_create_external_signer_wallet = false;
        m_external_signer_name.clear();
        m_external_signer_error.clear();
        m_suggested_external_signer_wallet_name = QStringLiteral("external_signer");
        Q_EMIT lastSelectedWalletNameChanged();
        Q_EMIT lastClosedWalletNameChanged();
        Q_EMIT closeWalletCallsChanged();
        Q_EMIT closePaymentRequestDetailRequestsChanged();
        Q_EMIT openReceiveRequestsChanged();
        Q_EMIT openWalletLocationCallsChanged();
        Q_EMIT lastImportedWalletInfoChanged();
        Q_EMIT externalSignerStatusChanged();
    }
    Q_INVOKABLE void requestClosePaymentRequestDetail(QObject* wallet)
    {
        ++m_close_payment_request_detail_requests;
        Q_EMIT closePaymentRequestDetailRequestsChanged();
        if (wallet) Q_EMIT closePaymentRequestDetailRequested(wallet);
    }
    Q_INVOKABLE void requestOpenReceive(QObject* wallet)
    {
        ++m_open_receive_requests;
        Q_EMIT openReceiveRequestsChanged();
        if (wallet) Q_EMIT openReceiveRequested(wallet);
    }
    Q_INVOKABLE void setOpenWalletLocationResult(const bool result, const QString& error = QString())
    {
        m_open_wallet_location_result = result;
        m_open_wallet_location_error = error;
    }
    Q_INVOKABLE bool openWalletLocation(QObject* wallet)
    {
        m_last_location_wallet = wallet;
        ++m_open_wallet_location_calls;
        Q_EMIT openWalletLocationCallsChanged();
        if (m_open_wallet_location_result) {
            clearWalletLocationOpenError();
            return true;
        }
        m_wallet_location_open_error = m_open_wallet_location_error.isEmpty()
            ? QStringLiteral("Could not open wallet file location.")
            : m_open_wallet_location_error;
        Q_EMIT walletLocationOpenErrorChanged();
        return false;
    }
    Q_INVOKABLE void clearWalletLocationOpenError()
    {
        if (m_wallet_location_open_error.isEmpty()) return;
        m_wallet_location_open_error.clear();
        Q_EMIT walletLocationOpenErrorChanged();
    }

Q_SIGNALS:
    void closeTargetChanged();
    void initializedChanged();
    void isWalletLoadedChanged();
    void noWalletsFoundChanged();
    void walletLoadInProgressChanged();
    void walletLoadErrorChanged();
    void walletLoadWarningsChanged();
    void walletCreateErrorChanged();
    void walletMigrationInProgressChanged();
    void walletMigrationErrorChanged();
    void lastImportedWalletInfoChanged();
    void externalSignerStatusChanged();
    void lastSelectedWalletNameChanged();
    void lastClosedWalletNameChanged();
    void closeWalletCallsChanged();
    void selectedWalletChanged();
    void openWalletsChanged();
    void closePaymentRequestDetailRequestsChanged();
    void closePaymentRequestDetailRequested(QObject* wallet);
    void openReceiveRequestsChanged();
    void openReceiveRequested(QObject* wallet);
    void openWalletSettingsRequested(QObject* wallet);
    void walletCreateSucceeded(QObject* wallet);
    void walletLoadSucceeded(QObject* wallet);
    void walletImportSucceeded(QObject* wallet);
    void walletMigrationSucceeded();
    void walletLoadStateChanged(const QString& name, int state, const QString& error);
    void walletLoadErrorNotice(const QString& name, quint64 operation, const QString& description);
    void walletMigrationRequired(const QString& wallet_name);
    void walletMigrationPassphraseRequired(const QString& wallet_name);
    void walletMigrationFailed();
    void walletMigrationErrorNotice(const QString& name, quint64 operation, bool needsPassphrase, const QString& description);
    void walletLocationOpenErrorChanged();
    void openWalletLocationCallsChanged();
private:
    MockWalletErrorLines m_error_lines{this}, m_create_error_lines{this};
    MockWalletErrorLines m_migration_required_details{this}, m_migration_passphrase_details{this};
    QString m_migration_notice_path;
    quint64 m_notice_operation{0};
    MockOpenWalletListModel m_open_model;
    QObject* m_close_target{nullptr};
    QMetaObject::Connection m_close_connection;
};

class MockCoreSettingsModel;
class MockCoreProxyDraft;

class MockCoreSettingEntryModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY addressChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY statusChanged)
    Q_PROPERTY(QString infoText READ infoText NOTIFY statusChanged)

public:
    MockCoreSettingEntryModel(QString key, MockCoreSettingsModel& model, QObject* parent = nullptr);

    QString key() const { return m_key; }
    QVariant value() const;
    void setValue(const QVariant& value);
    bool enabled() const;
    void setEnabled(bool enabled);
    QString address() const;
    void setAddress(const QString& address);
    QVariantMap status() const;
    bool canEdit() const;
    QString infoText() const;

    Q_INVOKABLE QString validate(const QString& value) const;
    Q_INVOKABLE bool commitAddress(const QString& address);
    Q_INVOKABLE QString defaultAddress() const;

Q_SIGNALS:
    void valueChanged();
    void enabledChanged();
    void addressChanged();
    void statusChanged();

private:
    QString m_key;
    MockCoreSettingsModel& m_model;
};

class MockCoreSettingsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY statusesChanged)

public:
    enum OperationState {Idle,Pending,Applied,Rejected,Stale,Failed,Closed};
    Q_ENUM(OperationState)
    explicit MockCoreSettingsModel(QObject* parent = nullptr) : QObject{parent} {}
    Q_INVOKABLE MockCoreProxyDraft* createProxyDraft();

    Q_INVOKABLE QObject* entry(const QString& key)
    {
        if (MockCoreSettingEntryModel* existing = m_entries.value(key)) return existing;
        auto* entry = new MockCoreSettingEntryModel{key, *this, this};
        m_entries.insert(key, entry);
        return entry;
    }

    bool listen() const { return m_listen; }
    bool natpmp() const { return m_natpmp; }
    bool server() const { return m_server; }
    bool prune() const { return m_prune; }
    int pruneSizeGB() const { return m_prune_size_gb; }
    bool proxyEnabled() const { return m_proxy_enabled; }
    bool torEnabled() const { return m_tor_enabled; }
    QString proxyAddress() const { return m_proxy_address; }
    QString torAddress() const { return m_tor_address; }
    QVariantMap statuses() const { return m_statuses; }
    QVariantMap status(const QString& key) const { return m_statuses.value(key).toMap(); }
    bool canEdit(const QString& key) const { return status(key).value(QStringLiteral("canEdit"), true).toBool(); }
    QString defaultAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    QString validateAddress(const QString& value) const { return value.length() > 0 ? QString{} : QStringLiteral("Proxy location is required."); }

    void setStatuses(const QVariantMap& statuses)
    {
        m_statuses = statuses;
        Q_EMIT statusesChanged();
        for (MockCoreSettingEntryModel* entry : std::as_const(m_entries)) {
            Q_EMIT entry->statusChanged();
        }
    }

    void setListen(bool value) { setBool(QStringLiteral("listen"), m_listen, value); }
    void setNatpmp(bool value) { setBool(QStringLiteral("natpmp"), m_natpmp, value); }
    void setServer(bool value) { setBool(QStringLiteral("server"), m_server, value); }
    void setPrune(bool value) { setBool(QStringLiteral("prune"), m_prune, value); }
    void setPruneSizeGB(int value)
    {
        if (!canEdit(QStringLiteral("prune")) || value == m_prune_size_gb || value < 1) return;
        m_prune_size_gb = value;
        Q_EMIT static_cast<MockCoreSettingEntryModel*>(entry(QStringLiteral("prune")))->valueChanged();
        Q_EMIT changed();
    }
    void setProxyEnabled(bool value) { setBool(QStringLiteral("proxy"), m_proxy_enabled, value); }
    void setTorEnabled(bool value) { setBool(QStringLiteral("onion"), m_tor_enabled, value); }
    bool setProxyAddress(const QString& value) { return setAddress(QStringLiteral("proxy"), m_proxy_address, value); }
    bool setTorAddress(const QString& value) { return setAddress(QStringLiteral("onion"), m_tor_address, value); }

Q_SIGNALS:
    void changed();
    void statusesChanged();

private:
    friend class MockCoreSettingEntryModel;

    void setBool(const QString& key, bool& field, bool value)
    {
        if (!canEdit(key) || value == field) return;
        field = value;
        auto* entry_model = static_cast<MockCoreSettingEntryModel*>(entry(key));
        Q_EMIT entry_model->valueChanged();
        Q_EMIT entry_model->enabledChanged();
        Q_EMIT changed();
    }

    bool setAddress(const QString& key, QString& field, const QString& value)
    {
        if (!canEdit(key) || !validateAddress(value).isEmpty()) return false;
        if (value == field) return true;
        field = value;
        auto* entry_model = static_cast<MockCoreSettingEntryModel*>(entry(key));
        Q_EMIT entry_model->addressChanged();
        Q_EMIT entry_model->valueChanged();
        Q_EMIT changed();
        return true;
    }

    bool m_listen{true};
    bool m_natpmp{false};
    bool m_server{false};
    bool m_prune{true};
    int m_prune_size_gb{2};
    bool m_proxy_enabled{false};
    bool m_tor_enabled{false};
    QString m_proxy_address{QStringLiteral("127.0.0.1:9050")};
    QString m_tor_address{QStringLiteral("127.0.0.1:9050")};
    QVariantMap m_statuses;
    QHash<QString, MockCoreSettingEntryModel*> m_entries;
};

class MockCoreProxyDraft : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY changed)
    Q_PROPERTY(QString proxyAddress READ proxyAddress WRITE setProxyAddress NOTIFY changed)
    Q_PROPERTY(bool torEnabled READ torEnabled WRITE setTorEnabled NOTIFY changed)
    Q_PROPERTY(QString torAddress READ torAddress WRITE setTorAddress NOTIFY changed)
    Q_PROPERTY(QString proxyError READ proxyError NOTIFY changed)
    Q_PROPERTY(QString torError READ torError NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(quint64 operationId READ operationId NOTIFY changed)
    Q_PROPERTY(int operationState READ operationState NOTIFY changed)
public:
    explicit MockCoreProxyDraft(MockCoreSettingsModel& source):m_source(&source){connect(&source,&MockCoreSettingsModel::changed,this,&MockCoreProxyDraft::evaluate);connect(&source,&MockCoreSettingsModel::statusesChanged,this,&MockCoreProxyDraft::evaluate);reset();}
    bool proxyEnabled() const{return m_proxy;}QString proxyAddress() const{return m_address;}
    bool torEnabled() const{return m_tor;}QString torAddress() const{return m_onion;}
    QString proxyError() const{return m_pending?QString{}:m_proxy_error;}QString torError() const{return m_pending?QString{}:m_tor_error;}
    bool pending() const{return m_pending;}bool ready() const{return m_ready;}bool dirty() const{return m_dirty;}
    bool canApply() const{return m_ready&&!m_pending&&m_valid&&m_state!=MockCoreSettingsModel::Pending;}
    QString error() const{return m_error;}quint64 operationId() const{return m_operation;}
    int operationState() const{return m_state;}
    void setProxyEnabled(bool value){if(value==m_proxy)return;m_proxy=value;evaluate();}
    void setProxyAddress(const QString& value){if(value==m_address)return;m_address=value;evaluate();}
    void setTorEnabled(bool value){if(value==m_tor)return;m_tor=value;evaluate();}
    void setTorAddress(const QString& value){if(value==m_onion)return;m_onion=value;evaluate();}
    Q_INVOKABLE void reset(){
        if(!m_source)return;const auto revision=++m_revision;m_pending=true;m_ready=false;Q_EMIT changed();
        QTimer::singleShot(0,this,[this,revision]{if(!m_source||revision!=m_revision)return;
            m_ready=true;m_proxy=m_source->proxyEnabled();m_address=m_source->proxyAddress().isEmpty()?m_source->defaultAddress():m_source->proxyAddress();
            m_tor=m_source->torEnabled();m_onion=m_source->torAddress().isEmpty()?m_source->defaultAddress():m_source->torAddress();evaluate();});
    }
    Q_INVOKABLE quint64 requestApply(){
        if(!m_source||!canApply())return 0;const auto ticket=++m_operation;m_state=MockCoreSettingsModel::Pending;Q_EMIT changed();
        const auto source=m_source;const QPointer<MockCoreProxyDraft> observer{this};const bool proxy=m_proxy,tor=m_tor;const auto address=m_address,onion=m_onion;
        QTimer::singleShot(0,source,[source,observer,proxy,tor,address,onion]{if(!source)return;
            const bool valid=(!proxy||source->validateAddress(address).isEmpty())&&(!tor||source->validateAddress(onion).isEmpty());
            if(valid){if(source->canEdit("proxy")){if(source->validateAddress(address).isEmpty())source->setProxyAddress(address);source->setProxyEnabled(proxy);}
                if(source->canEdit("onion")){if(source->validateAddress(onion).isEmpty())source->setTorAddress(onion);source->setTorEnabled(tor);}}
            if(observer){observer->m_state=valid?MockCoreSettingsModel::Applied:MockCoreSettingsModel::Failed;observer->m_error=valid?QString{}:QStringLiteral("Invalid proxy location");Q_EMIT observer->changed();}
        });return ticket;
    }
Q_SIGNALS:void changed();
private:
    void evaluate(){
        if(!m_source)return;const auto revision=++m_revision;m_pending=true;Q_EMIT changed();
        QTimer::singleShot(0,this,[this,revision]{if(!m_source||revision!=m_revision)return;
            m_proxy_error=m_source->validateAddress(m_address);m_tor_error=m_source->validateAddress(m_onion);
            m_valid=(!m_proxy||m_proxy_error.isEmpty())&&(!m_tor||m_tor_error.isEmpty());
            m_dirty=(m_source->canEdit("proxy")&&(m_proxy!=m_source->proxyEnabled()||m_address!=m_source->proxyAddress()))
                ||(m_source->canEdit("onion")&&(m_tor!=m_source->torEnabled()||m_onion!=m_source->torAddress()));
            m_pending=false;Q_EMIT changed();});
    }
    QPointer<MockCoreSettingsModel> m_source;quint64 m_revision{0},m_operation{0};int m_state{MockCoreSettingsModel::Idle};
    QString m_address,m_onion,m_proxy_error,m_tor_error,m_error;
    bool m_proxy{false},m_tor{false},m_pending{true},m_valid{false},m_dirty{false},m_ready{false};
};
MockCoreProxyDraft* MockCoreSettingsModel::createProxyDraft(){auto* result=new MockCoreProxyDraft(*this);QQmlEngine::setObjectOwnership(result,QQmlEngine::JavaScriptOwnership);return result;}

MockCoreSettingEntryModel::MockCoreSettingEntryModel(QString key, MockCoreSettingsModel& model, QObject* parent)
    : QObject{parent}
    , m_key{std::move(key)}
    , m_model{model}
{
}

QVariant MockCoreSettingEntryModel::value() const
{
    if (m_key == QStringLiteral("listen")) return m_model.listen();
    if (m_key == QStringLiteral("natpmp")) return m_model.natpmp();
    if (m_key == QStringLiteral("server")) return m_model.server();
    if (m_key == QStringLiteral("prune")) return m_model.pruneSizeGB();
    if (m_key == QStringLiteral("proxy")) return m_model.proxyAddress();
    if (m_key == QStringLiteral("onion")) return m_model.torAddress();
    return {};
}

void MockCoreSettingEntryModel::setValue(const QVariant& value)
{
    if (m_key == QStringLiteral("listen")) {
        m_model.setListen(value.toBool());
    } else if (m_key == QStringLiteral("natpmp")) {
        m_model.setNatpmp(value.toBool());
    } else if (m_key == QStringLiteral("server")) {
        m_model.setServer(value.toBool());
    } else if (m_key == QStringLiteral("prune")) {
        m_model.setPruneSizeGB(value.toInt());
    } else if (m_key == QStringLiteral("proxy")) {
        m_model.setProxyAddress(value.toString());
    } else if (m_key == QStringLiteral("onion")) {
        m_model.setTorAddress(value.toString());
    }
}

bool MockCoreSettingEntryModel::enabled() const
{
    if (m_key == QStringLiteral("listen")) return m_model.listen();
    if (m_key == QStringLiteral("natpmp")) return m_model.natpmp();
    if (m_key == QStringLiteral("server")) return m_model.server();
    if (m_key == QStringLiteral("prune")) return m_model.prune();
    if (m_key == QStringLiteral("proxy")) return m_model.proxyEnabled();
    if (m_key == QStringLiteral("onion")) return m_model.torEnabled();
    return false;
}

void MockCoreSettingEntryModel::setEnabled(bool enabled)
{
    if (m_key == QStringLiteral("listen")) {
        m_model.setListen(enabled);
    } else if (m_key == QStringLiteral("natpmp")) {
        m_model.setNatpmp(enabled);
    } else if (m_key == QStringLiteral("server")) {
        m_model.setServer(enabled);
    } else if (m_key == QStringLiteral("prune")) {
        m_model.setPrune(enabled);
    } else if (m_key == QStringLiteral("proxy")) {
        m_model.setProxyEnabled(enabled);
    } else if (m_key == QStringLiteral("onion")) {
        m_model.setTorEnabled(enabled);
    }
}

QString MockCoreSettingEntryModel::address() const
{
    if (m_key == QStringLiteral("proxy")) return m_model.proxyAddress();
    if (m_key == QStringLiteral("onion")) return m_model.torAddress();
    return {};
}

void MockCoreSettingEntryModel::setAddress(const QString& address)
{
    commitAddress(address);
}

QVariantMap MockCoreSettingEntryModel::status() const
{
    return m_model.status(m_key);
}

bool MockCoreSettingEntryModel::canEdit() const
{
    return status().value(QStringLiteral("canEdit"), true).toBool();
}

QString MockCoreSettingEntryModel::infoText() const
{
    return status().value(QStringLiteral("infoText")).toString();
}

QString MockCoreSettingEntryModel::validate(const QString& value) const
{
    return m_model.validateAddress(value);
}

bool MockCoreSettingEntryModel::commitAddress(const QString& address)
{
    if (m_key == QStringLiteral("proxy")) return m_model.setProxyAddress(address);
    if (m_key == QStringLiteral("onion")) return m_model.setTorAddress(address);
    return false;
}

QString MockCoreSettingEntryModel::defaultAddress() const
{
    return m_model.defaultAddress();
}

class MockOptionsTransactionLinks : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString txid READ txid WRITE setTxid NOTIFY txidChanged)
    Q_PROPERTY(QAbstractItemModel* links READ links CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)
public:
    MockOptionsTransactionLinks(QObject* owner, std::function<QVariantList(const QString&)> read)
        : QAbstractListModel(owner), m_read(std::move(read)) {}
    QString txid() const { return m_txid; }
    QAbstractItemModel* links() { return this; }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_links.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "host"}, {Qt::UserRole + 1, "url"}}; }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_links.size()) return {};
        const auto row = m_links[index.row()].toMap();
        return role == Qt::UserRole ? row.value("host") : role == Qt::UserRole + 1 ? row.value("url") : QVariant{};
    }
    bool pending() const { return m_pending; }
    QString error() const { return {}; }
    void setTxid(const QString& value) { if (value == m_txid) return; m_txid = value; Q_EMIT txidChanged(); refresh(); }
    void refresh() {
        const auto revision = ++m_revision;
        m_pending = true; Q_EMIT resultChanged();
        QTimer::singleShot(0, this, [this, revision] {
            if (revision != m_revision) return;
            beginResetModel(); m_links = m_read(m_txid); endResetModel();
            m_pending = false; Q_EMIT resultChanged();
        });
    }
Q_SIGNALS:
    void txidChanged();
    void resultChanged();
private:
    std::function<QVariantList(const QString&)> m_read;
    QString m_txid;
    QVariantList m_links;
    quint64 m_revision{0};
    bool m_pending{false};
};

class MockOptionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool settingsReady MEMBER m_settings_ready NOTIFY settingsReadyChanged)
    Q_PROPERTY(bool settingsPending MEMBER m_settings_pending NOTIFY settingsPendingChanged)
    Q_PROPERTY(QString settingsError MEMBER m_settings_error NOTIFY settingsErrorChanged)
    Q_PROPERTY(bool validationPending MEMBER m_validation_pending NOTIFY validationPendingChanged)
    Q_PROPERTY(bool signerPathValidationPending MEMBER m_signer_path_validation_pending NOTIFY signerPathValidationChanged)
    Q_PROPERTY(QString signerPathError MEMBER m_signer_path_error NOTIFY signerPathValidationChanged)
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(bool natpmp READ natpmp WRITE setNatpmp NOTIFY natpmpChanged)
    Q_PROPERTY(bool server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(int dbcacheSizeMiB MEMBER m_dbcache_size_mib NOTIFY dbcacheSizeMiBChanged)
    Q_PROPERTY(int minDbcacheSizeMiB MEMBER m_min_dbcache_size_mib CONSTANT)
    Q_PROPERTY(int maxDbcacheSizeMiB MEMBER m_max_dbcache_size_mib CONSTANT)
    Q_PROPERTY(int scriptThreads MEMBER m_script_threads NOTIFY scriptThreadsChanged)
    Q_PROPERTY(int minScriptThreads MEMBER m_min_script_threads CONSTANT)
    Q_PROPERTY(int maxScriptThreads MEMBER m_max_script_threads CONSTANT)
    Q_PROPERTY(int maxMempoolSizeMB READ maxMempoolSizeMB WRITE setMaxMempoolSizeMB NOTIFY maxMempoolSizeMBChanged)
    Q_PROPERTY(int maxMaxMempoolSizeMB MEMBER m_max_max_mempool_size_mb CONSTANT)
    Q_PROPERTY(int minMaxMempoolSizeMB MEMBER m_min_max_mempool_size_mb CONSTANT)
    Q_PROPERTY(bool prune READ prune WRITE setPrune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB READ pruneSizeGB WRITE setPruneSizeGB NOTIFY pruneSizeGBChanged)
    Q_PROPERTY(QString dataDir MEMBER m_data_dir NOTIFY dataDirChanged)
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString CONSTANT)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(QString displayUnitLabel READ displayUnitLabel NOTIFY displayUnitChanged)
    Q_PROPERTY(QString languageSummary READ languageSummary NOTIFY languageChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(bool connectionSettingsDirty MEMBER m_connection_settings_dirty NOTIFY connectionSettingsDirtyChanged)
    Q_PROPERTY(bool storageSettingsDirty MEMBER m_storage_settings_dirty NOTIFY storageSettingsDirtyChanged)
    Q_PROPERTY(bool developerSettingsDirty MEMBER m_developer_settings_dirty NOTIFY developerSettingsDirtyChanged)
    Q_PROPERTY(bool mempoolSettingsDirty MEMBER m_mempool_settings_dirty NOTIFY mempoolSettingsDirtyChanged)
    Q_PROPERTY(bool proxySettingsDirty MEMBER m_proxy_settings_dirty NOTIFY proxySettingsDirtyChanged)
    Q_PROPERTY(bool walletSettingsDirty MEMBER m_wallet_settings_dirty NOTIFY walletSettingsDirtyChanged)
    Q_PROPERTY(bool restartRequired MEMBER m_restart_required NOTIFY restartRequiredChanged)
    Q_PROPERTY(QObject* coreSettings READ coreSettings CONSTANT)
    Q_PROPERTY(QVariantMap coreSettingStatuses READ coreSettingStatuses NOTIFY coreSettingStatusesChanged)
    Q_PROPERTY(QString previewError MEMBER m_preview_error NOTIFY previewErrorChanged)
    Q_PROPERTY(bool canFinish READ canFinish NOTIFY canFinishChanged)
    Q_PROPERTY(int assumedBlockchainSize MEMBER m_assumed_blockchain_size NOTIFY assumedSizesChanged)
    Q_PROPERTY(int assumedChainstateSize MEMBER m_assumed_chainstate_size NOTIFY assumedSizesChanged)
    Q_PROPERTY(bool existingProfile READ existingProfile WRITE setExistingProfile NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageCheckPending MEMBER m_storage_check_pending NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY storageStatusChanged)
    Q_PROPERTY(int storageAvailableGB MEMBER m_storage_available_gb NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageAvailableText READ storageAvailableText NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storagePathMessage MEMBER m_storage_path_message NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageWarningText MEMBER m_storage_warning_text NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageErrorText MEMBER m_storage_error_text NOTIFY storageStatusChanged)
    Q_PROPERTY(int storageMinimumRequiredGB READ storageMinimumRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int fullStorageRequiredGB READ fullStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int prunedStorageRequiredGB READ prunedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int selectedStorageRequiredGB READ selectedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForSelected READ storageEnoughForSelected NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForFull READ storageEnoughForFull NOTIFY storageStatusChanged)
    Q_PROPERTY(QString thirdPartyTransactionUrls MEMBER m_third_party_transaction_urls NOTIFY thirdPartyTransactionUrlsChanged)
    Q_PROPERTY(QString moneyFontChoice MEMBER m_money_font_choice NOTIFY moneyFontChoiceChanged)
    Q_PROPERTY(QString externalSignerPath MEMBER m_external_signer_path NOTIFY externalSignerPathChanged)
    Q_PROPERTY(QFont moneyFont READ moneyFont NOTIFY moneyFontChanged)

public:
    bool m_settings_ready{true};
    bool m_settings_pending{false};
    bool m_validation_pending{false};
    QString m_settings_error;
    bool m_signer_path_validation_pending{false};
    QString m_signer_path_error;
    Q_PROPERTY(bool holdSignerPathValidationForTest MEMBER m_hold_signer_validation)
    bool m_hold_signer_validation{false};
    Q_INVOKABLE void requestExternalSignerPathValidation(const QString&)
    {
        m_signer_path_validation_pending = m_hold_signer_validation;
        m_signer_path_error.clear();
        Q_EMIT signerPathValidationChanged();
    }
    Q_INVOKABLE void finishSignerPathValidationForTest(const QString& error)
    {
        m_signer_path_validation_pending = false;
        m_signer_path_error = error;
        Q_EMIT signerPathValidationChanged();
    }
    bool m_listen{true};
    bool m_natpmp{false};
    bool m_server{false};
    int m_dbcache_size_mib{450};
    int m_min_dbcache_size_mib{4};
    int m_max_dbcache_size_mib{1024};
    int m_script_threads{0};
    int m_min_script_threads{-8};
    int m_max_script_threads{15};
    int m_max_mempool_size_mb{300};
    int m_max_max_mempool_size_mb{99999};
    int m_min_max_mempool_size_mb{1};
    bool m_prune{true};
    int m_prune_size_gb{2};
    QString m_data_dir{QStringLiteral("/tmp/bitcoin-default")};
    QString m_custom_data_dir{QStringLiteral("/tmp/bitcoin-custom")};
    bool m_connection_settings_dirty{false};
    bool m_storage_settings_dirty{false};
    bool m_developer_settings_dirty{false};
    bool m_mempool_settings_dirty{false};
    bool m_proxy_settings_dirty{false};
    bool m_wallet_settings_dirty{false};
    bool m_restart_required{false};
    QString m_preview_error;
    int m_assumed_blockchain_size{610};
    int m_assumed_chainstate_size{12};
    bool m_existing_profile{false};
    bool m_storage_check_pending{false};
    int m_storage_available_gb{123};
    QString m_storage_path_message{QStringLiteral("Directory already exists.")};
    QString m_storage_warning_text;
    QString m_storage_error_text;
    QString m_third_party_transaction_urls;
    QString m_money_font_choice{QStringLiteral("embedded")};
    QString m_external_signer_path;
    QVariantMap m_core_setting_status_overrides;
    MockCoreSettingsModel m_core_settings;

    MockOptionsModel()
    {
        connect(&m_core_settings, &MockCoreSettingsModel::changed, this, &MockOptionsModel::syncCoreSettingsFromDocument);
        m_core_settings.setListen(m_listen);
        m_core_settings.setNatpmp(m_natpmp);
        m_core_settings.setServer(m_server);
        m_core_settings.setPrune(m_prune);
        m_core_settings.setPruneSizeGB(m_prune_size_gb);
        m_core_settings.setStatuses(coreSettingStatuses());
    }

    bool listen() const { return m_core_settings.listen(); }
    void setListen(bool value) { m_core_settings.setListen(value); }
    bool natpmp() const { return m_core_settings.natpmp(); }
    void setNatpmp(bool value) { m_core_settings.setNatpmp(value); }
    bool server() const { return m_core_settings.server(); }
    void setServer(bool value) { m_core_settings.setServer(value); }
    int maxMempoolSizeMB() const { return m_max_mempool_size_mb; }
    void setMaxMempoolSizeMB(int value)
    {
        if (value == m_max_mempool_size_mb) return;
        m_max_mempool_size_mb = value;
        Q_EMIT maxMempoolSizeMBChanged(value);
    }
    QString getDefaultDataDirString() const { return QStringLiteral("/tmp/bitcoin-default"); }
    bool canFinish() const { return m_preview_error.isEmpty() && !m_storage_check_pending && m_storage_error_text.isEmpty(); }
    QString storageStatus() const
    {
        if (m_storage_check_pending) return QStringLiteral("checking");
        if (!m_storage_error_text.isEmpty()) return QStringLiteral("error");
        if (!m_storage_warning_text.isEmpty()) return QStringLiteral("warning");
        return QStringLiteral("ok");
    }
    QString storageAvailableText() const
    {
        return m_storage_check_pending ? QStringLiteral("Checking available storage...") : QStringLiteral("%1GB available").arg(m_storage_available_gb);
    }
    bool existingProfile() const { return m_existing_profile; }
    void setExistingProfile(bool value)
    {
        if (m_existing_profile == value) return;
        m_existing_profile = value;
        Q_EMIT storageStatusChanged();
        Q_EMIT canFinishChanged();
    }
    int storageMinimumRequiredGB() const { return m_existing_profile ? 1 : m_assumed_chainstate_size + 2; }
    int fullStorageRequiredGB() const { return m_assumed_blockchain_size + m_assumed_chainstate_size; }
    bool prune() const { return m_core_settings.prune(); }
    void setPrune(bool value) { m_core_settings.setPrune(value); }
    int pruneSizeGB() const { return m_core_settings.pruneSizeGB(); }
    void setPruneSizeGB(int value) { m_core_settings.setPruneSizeGB(value); }
    int prunedStorageRequiredGB() const { return pruneSizeGB() + m_assumed_chainstate_size; }
    int selectedStorageRequiredGB() const { return m_existing_profile ? storageMinimumRequiredGB() : (prune() ? prunedStorageRequiredGB() : fullStorageRequiredGB()); }
    bool storageEnoughForSelected() const { return m_storage_available_gb >= selectedStorageRequiredGB(); }
    bool storageEnoughForFull() const { return m_existing_profile ? storageEnoughForSelected() : m_storage_available_gb >= fullStorageRequiredGB(); }
    Q_INVOKABLE QString getCustomDataDirString() const { return m_custom_data_dir; }
    Q_INVOKABLE void setCustomDataDirString(const QString& dir) { m_custom_data_dir = dir; }
    Q_INVOKABLE void setCustomDataDirArgs(const QString& dir) { selectCustomDataDir(dir); }
    Q_INVOKABLE QString validateCustomDataDir(const QString&) const { return {}; }
    Q_INVOKABLE bool selectCustomDataDir(const QString& dir) { m_custom_data_dir = dir; m_data_dir = dir; Q_EMIT dataDirChanged(); return true; }
    Q_INVOKABLE void useDefaultDataDir() { m_data_dir = getDefaultDataDirString(); Q_EMIT dataDirChanged(); }
    Q_INVOKABLE void setStorageStatusForTest(bool pending, int available_gb, const QString& error_text, const QString& warning_text) {
        m_storage_check_pending = pending;
        m_storage_available_gb = available_gb;
        m_storage_error_text = error_text;
        m_storage_warning_text = warning_text;
        Q_EMIT storageStatusChanged();
        Q_EMIT canFinishChanged();
    }
    Q_INVOKABLE QString validateProxyLocation(const QString& location) const { return location.length() > 0 ? QString{} : QStringLiteral("Proxy location is required."); }
    Q_INVOKABLE bool commitProxyLocation(const QString&) { return true; }
    Q_INVOKABLE bool commitTorLocation(const QString&) { return true; }
    Q_INVOKABLE QString defaultProxyAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    Q_INVOKABLE QString externalSignerPathValidationError(const QString&) const { return {}; }
    QObject* coreSettings() { return &m_core_settings; }
    QVariantMap coreSettingStatuses() const {
        QVariantMap statuses;
        const QStringList names{
            QStringLiteral("listen"),
            QStringLiteral("natpmp"),
            QStringLiteral("server"),
            QStringLiteral("prune"),
            QStringLiteral("dbcache"),
            QStringLiteral("par"),
            QStringLiteral("maxmempool"),
            QStringLiteral("proxy"),
            QStringLiteral("onion"),
            QStringLiteral("signer"),
            QStringLiteral("lang"),
        };
        for (const QString& name : names) {
            statuses.insert(name, coreSettingStatus(name));
        }
        return statuses;
    }
    Q_INVOKABLE QVariantMap coreSettingStatus(const QString& name) const {
        if (m_core_setting_status_overrides.contains(name)) {
            return m_core_setting_status_overrides.value(name).toMap();
        }
        return defaultCoreSettingStatus();
    }
    Q_INVOKABLE void setCoreSettingStatusForTest(const QString& name, bool can_edit, const QString& source, const QString& info_text, bool creates_gui_override) {
        QVariantMap status = defaultCoreSettingStatus();
        status.insert(QStringLiteral("source"), source);
        status.insert(QStringLiteral("canEdit"), can_edit);
        status.insert(QStringLiteral("commandLineOverridden"), !can_edit);
        status.insert(QStringLiteral("hasRwSetting"), source == QStringLiteral("settings_json"));
        status.insert(QStringLiteral("hasConfigSetting"), source == QStringLiteral("bitcoin_conf"));
        status.insert(QStringLiteral("createsGuiOverride"), creates_gui_override);
        status.insert(QStringLiteral("infoText"), info_text);
        m_core_setting_status_overrides.insert(name, status);
        m_core_settings.setStatuses(coreSettingStatuses());
        Q_EMIT coreSettingStatusesChanged();
    }
    Q_INVOKABLE void clearCoreSettingStatusesForTest() {
        m_core_setting_status_overrides.clear();
        m_core_settings.setStatuses(coreSettingStatuses());
        Q_EMIT coreSettingStatusesChanged();
    }
    QVariantMap defaultCoreSettingStatus() const {
        QVariantMap status;
        status.insert(QStringLiteral("source"), QStringLiteral("default"));
        status.insert(QStringLiteral("canEdit"), true);
        status.insert(QStringLiteral("commandLineOverridden"), false);
        status.insert(QStringLiteral("hasRwSetting"), false);
        status.insert(QStringLiteral("hasConfigSetting"), false);
        status.insert(QStringLiteral("createsGuiOverride"), false);
        status.insert(QStringLiteral("infoText"), QString{});
        return status;
    }
    Q_INVOKABLE MockOptionsTransactionLinks* createTransactionLinks() {
        auto* view = new MockOptionsTransactionLinks(this, [this](const QString& txid) { return preparedTransactionLinks(txid); });
        connect(this, &MockOptionsModel::thirdPartyTransactionUrlsChanged, view, &MockOptionsTransactionLinks::refresh);
        return view;
    }
    QVariantList preparedTransactionLinks(const QString& txid) const {
        QVariantList result;
        if (m_third_party_transaction_urls.isEmpty()) return result;
        QVariantMap link;
        link.insert("host", "example.com");
        link.insert("url", QString("https://example.com/tx/%1").arg(txid));
        result.push_back(link);
        return result;
    }

    int displayUnit() const { return m_displayUnit; }
    void setDisplayUnit(int u) {
        if (u != m_displayUnit) { m_displayUnit = u; Q_EMIT displayUnitChanged(u); }
    }
    QString displayUnitLabel() const
    {
        if (m_displayUnit == 1) return QStringLiteral("mBTC");
        if (m_displayUnit == 2) return QStringLiteral("bits");
        if (m_displayUnit == 3) return QStringLiteral("sat");
        return QStringLiteral("BTC");
    }
    Q_INVOKABLE QString displayUnitLabelForAmount(qint64 satoshi) const {
        if (m_displayUnit == 1) return QString("mBTC");
        if (m_displayUnit == 2) return QString("bits");
        if (m_displayUnit != 3) return QString("₿");
        return (qAbs(satoshi) == 1) ? QString("sat") : QString("sats");
    }
    QString language() const { return m_language; }
    void setLanguage(const QString& l) {
        if (l != m_language) { m_language = l; Q_EMIT languageChanged(); }
    }
    QString languageSummary() const { return m_language.isEmpty() ? "System default" : m_language; }
    QStringList availableLanguages() const { return {"", "de", "es", "fr"}; }
    QFont moneyFont() const { return QFont(QStringLiteral("Roboto Mono")); }
    Q_INVOKABLE QString languageLabel(const QString& tag) const {
        if (tag.isEmpty()) return "System default";
        if (tag == "de") return "Deutsch — German";
        if (tag == "es") return "Español — Spanish";
        if (tag == "fr") return "Français — French";
        return tag;
    }

    void syncCoreSettingsFromDocument() {
        if (m_listen != m_core_settings.listen()) {
            m_listen = m_core_settings.listen();
            Q_EMIT listenChanged();
        }
        if (m_natpmp != m_core_settings.natpmp()) {
            m_natpmp = m_core_settings.natpmp();
            Q_EMIT natpmpChanged();
        }
        if (m_server != m_core_settings.server()) {
            m_server = m_core_settings.server();
            Q_EMIT serverChanged();
        }
        if (m_prune != m_core_settings.prune()) {
            m_prune = m_core_settings.prune();
            Q_EMIT pruneChanged();
            Q_EMIT storageStatusChanged();
        }
        if (m_prune_size_gb != m_core_settings.pruneSizeGB()) {
            m_prune_size_gb = m_core_settings.pruneSizeGB();
            Q_EMIT pruneSizeGBChanged();
            Q_EMIT storageStatusChanged();
        }
    }

Q_SIGNALS:
    void settingsReadyChanged();
    void settingsPendingChanged();
    void settingsErrorChanged();
    void validationPendingChanged();
    void signerPathValidationChanged();
    void dataDirSelectionFinished(bool success, const QString& error);
    void listenChanged();
    void natpmpChanged();
    void serverChanged();
    void dbcacheSizeMiBChanged();
    void scriptThreadsChanged();
    void maxMempoolSizeMBChanged(int value);
    void pruneChanged();
    void pruneSizeGBChanged();
    void dataDirChanged();
    void displayUnitChanged(int unit);
    void languageChanged();
    void connectionSettingsDirtyChanged();
    void storageSettingsDirtyChanged();
    void developerSettingsDirtyChanged();
    void mempoolSettingsDirtyChanged();
    void proxySettingsDirtyChanged();
    void walletSettingsDirtyChanged();
    void restartRequiredChanged();
    void coreSettingStatusesChanged();
    void previewErrorChanged();
    void canFinishChanged();
    void assumedSizesChanged();
    void storageStatusChanged();
    void thirdPartyTransactionUrlsChanged();
    void moneyFontChoiceChanged();
    void externalSignerPathChanged();
    void moneyFontChanged();

private:
    int m_displayUnit{0};
    QString m_language;
};

class MockChainModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int assumedChainstateSize MEMBER m_assumed_chainstate_size CONSTANT)
    Q_PROPERTY(int assumedBlockchainSize MEMBER m_assumed_blockchain_size CONSTANT)
    Q_PROPERTY(QString networkName MEMBER m_network_name CONSTANT)

public:
    int m_assumed_chainstate_size{12};
    int m_assumed_blockchain_size{610};
    QString m_network_name{QStringLiteral("REGTEST")};
};

class MockBlockClockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal currentTimeFraction MEMBER m_current_time_fraction NOTIFY currentTimeFractionChanged)
    Q_PROPERTY(QList<qreal> blockTimeFractions MEMBER m_block_time_fractions NOTIFY blockTimeFractionsChanged)
    Q_PROPERTY(QVariant blockSnapshot READ blockSnapshot NOTIFY blockTimeFractionsChanged)
    Q_PROPERTY(QString historyError READ historyError CONSTANT)
    Q_PROPERTY(bool historyPending READ historyPending CONSTANT)

public:
    QVariant blockSnapshot() const { auto value = std::make_shared<blockclock::Projection>(); value->fractions = m_block_time_fractions; m_snapshot = std::move(value); return QVariant::fromValue(blockclock::WeakSnapshot{m_snapshot}); }
    QString historyError() const { return {}; }
    bool historyPending() const { return false; }
    Q_INVOKABLE void refreshHistory() {}
    mutable blockclock::Snapshot m_snapshot;
    qreal m_current_time_fraction{0.25};
    QList<qreal> m_block_time_fractions{0.1, 0.2};

Q_SIGNALS:
    void currentTimeFractionChanged();
    void blockTimeFractionsChanged();
};

class MockNodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool networkActionReady MEMBER m_network_action_ready NOTIFY networkActionReadyChanged)
    Q_PROPERTY(bool networkActionPending MEMBER m_network_action_pending NOTIFY networkActionPendingChanged)
    Q_PROPERTY(QString networkActionError MEMBER m_network_action_error NOTIFY networkActionErrorChanged)
    Q_PROPERTY(bool pause MEMBER m_pause NOTIFY pauseChanged)
    Q_PROPERTY(int numPeers MEMBER m_num_peers NOTIFY numPeersChanged)
    Q_PROPERTY(int numInboundPeers MEMBER m_num_inbound_peers NOTIFY numInboundPeersChanged)
    Q_PROPERTY(int numOutboundPeers MEMBER m_num_outbound_peers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers MEMBER m_max_num_outbound_peers NOTIFY maxNumOutboundPeersChanged)
    Q_PROPERTY(double verificationProgress MEMBER m_verification_progress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool initialSyncComplete MEMBER m_initial_sync_complete NOTIFY initialSyncCompleteChanged)
    Q_PROPERTY(int remainingSyncTime MEMBER m_remaining_sync_time NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(bool headerSyncActive MEMBER m_header_sync_active NOTIFY headerSyncChanged)
    Q_PROPERTY(bool blockSyncActive MEMBER m_block_sync_active NOTIFY blockSyncActiveChanged)
    Q_PROPERTY(bool headerPresync MEMBER m_header_presync NOTIFY headerSyncChanged)
    Q_PROPERTY(double headerSyncProgress MEMBER m_header_sync_progress NOTIFY headerSyncChanged)
    Q_PROPERTY(bool faulted MEMBER m_faulted NOTIFY faultedChanged)
    Q_PROPERTY(QAbstractItemModel* startupDiagnostics READ startupDiagnostics CONSTANT)
    Q_PROPERTY(QAbstractItemModel* runtimeDialogDiagnostics READ runtimeDialogDiagnostics NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString startupError MEMBER m_startup_error NOTIFY startupErrorChanged)
    Q_PROPERTY(bool fatalException MEMBER m_fatal_exception NOTIFY fatalExceptionChanged)
    Q_PROPERTY(QString warnings MEMBER m_warnings NOTIFY warningsChanged)
    Q_PROPERTY(QStringList warningList MEMBER m_warning_list NOTIFY warningsChanged)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY warningsChanged)
    Q_PROPERTY(bool runtimeDialogVisible MEMBER m_runtime_dialog_visible NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogTitle MEMBER m_runtime_dialog_title NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogMessage MEMBER m_runtime_dialog_message NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogIcon MEMBER m_runtime_dialog_icon NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int runtimeDialogButtons MEMBER m_runtime_dialog_buttons NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogQuestion MEMBER m_runtime_dialog_question NOTIFY runtimeDialogChanged)
    Q_PROPERTY(int blockTipHeight MEMBER m_block_tip_height NOTIFY blockTipHeightChanged)
    Q_PROPERTY(int mempoolTransactionCount MEMBER m_mempool_transaction_count NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolUsageMB MEMBER m_mempool_usage_mb NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolMaxUsageMB MEMBER m_mempool_max_usage_mb NOTIFY mempoolInfoChanged)
    Q_PROPERTY(bool mempoolInfoPollingActive READ mempoolInfoPollingActive WRITE setMempoolInfoPollingActive NOTIFY mempoolInfoPollingActiveChanged)
    Q_PROPERTY(bool mempoolInformationAvailable MEMBER m_mempool_information_available NOTIFY mempoolInformationAvailableChanged)
    Q_PROPERTY(bool disconnectPeerResult MEMBER m_disconnect_peer_result NOTIFY peerActionStateChanged)
    Q_PROPERTY(bool banPeerResult MEMBER m_ban_peer_result NOTIFY peerActionStateChanged)
    Q_PROPERTY(QVariantList informationRows READ nodeInformationRows NOTIFY peerActionStateChanged)
    Q_PROPERTY(bool autoCompletePeerAction MEMBER m_auto_complete_peer_action)
    Q_PROPERTY(int disconnectPeerCalls READ disconnectPeerCalls NOTIFY peerActionCallsChanged)
    Q_PROPERTY(int banPeerCalls READ banPeerCalls NOTIFY peerActionCallsChanged)
    Q_PROPERTY(int lastDisconnectedNodeId MEMBER m_last_disconnected_node_id NOTIFY peerActionCallsChanged)
    Q_PROPERTY(QString lastBannedAddress MEMBER m_last_banned_address NOTIFY peerActionCallsChanged)

public:
    enum class PeerActionOutcome { Succeeded, Rejected, PartiallyApplied, Uncertain };
    Q_ENUM(PeerActionOutcome)
    QAbstractItemModel* startupDiagnostics() const { return nullptr; }
    QAbstractItemModel* runtimeDialogDiagnostics() const { return nullptr; }

    bool m_network_action_ready{true};
    bool m_network_action_pending{false};
    QString m_network_action_error;
    int m_last_disconnected_node_id{-1};
    QString m_last_banned_address;
    bool m_pause{false};
    int m_num_peers{0};
    int m_num_inbound_peers{0};
    int m_num_outbound_peers{0};
    int m_max_num_outbound_peers{8};
    double m_verification_progress{0.0};
    bool m_initial_sync_complete{false};
    int m_remaining_sync_time{0};
    bool m_header_sync_active{false};
    bool m_block_sync_active{false};
    bool m_header_presync{false};
    double m_header_sync_progress{0.0};
    bool m_faulted{false};
    QString m_startup_error;
    bool m_fatal_exception{false};
    QString m_warnings;
    QStringList m_warning_list;
    bool m_runtime_dialog_visible{false};
    QString m_runtime_dialog_title;
    QString m_runtime_dialog_message;
    QString m_runtime_dialog_icon{QStringLiteral("image://images/info-filled")};
    unsigned int m_runtime_dialog_buttons{0};
    bool m_runtime_dialog_question{false};
    int m_block_tip_height{0};
    int m_mempool_transaction_count{0};
    double m_mempool_usage_mb{0.0};
    double m_mempool_max_usage_mb{300.0};
    bool m_mempool_info_polling_active{false};
    bool m_mempool_information_available{true};
    bool m_disconnect_peer_result{true};
    bool m_ban_peer_result{true};
    bool m_auto_complete_peer_action{true};
    quint64 m_peer_ticket{0};
    struct PeerOutcome { PeerActionOutcome outcome; QString error; };
    QMap<quint64, PeerOutcome> m_peer_outcomes;
    int m_disconnect_peer_calls{0};
    int m_ban_peer_calls{0};
    bool hasWarnings() const { return !m_warning_list.isEmpty(); }
    bool mempoolInfoPollingActive() const { return m_mempool_info_polling_active; }
    int disconnectPeerCalls() const { return m_disconnect_peer_calls; }
    int banPeerCalls() const { return m_ban_peer_calls; }
    void setMempoolInfoPollingActive(bool active)
    {
        if (m_mempool_info_polling_active == active) return;
        m_mempool_info_polling_active = active;
        Q_EMIT mempoolInfoPollingActiveChanged(active);
    }

    Q_INVOKABLE void startNodeInitializionThread() {}
    Q_INVOKABLE void requestShutdown() { Q_EMIT requestedShutdown(); }
    Q_INVOKABLE void answerRuntimeDialog(unsigned int button)
    {
        Q_UNUSED(button);
        m_runtime_dialog_visible = false;
        Q_EMIT runtimeDialogChanged();
    }
    Q_INVOKABLE QVariantList nodeInformationRows() const
    {
        QVariantMap version;
        version.insert(QStringLiteral("label"), QStringLiteral("Client version"));
        version.insert(QStringLiteral("value"), QStringLiteral("Bitcoin Core test"));

        QVariantMap network;
        network.insert(QStringLiteral("label"), QStringLiteral("Network"));
        network.insert(QStringLiteral("value"), QStringLiteral("regtest"));

        QVariantMap peers;
        peers.insert(QStringLiteral("label"), QStringLiteral("Peers"));
        peers.insert(QStringLiteral("value"), QStringLiteral("0 total (0 inbound, 0 outbound)"));

        QVariantList rows;
        rows.push_back(QVariant::fromValue(version));
        rows.push_back(QVariant::fromValue(network));
        rows.push_back(QVariant::fromValue(peers));
        if (!m_warning_list.isEmpty()) {
            QVariantMap warnings;
            warnings.insert(QStringLiteral("label"), QStringLiteral("Warnings"));
            warnings.insert(QStringLiteral("value"), m_warning_list.join(QStringLiteral("\n")));
            rows.push_back(QVariant::fromValue(warnings));
        }
        return rows;
    }
    Q_INVOKABLE void setWarningsForTest(const QStringList& warnings)
    {
        m_warning_list = warnings;
        m_warnings = warnings.join(QStringLiteral("<hr />"));
        Q_EMIT warningsChanged();
    }
    Q_INVOKABLE void setBlockSyncActiveForTest(bool active)
    {
        if (m_block_sync_active == active) return;
        m_block_sync_active = active;
        Q_EMIT blockSyncActiveChanged();
    }
    Q_INVOKABLE void setRuntimeDialogForTest(const QString& title, const QString& message, unsigned int buttons, bool question)
    {
        m_runtime_dialog_title = title;
        m_runtime_dialog_message = message;
        m_runtime_dialog_icon = question ? QStringLiteral("image://images/alert-filled") : QStringLiteral("image://images/info-filled");
        m_runtime_dialog_buttons = buttons;
        m_runtime_dialog_question = question;
        m_runtime_dialog_visible = true;
        Q_EMIT runtimeDialogChanged();
    }
    Q_INVOKABLE void setStartupErrorForTest(const QString& error)
    {
        m_startup_error = error;
        m_faulted = !error.isEmpty();
        Q_EMIT startupErrorChanged();
        Q_EMIT faultedChanged();
    }
    Q_INVOKABLE void resetMempoolInfoPollingTestState()
    {
        setMempoolInfoPollingActive(false);
    }
    Q_INVOKABLE void resetPeerActionTestState()
    {
        m_disconnect_peer_result = true;
        m_ban_peer_result = true;
        m_auto_complete_peer_action = true;
        m_peer_outcomes.clear();
        m_disconnect_peer_calls = 0;
        m_ban_peer_calls = 0;
        Q_EMIT peerActionStateChanged();
        Q_EMIT peerActionCallsChanged();
    }
    Q_INVOKABLE QString defaultProxyAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    Q_INVOKABLE bool validateProxyAddress(const QString& value) const
    {
        // Minimal test stub to emulate host:port and [ipv6]:port acceptance.
        static const QRegularExpression pattern{
            QStringLiteral(R"(^(\[[0-9A-Fa-f:.]+\]|[0-9]{1,3}(\.[0-9]{1,3}){3}):([0-9]{1,5})$)")
        };
        return pattern.match(value).hasMatch();
    }
    Q_INVOKABLE bool disconnectPeer(int node_id)
    {
        m_last_disconnected_node_id = node_id;
        ++m_disconnect_peer_calls;
        Q_EMIT peerActionCallsChanged();
        return m_disconnect_peer_result;
    }
    Q_INVOKABLE quint64 requestDisconnectPeer(qint64 node_id)
    {
        const bool success = disconnectPeer(node_id);
        return queuePeerOutcome(success, success ? QString{} : QStringLiteral("The peer could not be disconnected. It may already be disconnected."));
    }
    Q_INVOKABLE quint64 requestBanPeer(const QString& address, qint64 duration)
    {
        const bool success = banPeer(address, duration);
        return queuePeerOutcome(success, success ? QString{} : QStringLiteral("The peer could not be banned."));
    }
    quint64 queuePeerOutcome(bool success, QString error)
    {
        const auto ticket = ++m_peer_ticket;
        m_peer_outcomes.insert(ticket, {success ? PeerActionOutcome::Succeeded : PeerActionOutcome::Rejected, std::move(error)});
        if (m_auto_complete_peer_action) QTimer::singleShot(0, this, [this, ticket] { completePeerAction(ticket); });
        return ticket;
    }
    Q_INVOKABLE void completePeerAction(quint64 ticket)
    {
        const auto found = m_peer_outcomes.find(ticket);
        if (found == m_peer_outcomes.end()) return;
        auto result = std::move(found.value());
        m_peer_outcomes.erase(found);
        Q_EMIT peerActionFinished(ticket, static_cast<int>(result.outcome), result.error);
    }
    Q_INVOKABLE void completePeerActionWithOutcome(quint64 ticket, int outcome, const QString& error)
    {
        const auto found = m_peer_outcomes.find(ticket);
        if (found == m_peer_outcomes.end() || outcome < 0 || outcome > static_cast<int>(PeerActionOutcome::Uncertain)) return;
        found->outcome = static_cast<PeerActionOutcome>(outcome); found->error = error;
        completePeerAction(ticket);
    }
    Q_INVOKABLE bool banPeer(const QString& raw_address, qint64 ban_duration)
    {
        m_last_banned_address = raw_address;
        Q_UNUSED(ban_duration);
        ++m_ban_peer_calls;
        Q_EMIT peerActionCallsChanged();
        return m_ban_peer_result;
    }

Q_SIGNALS:
    void peerActionFinished(quint64 ticket, int outcome, const QString& error);
    void networkActionReadyChanged();
    void networkActionPendingChanged();
    void networkActionErrorChanged();
    void requestedShutdown();
    void pauseChanged();
    void numPeersChanged();
    void numInboundPeersChanged();
    void numOutboundPeersChanged();
    void maxNumOutboundPeersChanged();
    void verificationProgressChanged();
    void initialSyncCompleteChanged();
    void remainingSyncTimeChanged();
    void headerSyncChanged();
    void blockSyncActiveChanged();
    void faultedChanged();
    void startupErrorChanged();
    void fatalExceptionChanged();
    void warningsChanged();
    void runtimeDialogChanged();
    void blockTipHeightChanged();
    void mempoolInfoChanged();
    void mempoolInfoPollingActiveChanged(bool active);
    void mempoolInformationAvailableChanged();
    void peerActionStateChanged();
    void peerActionCallsChanged();
};

class MockPeerTableModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool autoRefreshActive MEMBER m_auto_refresh_active NOTIFY autoRefreshActiveChanged)
    Q_PROPERTY(int refreshCalls READ refreshCalls NOTIFY refreshCallsChanged)

public:
    Q_INVOKABLE void startAutoRefresh() { m_auto_refresh_active = true; Q_EMIT autoRefreshActiveChanged(); }
    Q_INVOKABLE void stopAutoRefresh() { m_auto_refresh_active = false; Q_EMIT autoRefreshActiveChanged(); }
    Q_INVOKABLE void refresh()
    {
        ++m_refresh_calls;
        Q_EMIT refreshCallsChanged();
    }
    Q_INVOKABLE void resetTestState()
    {
        m_refresh_calls = 0;
        Q_EMIT refreshCallsChanged();
    }
    int refreshCalls() const { return m_refresh_calls; }

Q_SIGNALS:
    void refreshCallsChanged();
    void autoRefreshActiveChanged();

private:
    int m_refresh_calls{0};
    bool m_auto_refresh_active{false};
};

class MockNetworkTrafficTower : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(quint64 totalBytesReceived MEMBER m_total_bytes_received NOTIFY totalBytesReceivedChanged)
    Q_PROPERTY(quint64 totalBytesSent MEMBER m_total_bytes_sent NOTIFY totalBytesSentChanged)
    Q_PROPERTY(double maxReceivedRateBps MEMBER m_max_received_rate_bps NOTIFY maxReceivedRateBpsChanged)
    Q_PROPERTY(double maxSentRateBps MEMBER m_max_sent_rate_bps NOTIFY maxSentRateBpsChanged)
    Q_PROPERTY(QQueue<float> receivedRateList MEMBER m_received_rate_list NOTIFY receivedRateListChanged)
    Q_PROPERTY(QQueue<float> sentRateList MEMBER m_sent_rate_list NOTIFY sentRateListChanged)
    Q_PROPERTY(int lastFilterWindowSize MEMBER m_last_filter_window_size NOTIFY lastFilterWindowSizeChanged)

public:
    MockNetworkTrafficTower()
    {
        m_received_rate_list << 10.0f << 20.0f << 30.0f;
        m_sent_rate_list << 15.0f << 25.0f << 35.0f;
    }

    bool active() const { return m_active; }
    void setActive(bool active)
    {
        if (m_active == active) return;
        m_active = active;
        Q_EMIT activeChanged();
    }

    bool m_active{false};
    quint64 m_total_bytes_received{1'000};
    quint64 m_total_bytes_sent{2'000};
    double m_max_received_rate_bps{100.0};
    double m_max_sent_rate_bps{200.0};
    QQueue<float> m_received_rate_list;
    QQueue<float> m_sent_rate_list;
    int m_last_filter_window_size{30};

    Q_INVOKABLE void updateFilterWindowSize(const int window_size)
    {
        m_last_filter_window_size = window_size;
        Q_EMIT lastFilterWindowSizeChanged();
    }

Q_SIGNALS:
    void activeChanged();
    void totalBytesReceivedChanged();
    void totalBytesSentChanged();
    void maxReceivedRateBpsChanged();
    void maxSentRateBpsChanged();
    void receivedRateListChanged();
    void sentRateListChanged();
    void lastFilterWindowSizeChanged();
};

class MockNetworkStatusModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool reachabilityAvailable MEMBER m_reachability_available NOTIFY reachabilityChanged)
    Q_PROPERTY(bool networkOffline MEMBER m_network_offline NOTIFY reachabilityChanged)
    Q_PROPERTY(QString reachability MEMBER m_reachability NOTIFY reachabilityChanged)

public:
    bool m_reachability_available{true};
    bool m_network_offline{false};
    QString m_reachability{QStringLiteral("Online")};

    Q_INVOKABLE void setNetworkOfflineForTest(bool offline)
    {
        if (m_network_offline == offline) return;
        m_network_offline = offline;
        m_reachability = offline ? QStringLiteral("Disconnected") : QStringLiteral("Online");
        Q_EMIT reachabilityChanged();
    }

Q_SIGNALS:
    void reachabilityChanged();
};

class MockPeerListModelProxy : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(bool sortAscending MEMBER m_sort_ascending NOTIFY sortAscendingChanged)
    Q_PROPERTY(QString searchText MEMBER m_search_text NOTIFY searchTextChanged)
    Q_PROPERTY(QStringList directionFilters MEMBER m_direction_filters NOTIFY directionFiltersChanged)
    Q_PROPERTY(QStringList connectionTypeFilters MEMBER m_connection_type_filters NOTIFY connectionTypeFiltersChanged)
    Q_PROPERTY(QStringList networkFilters MEMBER m_network_filters NOTIFY networkFiltersChanged)
    Q_PROPERTY(QStringList transportFilters MEMBER m_transport_filters NOTIFY transportFiltersChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NodeIdRole = Qt::UserRole,
        AddressRole,
        SubversionRole,
        DirectionRole,
        ConnectionTypeRole,
        NetworkRole,
        TransportRole,
        SentRole,
        ReceivedRole,
    };

    QString sortBy() const { return m_sort_by; }
    void setSortBy(const QString& value)
    {
        if (m_sort_by == value) return;
        m_sort_by = value;
        Q_EMIT sortByChanged(m_sort_by);
    }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return m_peer_count;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_peer_count) return {};
        switch (role) {
        case NodeIdRole: return index.row();
        case AddressRole: return QStringLiteral("127.0.0.1:8333");
        case SubversionRole: return QStringLiteral("/Satoshi:test/");
        case DirectionRole: return QStringLiteral("Outbound");
        case ConnectionTypeRole: return QStringLiteral("Full relay");
        case NetworkRole: return QStringLiteral("IPv4");
        case TransportRole: return QStringLiteral("v2");
        case SentRole: return QStringLiteral("1 kB");
        case ReceivedRole: return QStringLiteral("2 kB");
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {NodeIdRole, "nodeId"},
            {AddressRole, "address"},
            {SubversionRole, "subversion"},
            {DirectionRole, "direction"},
            {ConnectionTypeRole, "connectionType"},
            {NetworkRole, "network"},
            {TransportRole, "transport"},
            {SentRole, "sent"},
            {ReceivedRole, "received"},
        };
    }

    Q_INVOKABLE void setPeerCountForTest(int count)
    {
        if (m_peer_count == count) return;
        beginResetModel();
        m_peer_count = std::max(0, count);
        endResetModel();
        Q_EMIT countChanged();
    }

    Q_INVOKABLE QObject* peerDetailsAt(int row) const
    {
        return row >= 0 && row < m_peer_count ? m_peer_details : nullptr;
    }

    void setPeerDetailsForTest(QObject* peer_details) { m_peer_details = peer_details; }

    Q_INVOKABLE int indexOfNodeId(qint64 node_id) const
    {
        Q_UNUSED(node_id);
        return -1;
    }

Q_SIGNALS:
    void sortByChanged(const QString& roleName);
    void sortAscendingChanged();
    void searchTextChanged();
    void directionFiltersChanged();
    void connectionTypeFiltersChanged();
    void networkFiltersChanged();
    void transportFiltersChanged();
    void countChanged();
    void dataChanged(int startIndex, int endIndex);

private:
    int m_peer_count{0};
    QObject* m_peer_details{nullptr};
    QString m_sort_by{QStringLiteral("nodeId")};
    bool m_sort_ascending{true};
    QString m_search_text;
    QStringList m_direction_filters;
    QStringList m_connection_type_filters;
    QStringList m_network_filters;
    QStringList m_transport_filters;
};

class MockBanListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool unbanResult MEMBER m_unban_result NOTIFY actionStateChanged)
    Q_PROPERTY(bool resetOnUnban MEMBER m_reset_on_unban NOTIFY actionStateChanged)
    Q_PROPERTY(int unbanCalls READ unbanCalls NOTIFY actionCallsChanged)
    Q_PROPERTY(int refreshCalls READ refreshCalls NOTIFY refreshCallsChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole,
        BanUntilRole
    };

    int count() const { return 1; }
    int unbanCalls() const { return m_unban_calls; }
    int refreshCalls() const { return m_refresh_calls; }

Q_SIGNALS:
    void unbanFinished(bool success);

public:

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return count();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
        switch (role) {
        case AddressRole:
            return QStringLiteral("127.0.0.1/32");
        case BanUntilRole:
            return QStringLiteral("January 1, 2030 12:00 AM");
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {BanUntilRole, "banUntil"},
        };
    }

    Q_INVOKABLE bool unbanAt(int row)
    {
        ++m_unban_calls;
        Q_EMIT actionCallsChanged();
        if (m_reset_on_unban) {
            beginResetModel();
            endResetModel();
        }
        return row >= 0 && row < count() && m_unban_result;
    }

    Q_INVOKABLE void refresh()
    {
        ++m_refresh_calls;
        Q_EMIT refreshCallsChanged();
    }

    Q_INVOKABLE void resetTestState()
    {
        m_unban_result = true;
        m_reset_on_unban = false;
        m_unban_calls = 0;
        m_refresh_calls = 0;
        Q_EMIT actionStateChanged();
        Q_EMIT actionCallsChanged();
        Q_EMIT refreshCallsChanged();
    }

Q_SIGNALS:
    void countChanged();
    void actionStateChanged();
    void actionCallsChanged();
    void refreshCallsChanged();

private:
    bool m_unban_result{true};
    bool m_reset_on_unban{false};
    int m_unban_calls{0};
    int m_refresh_calls{0};
};

class MockWalletListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int listWalletDirCalls READ listWalletDirCalls NOTIFY listWalletDirCallsChanged)
    Q_PROPERTY(bool walletDirLoaded READ walletDirLoaded WRITE setWalletDirLoaded NOTIFY walletDirLoadedChanged)

public:
    enum class LoadState {
        Closed = 0,
        Open = 1,
        Loading = 2,
        LoadError = 3,
    };
    Q_ENUM(LoadState)

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        DisplayNameRole,
        LoadStateRole,
        ErrorMessageRole,
        BalanceRole,
        KeySchemeKindRole,
        WalletSectionRole,
        BalanceSatoshiRole,
        BalanceKnownRole,
    };

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return m_wallet_names.size();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_wallet_names.size()) return {};
        if (role == Qt::DisplayRole || role == DisplayNameRole) return m_wallet_names.at(index.row());
        if (role == NameRole) return m_wallet_names.at(index.row());
        if (role == FormatRole) return QStringLiteral("sqlite");
        if (role == LoadStateRole) return m_wallet_load_states.at(index.row());
        if (role == ErrorMessageRole) return QString{};
        if (role == BalanceRole) return QString{};
        if (role == BalanceSatoshiRole) return qint64{0};
        if (role == BalanceKnownRole) return false;
        if (role == WalletSectionRole) return m_wallet_load_states.at(index.row()) == 1 ? QStringLiteral("open") : QStringLiteral("closed");
        if (role == KeySchemeKindRole) return 0;
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {NameRole, "name"},
            {FormatRole, "format"},
            {DisplayNameRole, "displayName"},
            {LoadStateRole, "loadState"},
            {ErrorMessageRole, "errorMessage"},
            {BalanceRole, "balance"},
            {KeySchemeKindRole, "keySchemeKind"},
            {WalletSectionRole, "walletSection"},
            {BalanceSatoshiRole, "balanceSatoshi"},
            {BalanceKnownRole, "balanceKnown"},
        };
    }

    int listWalletDirCalls() const { return m_list_wallet_dir_calls; }
    bool walletDirLoaded() const { return m_wallet_dir_loaded; }

    Q_INVOKABLE void listWalletDir()
    {
        ++m_list_wallet_dir_calls;
        Q_EMIT listWalletDirCallsChanged();
        setWalletDirLoaded(true);
    }
    Q_INVOKABLE void reset()
    {
        m_list_wallet_dir_calls = 0;
        m_wallet_dir_loaded = false;
        Q_EMIT listWalletDirCallsChanged();
        Q_EMIT walletDirLoadedChanged();
        setWalletLoadState(QStringLiteral("testwallet"), 1);
        setWalletLoadState(QStringLiteral("secondarywallet"), 0);
    }
    Q_INVOKABLE void setWalletDirLoaded(bool loaded)
    {
        if (m_wallet_dir_loaded == loaded) return;
        m_wallet_dir_loaded = loaded;
        Q_EMIT walletDirLoadedChanged();
    }
    Q_INVOKABLE void setWalletLoadState(const QString& name, int state)
    {
        const int row = m_wallet_names.indexOf(name);
        if (row < 0 || m_wallet_load_states.at(row) == state) return;
        m_wallet_load_states[row] = state;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {LoadStateRole, WalletSectionRole});
    }

Q_SIGNALS:
    void listWalletDirCallsChanged();
    void walletDirLoadedChanged();

private:
    int m_list_wallet_dir_calls{0};
    bool m_wallet_dir_loaded{false};
    QStringList m_wallet_names{QStringLiteral("testwallet"), QStringLiteral("secondarywallet")};
    QVector<int> m_wallet_load_states{1, 0};
};

class MockBumpTransactionModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(quint64 attemptId MEMBER m_attempt_id NOTIFY stateChanged)
    Q_PROPERTY(quint64 commitId MEMBER m_commit_id NOTIFY stateChanged)
    Q_PROPERTY(bool requireUnlock READ requireUnlock WRITE setRequireUnlock NOTIFY resultChanged)
    Q_PROPERTY(QString oldFee MEMBER m_old_fee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee MEMBER m_new_fee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease MEMBER m_fee_increase NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid MEMBER m_old_txid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid MEMBER m_new_txid NOTIFY resultChanged)
    Q_PROPERTY(QString errorText MEMBER m_error_text NOTIFY resultChanged)
    Q_PROPERTY(QString unlockErrorText MEMBER m_unlock_error_text NOTIFY resultChanged)
    Q_PROPERTY(bool needsUnlock MEMBER m_needs_unlock NOTIFY needsUnlockChanged)

public:
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed, Discarding, Uncertain, Closed };
    Q_ENUM(State)

    enum ActionType { SpeedUp };
    Q_ENUM(ActionType)

    int state() const { return m_state; }
    bool busy() const { return m_preparing || m_committing; }
    bool requireUnlock() const { return m_require_unlock; }

    void setState(int state)
    {
        if (m_state == state) return;
        m_state = state;
        Q_EMIT stateChanged();
    }

    void setRequireUnlock(bool require_unlock)
    {
        if (m_require_unlock == require_unlock) return;
        m_require_unlock = require_unlock;
        Q_EMIT resultChanged();
    }

    Q_INVOKABLE quint64 requestPrepareFeeBump(const QString& txid, unsigned int targetBlocks)
    {
        Q_UNUSED(targetBlocks);
        if (busy()) return 0;
        const auto attempt = ++m_attempt_id, revision = ++m_revision;
        m_preparing = true;
        m_old_txid = txid;
        m_new_txid.clear(); m_error_text.clear(); m_unlock_error_text.clear(); m_needs_unlock = false;
        m_old_fee = QStringLiteral("0.00000500 BTC");
        m_new_fee = QStringLiteral("0.00001000 BTC");
        m_fee_increase = QStringLiteral("0.00000500 BTC");
        setState(Preparing);
        Q_EMIT resultChanged();
        QTimer::singleShot(0, this, [this, attempt, revision] {
            if (attempt != m_attempt_id) return;
            m_preparing = false;
            setState(revision == m_revision ? NeedsConfirmation : Idle);
        });
        return attempt;
    }

    Q_INVOKABLE quint64 requestConfirmFeeBump()
    {
        if (busy() || m_state != NeedsConfirmation) return 0;
        const auto attempt = m_attempt_id, commit = ++m_commit_id;
        m_committing = true; m_needs_unlock = false; m_error_text.clear(); m_unlock_error_text.clear();
        setState(Committing);
        Q_EMIT needsUnlockChanged();
        QTimer::singleShot(0, this, [this, attempt, commit] {
            if (attempt != m_attempt_id || commit != m_commit_id) return;
            m_committing = false;
            if (m_require_unlock) {
                m_needs_unlock = true;
                setState(NeedsConfirmation);
            } else {
                m_new_txid = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
                setState(Succeeded);
            }
            Q_EMIT resultChanged();
            Q_EMIT needsUnlockChanged();
        });
        return commit;
    }

    Q_INVOKABLE quint64 requestConfirmFeeBumpWithPassphrase(const QString& passphrase)
    {
        Q_UNUSED(passphrase);
        if (busy() || m_state != NeedsConfirmation) return 0;
        m_require_unlock = false;
        return requestConfirmFeeBump();
    }

    Q_INVOKABLE void reset()
    {
        ++m_revision;
        if (m_committing) return;
        m_state = m_preparing ? Discarding : Idle;
        m_old_fee.clear();
        m_new_fee.clear();
        m_fee_increase.clear();
        m_old_txid.clear();
        m_new_txid.clear();
        m_error_text.clear();
        m_unlock_error_text.clear();
        m_needs_unlock = false;
        m_require_unlock = false;
        Q_EMIT stateChanged();
        Q_EMIT resultChanged();
        Q_EMIT needsUnlockChanged();
    }

Q_SIGNALS:
    void stateChanged();
    void actionTypeChanged();
    void resultChanged();
    void needsUnlockChanged();

private:
    int m_state{Idle};
    quint64 m_attempt_id{0}, m_commit_id{0}, m_revision{0};
    bool m_preparing{false}, m_committing{false};
    QString m_old_fee;
    QString m_new_fee;
    QString m_fee_increase;
    QString m_old_txid;
    QString m_new_txid;
    QString m_error_text;
    QString m_unlock_error_text;
    bool m_needs_unlock{false};
    bool m_require_unlock{false};
};

// Configurable presentation fixture. Transaction interpretation and filtering
// correctness are covered by the C++ model tests; this supplies their QML roles.
class MockActivityValueModel : public QAbstractListModel
{
public:
    explicit MockActivityValueModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_values.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}, {Qt::UserRole + 1, "content"}}; }
    QVariant data(const QModelIndex& index, int role) const override { if (!index.isValid() || index.row() >= m_values.size()) return {}; return role == Qt::UserRole ? m_values[index.row()] : role == Qt::UserRole + 1 ? m_values[index.row()].toMap().value("content") : QVariant{}; }
    void setValues(QVariantList values) { beginResetModel(); m_values = std::move(values); endResetModel(); }
private:
    QVariantList m_values;
};

class MockActivityDetailModel;
class MockTransactionActivityModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap selectedTransactionDetails READ selectedTransactionDetails NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QAbstractItemModel* detailInputs READ detailInputs CONSTANT)
    Q_PROPERTY(QAbstractItemModel* detailOutputs READ detailOutputs CONSTANT)
    Q_PROPERTY(QAbstractItemModel* detailActions READ detailActions CONSTANT)
    Q_PROPERTY(QAbstractItemModel* rawTransactionLines READ rawTransactionLines CONSTANT)
    Q_PROPERTY(QAbstractItemModel* entryDocument READ entryDocument CONSTANT)
    Q_PROPERTY(bool entryDetailsBusy READ entryDetailsBusy NOTIFY entryDetailsChanged)
    Q_PROPERTY(QString entryDetailsError READ entryDetailsError NOTIFY entryDetailsChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading MEMBER m_loading NOTIFY countChanged)
    Q_PROPERTY(QString loadError MEMBER m_load_error NOTIFY countChanged)
public:
    Q_INVOKABLE MockActivityDetailModel* createDetailView(QObject* owner);
    QVariantList rowsSnapshot() const { return m_rows; }
    bool activityLoading() const { return m_loading; }
    QString activityError() const { return m_load_error; }
    enum ActivityType { Send, Receive, Multiple, Consolidation, Split, InternalTransfer, Mined, Other };
    Q_ENUM(ActivityType)
    enum ActionDirection { SendAction, ReceiveAction, InternalAction };
    Q_ENUM(ActionDirection)
    enum ActionSource { Output, WalletInputs };
    Q_ENUM(ActionSource)

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QHash<int, QByteArray> roleNames() const override
    {
        QHash<int, QByteArray> roles;
        const QList<QByteArray> names{
            "activityId", "activityType", "txid", "requestId", "label", "address", "amount", "netAmountSat", "timestamp",
            "status", "depth", "blocksToMaturity", "statusKnown", "isInactive", "isPending", "isPendingRequest",
            "replacedByTxid", "hasPaymentRequest", "actions", "type", "paymentRequests", "canBump", "presentationLimited", "paymentRequestCount"};
        for (int i = 0; i < names.size(); ++i) roles.insert(Qt::UserRole + 500 + i, names[i]);
        return roles;
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        return index.isValid() && index.row() < m_rows.size() ? m_rows[index.row()].toMap().value(QString::fromUtf8(roleNames().value(role))) : QVariant{};
    }
    Q_INVOKABLE void setRows(const QVariantList& rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        Q_EMIT countChanged();
        requestTransactionDetails(m_requested_txid);
    }
    QAbstractItemModel* detailInputs() { return &m_inputs; }
    QAbstractItemModel* detailOutputs() { return &m_outputs; }
    QAbstractItemModel* detailActions() { return &m_actions; }
    QAbstractItemModel* rawTransactionLines() { return &m_raw; }
    QAbstractItemModel* entryDocument() { return &m_document; }
    bool entryDetailsBusy() const { return false; }
    QString entryDetailsError() const { return {}; }
    QVariantMap selectedTransactionDetails() const { auto value = transactionDetails(m_requested_txid, true); value["rawAvailable"] = !value.value("rawTransaction").toString().isEmpty(); value.remove("rawTransaction"); return value; }
    Q_INVOKABLE void reload() {}
    Q_INVOKABLE void requestTransactionDetails(const QString& txid) {
        m_requested_txid = txid;
        const auto value = transactionDetails(txid, true); const auto flow = value.value("flow").toMap();
        m_inputs.setValues(flow.value("inputs").toList()); m_outputs.setValues(flow.value("outputs").toList()); m_actions.setValues(value.value("actions").toList());
        QVariantList raw; const auto hex = value.value("rawTransaction").toString(); for (qsizetype offset = 0; offset < hex.size(); offset += 1024) raw.append(QVariantMap{{"content", hex.mid(offset, 1024)}}); m_raw.setValues(raw);
        Q_EMIT transactionDetailsChanged();
    }
    Q_INVOKABLE quint64 requestCopyRawTransaction(const QString& txid) {
        const auto text = rawTransaction(txid); if (text.isEmpty()) return 0;
        const auto ticket = ++m_copy_ticket;
        const QPointer<MockTransactionActivityModel> observer{this};
        QTimer::singleShot(0, QCoreApplication::instance(), [observer, ticket, text] { const bool copied = QGuiApplication::clipboard(); if (copied) QGuiApplication::clipboard()->setText(text); if(observer)Q_EMIT observer->copyFinished(ticket, copied, {}); }); return ticket;
    }
    Q_INVOKABLE quint64 requestEntryDetails(const QString& side, int row) {
        const auto details = transactionDetails(m_requested_txid, true); const auto flow = details.value("flow").toMap();
        const auto values = side == "actions" ? details.value("actions").toList() : flow.value(side).toList(); if (row < 0 || row >= values.size()) return 0;
        QVariantList lines; std::function<void(QString, QVariant)> flatten;
        flatten = [&](QString field, QVariant value) { if (value.metaType().id() == QMetaType::QVariantMap) { const auto map = value.toMap(); for (auto item = map.cbegin(); item != map.cend(); ++item) flatten(field.isEmpty() ? item.key() : field + "." + item.key(), item.value()); } else if (value.metaType().id() == QMetaType::QVariantList) { const auto list = value.toList(); for (int i = 0; i < list.size(); ++i) flatten(field + QStringLiteral("[%1]").arg(i), list[i]); } else { const auto text = value.toString(); for (qsizetype offset = 0; offset < std::max(qsizetype(1), text.size()); offset += 1024) lines.append(QVariantMap{{"field", field}, {"content", text.mid(offset, 1024)}}); } };
        flatten({}, values[row]); m_document.setValues(lines); Q_EMIT entryDetailsChanged(); return 1;
    }
    Q_INVOKABLE QString requestedTransaction() const { return m_requested_txid; }
    Q_INVOKABLE QString rawTransaction(const QString& txid) const { return transactionDetails(txid).value("rawTransaction").toString(); }
    Q_INVOKABLE QString paymentRequestUri(const QString& request_id) const
    {
        for (const auto& row : m_rows) if (row.toMap().value("requestId").toString() == request_id) return row.toMap().value("uri").toString();
        return {};
    }
    Q_INVOKABLE QVariantMap transactionDetails(const QString& txid, bool include_flow = false) const
    {
        Q_UNUSED(include_flow);
        for (const auto& row : m_rows) if (row.toMap().value("txid").toString() == txid) {
            auto value = row.toMap(); auto flow = value.value("flow").toMap(); if (!flow.isEmpty()) { flow["presentation"] = PrepareTransactionFlowPresentation(flow); value["flow"] = flow; } return value;
        }
        return {};
    }
Q_SIGNALS:
    void countChanged();
    void transactionDetailsChanged();
    void entryDetailsChanged();
    void copyFinished(quint64 ticket, bool copied, const QString& error);
private:
    MockActivityValueModel m_inputs{this}, m_outputs{this}, m_actions{this}, m_raw{this}, m_document{this};
    quint64 m_copy_ticket{0};
    QVariantList m_rows;
    bool m_loading{false};
    QString m_load_error;
    QString m_requested_txid;
};

class MockActivityDetailModel : public MockTransactionActivityModel
{
    Q_OBJECT
    Q_PROPERTY(QString transactionId READ transactionId WRITE setTransactionId NOTIFY transactionIdChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(MockActivityDetailModel::State state READ state NOTIFY transactionDetailsChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QString error READ error NOTIFY transactionDetailsChanged)
public:
    enum State { Inactive, Idle, Loading, Ready, Failed, Closed };
    Q_ENUM(State)
    State state() const {return !m_active?Inactive:m_txid.isEmpty()?Idle:Ready;}
    MockActivityDetailModel(MockTransactionActivityModel& source,QObject* owner) {
        setParent(owner); QQmlEngine::setObjectOwnership(this,QQmlEngine::CppOwnership);
        setRows(source.rowsSnapshot());
        connect(&source,&MockTransactionActivityModel::countChanged,this,[this,&source] {setRows(source.rowsSnapshot());});
    }
    QString transactionId() const {return m_txid;}
    bool active() const {return m_active;}
    bool pending() const {return false;}
    QString error() const {return {};}
    void setTransactionId(const QString& txid) {if(m_txid==txid)return;m_txid=txid;requestTransactionDetails(m_active?m_txid:QString{});Q_EMIT transactionIdChanged();}
    void setActive(bool value) {if(m_active==value)return;m_active=value;requestTransactionDetails(m_active?m_txid:QString{});Q_EMIT activeChanged();}
Q_SIGNALS:
    void transactionIdChanged();
    void activeChanged();
private:
    QString m_txid;
    bool m_active{false};
};
MockActivityDetailModel* MockTransactionActivityModel::createDetailView(QObject* owner)
{
    return owner?new MockActivityDetailModel{*this,owner}:nullptr;
}

class MockActivityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY sourceStateChanged)
    Q_PROPERTY(QString error READ error NOTIFY sourceStateChanged)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportChanged)
    Q_PROPERTY(QString exportError READ exportError NOTIFY exportChanged)
    Q_PROPERTY(QString exportedPath READ exportedPath NOTIFY exportChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(DateFilter dateFilter READ dateFilter WRITE setDateFilter NOTIFY dateFilterChanged)
    Q_PROPERTY(TypeFilter typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(QList<int> typeFilters READ typeFilters WRITE setTypeFilters NOTIFY typeFiltersChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(qint64 minAmount READ minAmount WRITE setMinAmount NOTIFY minAmountChanged)
    Q_PROPERTY(qint64 maxAmount READ maxAmount WRITE setMaxAmount NOTIFY maxAmountChanged)
    Q_PROPERTY(qint64 availableMaxAmount READ availableMaxAmount NOTIFY availableMaxAmountChanged)
    Q_PROPERTY(QDate rangeStart READ rangeStart NOTIFY rangeChanged)
    Q_PROPERTY(QDate rangeEnd READ rangeEnd NOTIFY rangeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY countChanged)
    Q_PROPERTY(int requestCount READ requestCount NOTIFY countChanged)
    Q_PROPERTY(qint64 pendingBalanceSat READ pendingBalanceSat NOTIFY pendingBalanceChanged)
    Q_PROPERTY(QString pendingBalance READ pendingBalance NOTIFY pendingBalanceChanged)
    Q_PROPERTY(GroupBy groupBy READ groupBy WRITE setGroupBy NOTIFY groupByChanged)

public:
    enum DateFilter {
        DateAll,
        Today,
        ThisWeek,
        ThisMonth,
        ThisYear,
        CustomRange
    };
    Q_ENUM(DateFilter)

    enum TypeFilter {
        TypeAll,
        Received,
        Sent,
        SentToSelf,
        Mined,
        Other,
        PaymentRequest,
        Multiple,
        Consolidation,
        Split
    };
    Q_ENUM(TypeFilter)

    enum GroupBy { Month, Day };
    Q_ENUM(GroupBy)
    enum SectionRole { SectionKeyRole = Qt::UserRole + 200, SectionLabelRole, DateTimeLabelRole };
    GroupBy groupBy() const { return m_group_by; }
    void setGroupBy(GroupBy group)
    {
        if (group == m_group_by) return;
        m_group_by = group;
        if (rowCount()) Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0));
        Q_EMIT groupByChanged();
    }
    bool grouped() const { return qobject_cast<MockTransactionActivityModel*>(sourceModel()); }
    QVariant namedData(const QModelIndex& index, const QByteArray& name) const
    {
        const auto roles = index.model()->roleNames();
        return index.data(roles.key(name, -1));
    }
    int requestCount() const
    {
        int count{0};
        for (int i = 0; i < rowCount(); ++i) count += namedData(mapToSource(index(i, 0)), "isPendingRequest").toBool();
        return count;
    }
    int transactionCount() const { return rowCount() - requestCount(); }
    qint64 pendingBalanceSat() const
    {
        qint64 total{0};
        if (!grouped()) return total;
        for (int i = 0; i < rowCount(); ++i) {
            const auto row = mapToSource(index(i, 0));
            if (namedData(row, "isPending").toBool() && !namedData(row, "isPendingRequest").toBool()) {
                total += namedData(row, "netAmountSat").toLongLong();
            }
        }
        return total;
    }
    QString pendingBalance() const
    {
        MockBitcoinAmount amount;
        amount.setUnit(static_cast<MockBitcoinAmount::Unit>(m_display_unit));
        amount.setSatoshi(pendingBalanceSat());
        return (pendingBalanceSat() > 0 ? QStringLiteral("+") : QString{}) + amount.displayWithUnit();
    }

    explicit MockActivityFilterProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        connect(this, &QAbstractItemModel::rowsInserted, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::rowsRemoved, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::modelReset, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::layoutChanged, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &MockActivityFilterProxyModel::countChanged, this, &MockActivityFilterProxyModel::pendingBalanceChanged);
        connect(this, &QAbstractItemModel::dataChanged, this, &MockActivityFilterProxyModel::pendingBalanceChanged);
        connect(this, &MockActivityFilterProxyModel::displayUnitChanged, this, &MockActivityFilterProxyModel::pendingBalanceChanged);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        auto roles = sourceModel() ? sourceModel()->roleNames() : QHash<int, QByteArray>{};
        if (grouped()) {
            roles.insert(SectionKeyRole, "sectionKey");
            roles.insert(SectionLabelRole, "sectionLabel");
            roles.insert(DateTimeLabelRole, "dateTimeLabel");
        }
        return roles;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (grouped() && (role == SectionKeyRole || role == SectionLabelRole || role == DateTimeLabelRole)) {
            const auto source = mapToSource(index);
            const bool pending = namedData(source, "isPending").toBool();
            const auto date = QDateTime::fromSecsSinceEpoch(namedData(source, "timestamp").toLongLong());
            if (role == DateTimeLabelRole) return date.toString(m_group_by == Day && !pending ? "h:mm AP" : "MMM d, h:mm AP");
            if (pending) return QStringLiteral("Pending");
            return date.toString(m_group_by == Day ? "MMMM d, yyyy" : "MMMM yyyy");
        }
        return QSortFilterProxyModel::data(index, role);
    }

    void setSourceModel(QAbstractItemModel* source_model) override
    {
        if (sourceModel() == source_model) return;
        for (const auto& connection : m_source_connections) disconnect(connection);
        m_source_connections.clear();
        QSortFilterProxyModel::setSourceModel(source_model);
        if (source_model) {
            m_source_connections << connect(source_model, &QAbstractItemModel::modelReset, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            m_source_connections << connect(source_model, &QAbstractItemModel::rowsInserted, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            m_source_connections << connect(source_model, &QAbstractItemModel::rowsRemoved, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            m_source_connections << connect(source_model, &QAbstractItemModel::dataChanged, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            if (auto* activity = qobject_cast<MockTransactionActivityModel*>(source_model)) {
                m_source_connections << connect(activity, &MockTransactionActivityModel::countChanged, this, &MockActivityFilterProxyModel::sourceStateChanged);
            }
            m_source_connections << connect(source_model, &QObject::destroyed, this, &MockActivityFilterProxyModel::sourceStateChanged);
        }
        Q_EMIT sourceStateChanged();
        Q_EMIT availableMaxAmountChanged();
        if (grouped()) sort(0, Qt::DescendingOrder);
        Q_EMIT countChanged();
    }

    QString searchText() const { return m_search_text; }
    void setSearchText(const QString& search_text)
    {
        if (m_search_text == search_text) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_search_text = search_text;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        Q_EMIT searchTextChanged();
        Q_EMIT countChanged();
    }

    DateFilter dateFilter() const { return m_date_filter; }
    void setDateFilter(DateFilter date_filter)
    {
        if (m_date_filter == date_filter) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_date_filter = date_filter;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        Q_EMIT dateFilterChanged();
        Q_EMIT countChanged();
    }

    TypeFilter typeFilter() const { return m_type_filters.isEmpty() ? TypeAll : static_cast<TypeFilter>(m_type_filters.first()); }
    QList<int> typeFilters() const { return m_type_filters; }
    void setTypeFilter(TypeFilter type) { setTypeFilters(type == TypeAll ? QList<int>{} : QList<int>{type}); }
    void setTypeFilters(const QList<int>& types)
    {
        if (m_type_filters == types) return;
        m_type_filters = types;
        invalidate();
        Q_EMIT typeFilterChanged();
        Q_EMIT typeFiltersChanged();
        Q_EMIT countChanged();
    }

    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int display_unit)
    {
        if (m_display_unit == display_unit) return;
        m_display_unit = display_unit;
        Q_EMIT displayUnitChanged();
    }

    qint64 minAmount() const { return m_min_amount; }
    qint64 maxAmount() const { return m_max_amount; }
    void setMinAmount(qint64 value) { setAmountRange(value, m_max_amount); }
    void setMaxAmount(qint64 value) { setAmountRange(m_min_amount, value); }
    Q_INVOKABLE bool setAmountRange(qint64 minimum, qint64 maximum)
    {
        if (minimum >= 0 && maximum >= 0 && minimum > maximum) return false;
        m_min_amount = minimum < 0 ? -1 : minimum;
        m_max_amount = maximum < 0 ? -1 : maximum;
        invalidate();
        Q_EMIT minAmountChanged();
        Q_EMIT maxAmountChanged();
        Q_EMIT countChanged();
        return true;
    }
    qint64 availableMaxAmount() const
    {
        qint64 result{0};
        if (sourceModel()) for (int i = 0; i < sourceModel()->rowCount(); ++i) {
            const auto row = sourceModel()->index(i, 0);
            if (!namedData(row, "isPendingRequest").toBool()) result = qMax(result, qAbs(namedData(row, "netAmountSat").toLongLong()));
        }
        return result;
    }

    QDate rangeStart() const { return m_range_start; }
    QDate rangeEnd() const { return m_range_end; }

    Q_INVOKABLE bool applyCustomRange(const QString& start_iso, const QString& end_iso)
    {
        const QDate start = QDate::fromString(start_iso, Qt::ISODate);
        const QDate end = QDate::fromString(end_iso, Qt::ISODate);
        if (!start.isValid() || !end.isValid() || start > end) return false;

        const bool range_changed = start != m_range_start || end != m_range_end;
        const bool filter_changed = m_date_filter != CustomRange;
        if (!range_changed && !filter_changed) return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_range_start = start;
        m_range_end = end;
        m_date_filter = CustomRange;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif

        if (range_changed) Q_EMIT rangeChanged();
        if (filter_changed) Q_EMIT dateFilterChanged();
        Q_EMIT countChanged();
        return true;
    }

    int count() const { return rowCount(); }

    bool busy() const {
        const auto* source = qobject_cast<MockTransactionActivityModel*>(sourceModel());
        return source && source->activityLoading();
    }
    QString error() const {
        const auto* source = qobject_cast<MockTransactionActivityModel*>(sourceModel());
        return source ? source->activityError() : QString{};
    }
    bool exportBusy() const { return m_export_busy; }
    QString exportError() const { return {}; }
    QString exportedPath() const { return m_export_path; }
    Q_INVOKABLE quint64 requestExportCsv(const QString& path) {
        if (m_export_busy || path.isEmpty()) return 0; m_export_busy = true; m_export_path.clear(); const auto ticket = ++m_ticket; Q_EMIT exportChanged();
        QTimer::singleShot(0, this, [this, path, ticket] { m_export_busy = false; m_export_path = path; Q_EMIT exportChanged(); Q_EMIT exportFinished(ticket, true, path, {}); }); return ticket;
    }
    Q_INVOKABLE quint64 captureRowContext(int row) { if (row < 0 || row >= rowCount()) return 0; m_context_txid = namedData(mapToSource(index(row, 0)), "txid").toString(); return ++m_context; }
    Q_INVOKABLE void releaseRowContext(quint64 token) { if (token == m_context) { m_context_txid.clear(); ++m_context; } }
    Q_INVOKABLE quint64 requestCopyRawTransaction(quint64 context) { auto* model = qobject_cast<MockTransactionActivityModel*>(sourceModel()); return model && context == m_context ? model->requestCopyRawTransaction(m_context_txid) : 0; }

    bool matchesTypes(const QModelIndex& row) const
    {
        using Model = MockTransactionActivityModel;
        if (m_type_filters.isEmpty()) return true;
        const bool request = namedData(row, "isPendingRequest").toBool();
        const int kind = namedData(row, "activityType").toInt();
        for (int type : m_type_filters) {
            if (type == PaymentRequest && (request || namedData(row, "hasPaymentRequest").toBool())) return true;
            if (request) continue;
            if ((type == Multiple && kind == Model::Multiple) || (type == Consolidation && kind == Model::Consolidation)
                || (type == Split && kind == Model::Split) || (type == Mined && kind == Model::Mined)
                || (type == Other && kind == Model::Other) || (type == SentToSelf && (kind == Model::InternalTransfer || kind == Model::Consolidation || kind == Model::Split))) return true;
            if (type == Sent || type == Received) {
                if ((type == Sent && kind == Model::Send) || (type == Received && kind == Model::Receive)) return true;
                for (const auto& action : namedData(row, "actions").toList()) {
                    if (action.toMap().value("direction").toInt() == (type == Sent ? Model::SendAction : Model::ReceiveAction)) return true;
                }
            }
        }
        return false;
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override
    {
        if (grouped()) {
            const auto row = sourceModel()->index(source_row, 0, source_parent);
            QString search = namedData(row, "label").toString() + namedData(row, "address").toString() + namedData(row, "txid").toString();
            for (const auto& action : namedData(row, "actions").toList()) search += action.toMap().value("label").toString() + action.toMap().value("address").toString();
            return search.contains(m_search_text.trimmed(), Qt::CaseInsensitive)
                && (m_min_amount < 0 || qAbs(namedData(row, "netAmountSat").toLongLong()) >= m_min_amount)
                && (m_max_amount < 0 || qAbs(namedData(row, "netAmountSat").toLongLong()) <= m_max_amount)
                && matchesTypes(row) && m_date_filter == DateAll;
        }
        Q_UNUSED(source_row);
        Q_UNUSED(source_parent);
        return m_search_text.trimmed().isEmpty() &&
            m_date_filter == DateAll &&
            m_type_filters.isEmpty();
    }

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        if (!grouped()) return QSortFilterProxyModel::lessThan(left, right);
        const bool a = namedData(left, "isPending").toBool(), b = namedData(right, "isPending").toBool();
        if (a != b) return !a;
        return namedData(left, "timestamp").toLongLong() < namedData(right, "timestamp").toLongLong();
    }

Q_SIGNALS:
    void exportChanged();
    void exportFinished(quint64 ticket, bool saved, const QString& path, const QString& error);
    void sourceStateChanged();
    void searchTextChanged();
    void dateFilterChanged();
    void typeFilterChanged();
    void typeFiltersChanged();
    void displayUnitChanged();
    void minAmountChanged();
    void maxAmountChanged();
    void availableMaxAmountChanged();
    void rangeChanged();
    void countChanged();

    void groupByChanged();
    void pendingBalanceChanged();

private:
    bool m_export_busy{false};
    quint64 m_ticket{0}, m_context{0};
    QString m_export_path, m_context_txid;
    QString m_search_text;
    GroupBy m_group_by{Month};
    DateFilter m_date_filter{DateAll};
    QList<int> m_type_filters;
    QList<QMetaObject::Connection> m_source_connections;
    int m_display_unit{0};
    qint64 m_min_amount{-1};
    qint64 m_max_amount{-1};
    QDate m_range_start;
    QDate m_range_end;
};

class MockDesktopWindowBehaviorModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool desktopPlatform READ desktopPlatform CONSTANT)
    Q_PROPERTY(bool settingsReady READ settingsReady CONSTANT)
    Q_PROPERTY(bool persistencePending READ persistencePending CONSTANT)
    Q_PROPERTY(QString persistenceError READ persistenceError CONSTANT)
    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged)
    Q_PROPERTY(bool minimizeOnClose READ minimizeOnClose WRITE setMinimizeOnClose NOTIFY minimizeOnCloseChanged)

public:
    bool settingsReady() const { return true; }
    bool persistencePending() const { return false; }
    QString persistenceError() const { return {}; }
    Q_INVOKABLE void retryPersistence() {}
    bool desktopPlatform() const { return m_desktop_platform; }

    bool showTrayIcon() const { return m_show_tray_icon; }
    void setShowTrayIcon(bool show) {
        if (m_show_tray_icon == show) return;
        m_show_tray_icon = show;
        if (!show && m_minimize_to_tray) {
            setMinimizeToTray(false);
        }
        Q_EMIT showTrayIconChanged(show);
    }

    bool minimizeToTray() const { return m_minimize_to_tray; }
    void setMinimizeToTray(bool minimize) {
        if (!m_show_tray_icon && minimize) return;
        if (m_minimize_to_tray == minimize) return;
        m_minimize_to_tray = minimize;
        Q_EMIT minimizeToTrayChanged(minimize);
    }

    bool minimizeOnClose() const { return m_minimize_on_close; }
    void setMinimizeOnClose(bool minimize) {
        if (m_minimize_on_close == minimize) return;
        m_minimize_on_close = minimize;
        Q_EMIT minimizeOnCloseChanged(minimize);
    }

    Q_INVOKABLE bool shouldHideToTrayOnMinimize() const {
        return m_desktop_platform && m_show_tray_icon && m_minimize_to_tray;
    }
    Q_INVOKABLE bool shouldMinimizeWindowOnClose() const {
        return m_desktop_platform && m_minimize_on_close;
    }

Q_SIGNALS:
    void showTrayIconChanged(bool show);
    void minimizeToTrayChanged(bool minimize);
    void minimizeOnCloseChanged(bool minimize);

private:
    bool m_desktop_platform{true};
    bool m_show_tray_icon{true};
    bool m_minimize_to_tray{false};
    bool m_minimize_on_close{false};
};

class MockDebugLogModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool hasMoreLines READ hasMoreLines NOTIFY hasMoreLinesChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool warningsAndErrorsOnly READ warningsAndErrorsOnly WRITE setWarningsAndErrorsOnly NOTIFY warningsAndErrorsOnlyChanged)
    Q_PROPERTY(QString openError READ openError NOTIFY openErrorChanged)
    Q_PROPERTY(bool logAvailable READ logAvailable NOTIFY logAvailableChanged)
    Q_PROPERTY(bool fileLoggingEnabled READ fileLoggingEnabled NOTIFY fileLoggingEnabledChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int loadMoreCalls READ loadMoreCalls NOTIFY loadMoreCallsChanged)
    Q_PROPERTY(int openLogFileCalls READ openLogFileCalls NOTIFY openLogFileCallsChanged)

public:
    enum Role {
        MessageRole = Qt::UserRole + 1,
        TimestampRole,
        IsErrorRole,
        IsWarningRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int count() const { return m_rows.size(); }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
        const Row& row = m_rows.at(index.row());
        switch (role) {
        case MessageRole: return row.message;
        case TimestampRole: return row.timestamp;
        case IsErrorRole: return row.is_error;
        case IsWarningRole: return row.is_warning;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {MessageRole, "message"},
            {TimestampRole, "timestamp"},
            {IsErrorRole, "isError"},
            {IsWarningRole, "isWarning"},
        };
    }

    bool active() const { return m_active; }
    void setActive(bool active)
    {
        if (m_active == active) return;
        m_active = active;
        Q_EMIT activeChanged();
    }

    bool hasMoreLines() const { return m_has_more_lines; }
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter)
    {
        if (m_filter == filter) return;
        m_filter = filter;
        Q_EMIT filterChanged();
    }
    QString openError() const { return m_open_error; }
    bool logAvailable() const { return m_log_available; }
    Q_INVOKABLE void setLogAvailable(bool available)
    {
        if (m_log_available == available) return;
        m_log_available = available;
        Q_EMIT logAvailableChanged();
    }
    bool fileLoggingEnabled() const { return m_file_logging_enabled; }
    Q_INVOKABLE void setFileLoggingEnabled(bool enabled)
    {
        if (m_file_logging_enabled == enabled) return;
        m_file_logging_enabled = enabled;
        Q_EMIT fileLoggingEnabledChanged();
    }
    bool warningsAndErrorsOnly() const { return m_warnings_and_errors_only; }
    void setWarningsAndErrorsOnly(bool warnings_and_errors_only)
    {
        if (m_warnings_and_errors_only == warnings_and_errors_only) return;
        m_warnings_and_errors_only = warnings_and_errors_only;
        Q_EMIT warningsAndErrorsOnlyChanged();
    }
    int loadMoreCalls() const { return m_load_more_calls; }
    int openLogFileCalls() const { return m_open_log_file_calls; }

    Q_INVOKABLE void refresh(bool = false) {}
    Q_INVOKABLE void loadMore()
    {
        ++m_load_more_calls;
        Q_EMIT loadMoreCallsChanged();
        prependRowsForTest(20);
        setHasMoreLinesForTest(false);
    }
    Q_INVOKABLE bool openLogFile()
    {
        ++m_open_log_file_calls;
        Q_EMIT openLogFileCallsChanged();
        const QString next = m_open_result ? QString{} : m_pending_error;
        if (m_open_error != next) {
            m_open_error = next;
            Q_EMIT openErrorChanged();
        }
        return m_open_result;
    }
    Q_INVOKABLE void setOpenLogFileResult(bool ok, const QString& error)
    {
        m_open_result = ok;
        m_pending_error = error;
    }
    Q_INVOKABLE void clearOpenError()
    {
        if (m_open_error.isEmpty()) return;
        m_open_error.clear();
        Q_EMIT openErrorChanged();
    }
    Q_INVOKABLE void reset()
    {
        m_open_result = true;
        m_pending_error.clear();
        setLogAvailable(true);
        setFileLoggingEnabled(true);
        if (!m_open_error.isEmpty()) {
            m_open_error.clear();
            Q_EMIT openErrorChanged();
        }
    }

    Q_INVOKABLE void resetForTest(int count, bool has_more_lines)
    {
        m_filter.clear();
        m_warnings_and_errors_only = false;
        beginResetModel();
        m_rows.clear();
        m_rows.reserve(count);
        for (int i = 0; i < count; ++i) {
            m_rows.append(makeRow(QStringLiteral("entry-%1").arg(i)));
        }
        endResetModel();
        m_next_new_row = 0;
        m_next_old_row = count;
        m_load_more_calls = 0;
        m_open_log_file_calls = 0;
        Q_EMIT countChanged();
        Q_EMIT filterChanged();
        Q_EMIT warningsAndErrorsOnlyChanged();
        Q_EMIT loadMoreCallsChanged();
        Q_EMIT openLogFileCallsChanged();
        setHasMoreLinesForTest(has_more_lines);
    }

    Q_INVOKABLE void prependRowsForTest(int count)
    {
        if (count <= 0) return;

        QList<Row> added;
        added.reserve(count);
        for (int i = 0; i < count; ++i) {
            added.append(makeRow(QStringLiteral("old-%1").arg(m_next_old_row++)));
        }

        beginInsertRows(QModelIndex(), 0, count - 1);
        for (int i = count - 1; i >= 0; --i) {
            m_rows.prepend(std::move(added[i]));
        }
        endInsertRows();
        Q_EMIT countChanged();
        Q_EMIT newLinesAdded(count);
    }

    Q_INVOKABLE void appendRowsForTest(int count)
    {
        if (count <= 0) return;

        const int first = m_rows.size();
        beginInsertRows(QModelIndex(), first, first + count - 1);
        for (int i = 0; i < count; ++i) {
            m_rows.append(makeRow(QStringLiteral("new-%1").arg(m_next_new_row++)));
        }
        endInsertRows();
        Q_EMIT countChanged();
    }

    Q_INVOKABLE void removeRowsFromEndForTest(int count)
    {
        count = std::min<int>(count, m_rows.size());
        if (count <= 0) return;

        const int first = m_rows.size() - count;
        beginRemoveRows(QModelIndex(), first, m_rows.size() - 1);
        m_rows.remove(first, count);
        endRemoveRows();
        Q_EMIT countChanged();
    }

    Q_INVOKABLE void prependAndPruneRowsForTest(int prepend_count, int prune_count)
    {
        prependRowsForTest(prepend_count);
        removeRowsFromEndForTest(prune_count);
    }

    Q_INVOKABLE void prependAndAppendRowsForTest(int prepend_count,
                                                 int append_count,
                                                 bool prepend_first)
    {
        if (prepend_first) {
            prependRowsForTest(prepend_count);
            appendRowsForTest(append_count);
        } else {
            appendRowsForTest(append_count);
            prependRowsForTest(prepend_count);
        }
    }

    Q_INVOKABLE void setMessageForTest(int row, const QString& message)
    {
        if (row < 0 || row >= m_rows.size() || m_rows.at(row).message == message) return;
        m_rows[row].message = message;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {MessageRole});
    }

    Q_INVOKABLE void setStructuredFieldsForTest(int row,
                                                bool is_error,
                                                const QString& timestamp)
    {
        if (row < 0 || row >= m_rows.size()) return;
        Row& item = m_rows[row];
        item.is_error = is_error;
        item.is_warning = false;
        item.timestamp = timestamp;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index,
                           {IsErrorRole, IsWarningRole, TimestampRole});
    }

    Q_INVOKABLE void setWarningForTest(int row, bool is_warning)
    {
        if (row < 0 || row >= m_rows.size()) return;
        Row& item = m_rows[row];
        item.is_warning = is_warning;
        if (is_warning) item.is_error = false;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {IsErrorRole, IsWarningRole});
    }

    Q_INVOKABLE QString messageAt(int row) const
    {
        if (row < 0 || row >= m_rows.size()) return {};
        return m_rows.at(row).message;
    }

    Q_INVOKABLE void setHasMoreLinesForTest(bool has_more_lines)
    {
        if (m_has_more_lines == has_more_lines) return;
        m_has_more_lines = has_more_lines;
        Q_EMIT hasMoreLinesChanged();
    }

Q_SIGNALS:
    void activeChanged();
    void hasMoreLinesChanged();
    void filterChanged();
    void warningsAndErrorsOnlyChanged();
    void openErrorChanged();
    void logAvailableChanged();
    void fileLoggingEnabledChanged();
    void newLinesAdded(int count);
    void countChanged();
    void loadMoreCallsChanged();
    void openLogFileCallsChanged();

private:
    struct Row {
        QString message;
        QString timestamp;
        bool is_error{false};
        bool is_warning{false};
    };

    static Row makeRow(const QString& message)
    {
        return Row{
            message,
            QStringLiteral("15:42:08"),
            false,
            false,
        };
    }

    bool m_active{false};
    bool m_has_more_lines{false};
    QString m_filter;
    bool m_warnings_and_errors_only{false};
    QList<Row> m_rows;
    int m_load_more_calls{0};
    int m_open_log_file_calls{0};
    int m_next_new_row{0};
    int m_next_old_row{0};
    QString m_open_error;
    QString m_pending_error;
    bool m_open_result{true};
    bool m_log_available{true};
    bool m_file_logging_enabled{true};
};

class MockDesktopTrayIconController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool visible MEMBER m_visible NOTIFY visibleChanged)
    Q_PROPERTY(bool windowVisible MEMBER m_window_visible NOTIFY windowVisibleChanged)

public:
    bool supported() const { return false; }
    Q_INVOKABLE void showMainWindow() { m_window_visible = true; Q_EMIT windowVisibleChanged(); }
    Q_INVOKABLE void hideMainWindow() { m_window_visible = false; Q_EMIT windowVisibleChanged(); }

Q_SIGNALS:
    void visibleChanged();
    void windowVisibleChanged();
    void showRequested();
    void hideRequested();
    void quitRequested();

private:
    bool m_visible{false};
    bool m_window_visible{true};
};

class MockRpcOutputModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount CONSTANT)

public:
    int rowCount(const QModelIndex& = {}) const override { return 0; }
    QVariant data(const QModelIndex&, int) const override { return {}; }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "timestamp"}, {Qt::UserRole + 2, "content"}, {Qt::UserRole + 3, "category"}};
    }
};

class MockRpcConsoleModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString completionPrefix MEMBER m_completion_prefix)
    Q_PROPERTY(QStringList completionSuggestions READ completionSuggestions CONSTANT)
    Q_PROPERTY(bool completionBusy READ completionBusy CONSTANT)
    Q_PROPERTY(QString completionError READ completionError CONSTANT)
    Q_PROPERTY(bool executing READ executing CONSTANT)
    Q_PROPERTY(QStringList availableCommands READ availableCommands CONSTANT)
    Q_PROPERTY(QObject* outputModel READ outputModel CONSTANT)
    Q_PROPERTY(QColor requestColor MEMBER m_request_color)
    Q_PROPERTY(QColor replyColor MEMBER m_reply_color)
    Q_PROPERTY(QColor errorColor MEMBER m_error_color)
    Q_PROPERTY(QColor keyColor MEMBER m_key_color)

public:
    bool executing() const { return false; }
    QStringList availableCommands() const { return {}; }
    QStringList completionSuggestions() const { return {}; }
    bool completionBusy() const { return false; }
    QString completionError() const { return {}; }
    QObject* outputModel() { return &m_output; }
    Q_INVOKABLE bool submitCommand(const QString&, const QString&) { return false; }
    Q_INVOKABLE QString browseHistory(int, const QString& current) { return current; }
    Q_INVOKABLE void resetHistoryNavigation() {}
    Q_INVOKABLE void clear() {}

private:
    MockRpcOutputModel m_output;
    QString m_completion_prefix;
    QColor m_request_color;
    QColor m_reply_color;
    QColor m_error_color;
    QColor m_key_color;
};

class TestIconProvider : public QQuickImageProvider
{
public:
    TestIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested_size) override
    {
        const QPixmap pixmap = QIcon(QStringLiteral(":/icons/") +
            (id == QStringLiteral("app") ? QStringLiteral("bitcoin") : id)).pixmap(requested_size);
        if (size) *size = pixmap.size();
        return pixmap;
    }
};

//! Stands in for the real UrlOpener so the popup's success and failure paths
//! can be driven without launching a browser. Mirrors the production scheme
//! allowlist so a test cannot pass on a URL the real opener would reject.
class MockUrlOpener : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE bool openUrl(const QString& url)
    {
        m_last_url = url;
        const QUrl parsed(url, QUrl::StrictMode);
        if (!parsed.isValid() || parsed.host().isEmpty()) return false;
        const QString scheme = parsed.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;
        return m_open_result;
    }
    Q_INVOKABLE void setOpenResult(bool ok) { m_open_result = ok; }
    Q_INVOKABLE QString lastUrl() const { return m_last_url; }
    Q_INVOKABLE void reset()
    {
        m_open_result = true;
        m_last_url.clear();
    }

private:
    bool m_open_result{true};
    QString m_last_url;
};

class MockApplicationModels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MockNodeModel* nodeModel READ nodeModel CONSTANT)
    Q_PROPERTY(MockChainModel* chainModel READ chainModel CONSTANT)
    Q_PROPERTY(MockBlockClockModel* blockClockModel READ blockClockModel CONSTANT)
    Q_PROPERTY(MockOptionsModel* optionsModel READ optionsModel CONSTANT)
    Q_PROPERTY(MockNetworkTrafficTower* networkTrafficTower READ networkTrafficTower CONSTANT)
    Q_PROPERTY(MockNetworkStatusModel* networkStatusModel READ networkStatusModel CONSTANT)
    Q_PROPERTY(MockPeerTableModel* peerTableModel READ peerTableModel CONSTANT)
    Q_PROPERTY(MockPeerListModelProxy* peerListModelProxy READ peerListModelProxy CONSTANT)
    Q_PROPERTY(MockBanListModel* banListModel READ banListModel CONSTANT)
    Q_PROPERTY(MockDebugLogModel* debugLogModel READ debugLogModel CONSTANT)
    Q_PROPERTY(QObject* rpcConsoleModel READ rpcConsoleModel CONSTANT)
    Q_PROPERTY(MockWalletController* walletController READ walletController CONSTANT)
    Q_PROPERTY(MockWalletListModel* walletListModel READ walletListModel CONSTANT)
    Q_PROPERTY(MockDesktopWindowBehaviorModel* desktopWindowBehaviorModel READ desktopWindowBehaviorModel CONSTANT)
    Q_PROPERTY(QObject* desktopTrayIconController READ desktopTrayIconController CONSTANT)
    Q_PROPERTY(MockOptionsModel* onboardingOptionsModel READ onboardingOptionsModel CONSTANT)
    Q_PROPERTY(bool testAutomationEnabled READ testAutomationEnabled CONSTANT)
    Q_PROPERTY(QString startupError READ startupError CONSTANT)
    Q_PROPERTY(QAbstractItemModel* startupErrorDetails READ startupErrorDetails CONSTANT)
    Q_PROPERTY(bool startupErrorPending READ startupErrorPending CONSTANT)

public:
    QAbstractItemModel* startupErrorDetails() const { return nullptr; }
    bool startupErrorPending() const { return false; }

    explicit MockApplicationModels(QQmlEngine* engine) : QObject(engine), m_context(engine->rootContext()) {}

    MockNodeModel* nodeModel() const { return m_context->contextProperty(QStringLiteral("nodeModel")).value<MockNodeModel*>(); }
    MockChainModel* chainModel() const { return m_context->contextProperty(QStringLiteral("chainModel")).value<MockChainModel*>(); }
    MockBlockClockModel* blockClockModel() const { return m_context->contextProperty(QStringLiteral("blockClockModel")).value<MockBlockClockModel*>(); }
    MockOptionsModel* optionsModel() const { return m_context->contextProperty(QStringLiteral("optionsModel")).value<MockOptionsModel*>(); }
    MockNetworkTrafficTower* networkTrafficTower() const { return m_context->contextProperty(QStringLiteral("networkTrafficTower")).value<MockNetworkTrafficTower*>(); }
    MockNetworkStatusModel* networkStatusModel() const { return m_context->contextProperty(QStringLiteral("networkStatusModel")).value<MockNetworkStatusModel*>(); }
    MockPeerTableModel* peerTableModel() const { return m_context->contextProperty(QStringLiteral("peerTableModel")).value<MockPeerTableModel*>(); }
    MockPeerListModelProxy* peerListModelProxy() const { return m_context->contextProperty(QStringLiteral("peerListModelProxy")).value<MockPeerListModelProxy*>(); }
    MockBanListModel* banListModel() const { return m_context->contextProperty(QStringLiteral("banListModel")).value<MockBanListModel*>(); }
    MockDebugLogModel* debugLogModel() const { return m_context->contextProperty(QStringLiteral("debugLogModel")).value<MockDebugLogModel*>(); }
    QObject* rpcConsoleModel() const { return m_context->contextProperty(QStringLiteral("rpcConsoleModel")).value<QObject*>(); }
    MockWalletController* walletController() const { return m_context->contextProperty(QStringLiteral("walletController")).value<MockWalletController*>(); }
    MockWalletListModel* walletListModel() const { return m_context->contextProperty(QStringLiteral("walletListModel")).value<MockWalletListModel*>(); }
    MockDesktopWindowBehaviorModel* desktopWindowBehaviorModel() const { return m_context->contextProperty(QStringLiteral("desktopWindowBehaviorModel")).value<MockDesktopWindowBehaviorModel*>(); }
    QObject* desktopTrayIconController() const { return m_context->contextProperty(QStringLiteral("desktopTrayIconController")).value<QObject*>(); }
    MockOptionsModel* onboardingOptionsModel() const { return m_context->contextProperty(QStringLiteral("optionsModel")).value<MockOptionsModel*>(); }
    bool testAutomationEnabled() const { return m_context->contextProperty(QStringLiteral("testAutomationEnabled")).toBool(); }
    QString startupError() const { return m_context->contextProperty(QStringLiteral("message")).toString(); }

private:
    QQmlContext* m_context;
};

class QmlTestsSetup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supportsApplicationFonts READ supportsApplicationFonts CONSTANT)

public:
    bool hadWarnings() const { return m_had_warnings; }
    // The minimal platform cannot register application fonts. FontLoader
    // fixtures use the platform's fallback fonts there.
    bool supportsApplicationFonts() const { return QGuiApplication::platformName() != QStringLiteral("minimal"); }

public Q_SLOTS:
    int namedChildCount(QObject* object, const QString& name) const { return object ? object->findChildren<QObject*>(name).size() : 0; }
    QString entryFieldText(QObject* object, const QString& field) const {
        const auto* model = qobject_cast<QAbstractItemModel*>(object); QString text;
        if (model) for (int row = 0; row < model->rowCount(); ++row) {
            const auto entry = model->data(model->index(row, 0), Qt::UserRole).toMap();
            if (entry.value("field").toString() == field) text += entry.value("content").toString();
        }
        return text;
    }
    QString nativeClipboardText() const { return QGuiApplication::clipboard()->text(); }
    QVariantMap prepareTransactionFlow(const QVariantMap& input) {
        auto flow = input; flow["presentation"] = PrepareTransactionFlowPresentation(flow); return flow;
    }
    bool replaceText(QObject* object, const QString& text)
    {
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || !item->window()) return false;
        item->forceActiveFocus();
        if (!QMetaObject::invokeMethod(item, "selectAll")) return false;
        QTest::keyClick(item->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
        QInputMethodEvent edit;
        edit.setCommitString(text);
        QCoreApplication::sendEvent(item, &edit);
        return edit.isAccepted();
    }

    void failOnWarnings()
    {
        // QTest::failOnWarning() was added in Qt 6.3. Keep this a no-op on Qt 6.2.
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        // The minimal plugin cannot register application fonts or raise windows.
        // Fatal error tests call raise() to bring the error window forward.
        // Allow only the two exact plugin messages. Other Qt warnings still fail
        // this test, and qmlEngineAvailable() rejects all QML engine warnings.
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!This plugin does not support (?:application fonts|raise\\(\\))$).*")));
#endif
    }

    void applicationAvailable()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BitcoinCoreQmlTests"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("bitcoincore.org"));
        QCoreApplication::setApplicationName(QStringLiteral("bitcoinqml_qmltests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settings.path());
        m_qr_executor = std::make_unique<BackendExecutor>(execution::Limits::Bounded());
        BlockClockDial::setDefaultServices({m_qr_executor->scheduler(),
            [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); }});
        BitcoinAddress::setDefaultServices({m_qr_executor->scheduler(),
            [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); }});
        MonospaceOutputModel::setDefaultServices({m_qr_executor->scheduler(),
            [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); },
            [this](std::function<void()> work) { return m_qr_executor->submit(this, std::move(work), [] {}); }, {}});
        qmlRegisterType<MonospaceOutputModel>("org.bitcoincore.qt", 1, 0, "MonospaceOutputModel");
        // Exercise the same customizable controls used by the application.
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    }

    void processDeferredDeletes()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    Q_INVOKABLE QVariant blockClockSnapshot(const QList<qreal>& fractions) const
    {
        auto frame = std::make_shared<blockclock::Projection>(); frame->fractions = fractions;
        m_block_clock_fixture = std::move(frame);
        return QVariant::fromValue(blockclock::WeakSnapshot{m_block_clock_fixture});
    }

    int incubatingObjectCount(QObject* object) const
    {
        const auto* engine = qmlEngine(object);
        const auto* controller = engine ? engine->incubationController() : nullptr;
        return controller ? controller->incubatingObjectCount() : 0;
    }

    void cleanupTestCase()
    {
        for(const auto& store:m_preferences_stores)if(store)store->closeAdmission();
        BitcoinAddress::setDefaultServices({});
        QEventLoop loop;
        bool retired{false};
        MonospaceOutputModel::closeAll();
        MonospaceOutputModel::whenRetired(&loop, [&] { retired = true; loop.quit(); });
        while (!retired) loop.exec();
        MonospaceOutputModel::setDefaultServices({});
        retired = false;
        BlockClockDial::closeAll();
        BlockClockDial::whenRetired(&loop, [&] { retired = true; loop.quit(); });
        while (!retired) loop.exec();
        BlockClockDial::setDefaultServices({});
        bool drained{false};
        BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
        while (!drained) loop.exec();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        // Match failOnWarnings(): Qt 6.2 skips the new component warning policy.
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        // Qt Test's per-function warning policy does not cover QML loading or
        // engine destruction. Observe both phases so these warnings also fail.
        connect(engine, &QQmlEngine::warnings, engine, [this](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) {
                m_had_warnings = true;
                std::cerr << "Unexpected QML warning: " << warning.toString().toStdString() << '\n';
            }
        });
#endif
        auto* preferences=new UiPreferencesStore{{m_qr_executor->scheduler(),[this](auto turn){QMetaObject::invokeMethod(this,std::move(turn),Qt::QueuedConnection);}},UiPreferencesNamespace::Capture(),engine};
        m_preferences_stores.emplace_back(preferences);
        engine->rootContext()->setContextProperty(QStringLiteral("uiPreferencesStore"),preferences);
        preferences->start();
        QEventLoop preferences_loop;
        connect(preferences,&UiPreferencesStore::stateChanged,&preferences_loop,[&]{if(preferences->ready()||!preferences->error().isEmpty())preferences_loop.quit();});
        while(!preferences->ready()&&preferences->error().isEmpty())preferences_loop.exec();
        engine->rootContext()->setContextProperty(QStringLiteral("testSetup"), this);
        engine->addImageProvider(QStringLiteral("images"), new TestIconProvider);
        engine->addImageProvider(QStringLiteral("qr"), new QRImageProvider{m_qr_executor->scheduler(), QRImageProvider::RenderImage, engine});
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_TEST_MOCKS_DIR));
        static MockAppMode app_mode;
        static MockBuildInfo build_info;
        static MockOptionsModel options_model;
        static MockChainModel chain_model;
        static MockBlockClockModel block_clock_model;
        static MockNodeModel node_model;
        static MockPeerTableModel peer_table_model;
        static MockNetworkTrafficTower network_traffic_tower;
        static MockNetworkStatusModel network_status_model;
        static MockPeerListModelProxy peer_list_model_proxy;
        static MockBanListModel ban_list_model;
        static MockPeerDetailsModel peer_details_model;
        static MockPeerDetailsModel other_peer_details_model;
        other_peer_details_model.m_node_id = 8;
        other_peer_details_model.m_raw_address = QStringLiteral("192.0.2.1");
        other_peer_details_model.m_address = QStringLiteral("192.0.2.1:8333");
        QQmlEngine::setObjectOwnership(&peer_details_model, QQmlEngine::CppOwnership);
        peer_list_model_proxy.setPeerDetailsForTest(&peer_details_model);
        static MockWalletQmlModelTransaction wallet_transaction;
        static MockPaymentRequest payment_request;
        static MockSendRecipient send_recipient;
        static MockRecipientsModel recipients_model;
        static MockCoinsListModel coins_list_model;
        static MockWalletQmlModel wallet_model;
        static MockWalletController wallet_controller;
        static MockWalletListModel wallet_list_model;
        static MockTransactionActivityModel transaction_activity_model;
        static MockBumpTransactionModel bump_model;
        static MockSignVerifyMessageModel sign_verify_model;
        static MockDesktopWindowBehaviorModel desktop_window_behavior_model;
        static MockDesktopTrayIconController desktop_tray_icon_controller;
        static MockRpcConsoleModel rpc_console_model;
        static MockDebugLogModel debug_log_model;
        static MockUrlOpener url_opener;
        recipients_model.setCurrent(&send_recipient);
        wallet_model.setTransactionActivityModel(&transaction_activity_model);
        wallet_model.setBumpModel(&bump_model);
        wallet_model.setSignVerifyMessageModel(&sign_verify_model);
        wallet_model.setRecipients(&recipients_model);
        wallet_model.setCoinsListModel(&coins_list_model);
        wallet_model.setCurrentTransaction(&wallet_transaction);
        wallet_model.setCurrentPaymentRequest(&payment_request);
        wallet_controller.setSelectedWalletObject(&wallet_model);
        // A second, fully independent wallet for per-wallet view tests.
        static MockWalletQmlModelTransaction second_wallet_transaction;
        static MockPaymentRequest second_payment_request;
        static MockSendRecipient second_send_recipient;
        static MockRecipientsModel second_recipients_model;
        static MockCoinsListModel second_coins_list_model;
        static MockTransactionActivityModel second_transaction_activity_model;
        static MockBumpTransactionModel second_bump_model;
        static MockSignVerifyMessageModel second_sign_verify_model;
        static MockWalletQmlModel second_wallet_model;
        second_recipients_model.setCurrent(&second_send_recipient);
        second_wallet_model.setTransactionActivityModel(&second_transaction_activity_model);
        second_wallet_model.setBumpModel(&second_bump_model);
        second_wallet_model.setSignVerifyMessageModel(&second_sign_verify_model);
        second_wallet_model.setRecipients(&second_recipients_model);
        second_wallet_model.setCoinsListModel(&second_coins_list_model);
        second_wallet_model.setCurrentTransaction(&second_wallet_transaction);
        second_wallet_model.setCurrentPaymentRequest(&second_payment_request);
        second_wallet_model.setProperty("name", QStringLiteral("second"));
        qmlRegisterSingletonType<MockApplicationModels>("org.bitcoincore.qt", 1, 0, "ApplicationModels",
            [](QQmlEngine* qml_engine, QJSEngine*) -> QObject* { return new MockApplicationModels(qml_engine); });
        qmlRegisterSingletonInstance<MockAppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
        qmlRegisterSingletonInstance<MockBuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", &build_info);
        qmlRegisterSingletonInstance<MockUrlOpener>("org.bitcoincore.qt", 1, 0, "UrlOpener", &url_opener);
        qmlRegisterUncreatableType<MockPeerDetailsModel>(
            "org.bitcoincore.qt",
            1,
            0,
            "PeerDetailsModel",
            "Test stub type"
        );
        qmlRegisterUncreatableType<MockNodeModel>("org.bitcoincore.qt", 1, 0, "NodeModel", "Provided by ApplicationModels");
        qmlRegisterType<BitcoinAmount>("org.bitcoincore.qt", 1, 0, "BitcoinAmount");
        qmlRegisterType<MockBitcoinAddress>("org.bitcoincore.qt", 1, 0, "BitcoinAddress");
        qmlRegisterType<MockActivityFilterProxyModel>("org.bitcoincore.qt", 1, 0, "ActivityFilterProxyModel");
        qmlRegisterUncreatableType<MockTransactionActivityModel>("org.bitcoincore.qt", 1, 0, "TransactionActivityModel", "Test fixture");
        qmlRegisterUncreatableType<MockActivityDetailModel>("org.bitcoincore.qt",1,0,"ActivityDetailModel","Per-view factory");
        qmlRegisterType<AppPreferences>("org.bitcoincore.qt",1,0,"AppPreferences");
        qmlRegisterUncreatableType<MockCoreSettingsModel>("org.bitcoincore.qt",1,0,"CoreSettingsModel","Test fixture");
        qmlRegisterUncreatableType<MockCoreProxyDraft>("org.bitcoincore.qt",1,0,"CoreProxyDraft","Test fixture");
        qmlRegisterUncreatableType<MockOptionsTransactionLinks>("org.bitcoincore.qt", 1, 0, "OptionsTransactionLinks", "Test fixture");
        qmlRegisterUncreatableType<MockAddressListModel>("org.bitcoincore.qt", 1, 0, "AddressListModel", "Test stub type");
        qmlRegisterType<MockPaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest");
        qmlRegisterUncreatableType<MockImageExportOperation>("org.bitcoincore.qt", 1, 0, "ImageExportOperation", "Test fixture");
        qmlRegisterUncreatableType<MockReceiveRequestOperation>("org.bitcoincore.qt", 1, 0, "ReceiveRequestOperation", "Test fixture");
        qmlRegisterUncreatableType<MockTransaction>("org.bitcoincore.qt", 1, 0, "Transaction", "Test stub type");
        qmlRegisterUncreatableType<MockSendRecipient>("org.bitcoincore.qt", 1, 0, "SendRecipient", "Test stub type");
        qmlRegisterUncreatableType<MockBumpTransactionModel>("org.bitcoincore.qt", 1, 0, "BumpTransactionModel", "Test stub type");
        qmlRegisterUncreatableType<MockBackupFileOperation>("org.bitcoincore.qt", 1, 0, "BackupFileOperation", "Test stub type");
        qmlRegisterUncreatableType<MockWalletQmlModel>("org.bitcoincore.qt", 1, 0, "WalletQmlModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletProvisioningCheck>("org.bitcoincore.qt", 1, 0, "WalletProvisioningCheck", "Test stub type");
        qmlRegisterUncreatableType<MockWalletListModel>("org.bitcoincore.qt", 1, 0, "WalletListModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletQmlModelTransaction>(
            "org.bitcoincore.qt",
            1,
            0,
            "WalletQmlModelTransaction",
            "Test stub type"
        );
        qmlRegisterUncreatableType<MockDebugLogModel>(
            "org.bitcoincore.qt",
            1,
            0,
            "DebugLogModel",
            "Test stub type"
        );
        qmlRegisterType<MockFileDialog>("org.bitcoincore.qt", 1, 0, "AppFileDialog");
        qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
        qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
        engine->rootContext()->setContextProperty(QStringLiteral("optionsModel"), &options_model);
        engine->rootContext()->setContextProperty(QStringLiteral("chainModel"), &chain_model);
        engine->rootContext()->setContextProperty(QStringLiteral("blockClockModel"), &block_clock_model);
        engine->rootContext()->setContextProperty(QStringLiteral("nodeModel"), &node_model);
        engine->rootContext()->setContextProperty(QStringLiteral("debugLogModel"), &debug_log_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testUrlOpener"), &url_opener);
        engine->rootContext()->setContextProperty(QStringLiteral("peerTableModel"), &peer_table_model);
        engine->rootContext()->setContextProperty(QStringLiteral("networkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("testNetworkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("networkStatusModel"), &network_status_model);
        engine->rootContext()->setContextProperty(QStringLiteral("peerListModelProxy"), &peer_list_model_proxy);
        engine->rootContext()->setContextProperty(QStringLiteral("banListModel"), &ban_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testPeerDetailsModel"), &peer_details_model);
        engine->rootContext()->setContextProperty(QStringLiteral("otherPeerDetailsModel"), &other_peer_details_model);
        engine->rootContext()->setContextProperty(QStringLiteral("walletController"), &wallet_controller);
        engine->rootContext()->setContextProperty(QStringLiteral("walletListModel"), &wallet_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletModel"), &wallet_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testSecondWalletModel"), &second_wallet_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletTransaction"), &wallet_transaction);
        engine->rootContext()->setContextProperty(QStringLiteral("testPaymentRequest"), &payment_request);
        engine->rootContext()->setContextProperty(QStringLiteral("testTransactionActivityModel"), &transaction_activity_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testSendRecipient"), &send_recipient);
        engine->rootContext()->setContextProperty(QStringLiteral("testAutomationEnabled"), false);
        engine->rootContext()->setContextProperty(QStringLiteral("testRecipientsModel"), &recipients_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testCoinsListModel"), &coins_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testBumpModel"), &bump_model);
        engine->rootContext()->setContextProperty(QStringLiteral("desktopWindowBehaviorModel"), &desktop_window_behavior_model);
        engine->rootContext()->setContextProperty(QStringLiteral("desktopTrayIconController"), &desktop_tray_icon_controller);
        engine->rootContext()->setContextProperty(QStringLiteral("rpcConsoleModel"), &rpc_console_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testDebugLogModel"), &debug_log_model);
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_SOURCE_DIR));
    }

private:
    std::vector<QPointer<UiPreferencesStore>> m_preferences_stores;
    mutable blockclock::Snapshot m_block_clock_fixture;
    std::unique_ptr<BackendExecutor> m_qr_executor;
    QTemporaryDir m_settings;
    bool m_had_warnings{false};
};

int main(int argc, char* argv[])
{
    // Transition animations are irrelevant to assertions and make teardown flaky on Qt 6.2.
    qputenv("BITCOIN_QML_TEST_NO_ANIMATIONS", "1");
    QmlTestsSetup setup;
    const int status = quick_test_main_with_setup(argc, argv, "bitcoinqml_qmltests", QUICK_TEST_SOURCE_DIR, &setup);
    if (status != 0) return status;
    return setup.hadWarnings() ? EXIT_FAILURE : EXIT_SUCCESS;
}

#include <qml_tests_main.moc>
