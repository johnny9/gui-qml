// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H
#define BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H

#include <functional>
#include <memory>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QTimer>

class BlockClockHistory;

/**
 * Raw data represented by the block clock.
 *
 * The dial covers one local twelve-hour period, beginning at midnight or
 * noon. Timestamps are Unix seconds. Block timestamps are sorted and restricted
 * to [period_start, period_start + PERIOD_SECONDS). Equal timestamps are kept:
 * distinct blocks can share a timestamp and each contributes a confirmation.
 */
struct BlockClockTimeline
{
    qint64 period_start{0};
    QList<qint64> block_timestamps;
};

/**
 * Maintains block-clock time and history independently of its presentation.
 *
 * This model remains current while the dial is hidden. currentTimeFraction is
 * refreshed at minute boundaries and when a block arrives. It is separate
 * from blockTimeFractions so a clock tick never republishes the full block
 * history. Fractions are normalized to the current twelve-hour period and are
 * always in the range [0, 1]. An empty blockTimeFractions list is valid: it
 * means the active chain contains no blocks timestamped in the displayed
 * period, not that synchronization is incomplete. Initial-sync state belongs
 * to NodeModel.
 *
 * All methods and owned timers run on this object's thread (the GUI thread
 * in production). History comes from a local notification cache. This model
 * never calls the node or chain interfaces. A bounded refresh cadence coalesces
 * block notifications, including while the clock is hidden.
 */
class BlockClockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 periodStart READ periodStart NOTIFY periodChanged)
    Q_PROPERTY(qreal currentTimeFraction READ currentTimeFraction NOTIFY currentTimeFractionChanged)
    Q_PROPERTY(QList<qreal> blockTimeFractions READ blockTimeFractions NOTIFY blockTimeFractionsChanged)

public:
    static constexpr qint64 PERIOD_SECONDS{12 * 60 * 60};
    static constexpr int CLOCK_UPDATE_INTERVAL_MS{60 * 1000};
    static constexpr int HISTORY_UPDATE_INTERVAL_MS{100};
    using CurrentTimeProvider = std::function<QDateTime()>;

    explicit BlockClockModel(std::shared_ptr<BlockClockHistory> history = {}, bool start_timer = true,
                             CurrentTimeProvider current_time_provider = {}, QObject* parent = nullptr);

    qint64 periodStart() const { return m_timeline.period_start; }
    qreal currentTimeFraction() const { return m_current_time_fraction; }
    QList<qreal> blockTimeFractions() const { return m_block_time_fractions; }
    bool timerActive() const { return m_clock_timer.isActive(); }

    /** Return the midnight/noon boundary containing @p current_time. */
    static qint64 PeriodStartFor(const QDateTime& current_time);

    /** Update the scalar clock position. Public to allow deterministic tests. */
    void updateCurrentTime(const QDateTime& current_time);

public Q_SLOTS:
    /** Consume coalesced block notifications without querying the backend. */
    void refreshHistory();

Q_SIGNALS:
    void periodChanged();
    void currentTimeFractionChanged();
    void blockTimeFractionsChanged();

private:
    void scheduleNextClockUpdate(const QDateTime& current_time);
    void replaceBlockHistory(QList<qint64> block_timestamps, bool period_changed = false);
    void rebuildBlockTimeFractions();
    qreal fractionForTimestamp(qint64 timestamp) const;

    std::shared_ptr<BlockClockHistory> m_history;
    CurrentTimeProvider m_current_time_provider;
    QTimer m_clock_timer;
    QTimer m_history_timer;
    QList<qint64> m_cached_block_timestamps;
    BlockClockTimeline m_timeline;
    qreal m_current_time_fraction{0.0};
    QList<qreal> m_block_time_fractions;
};

#endif // BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H
