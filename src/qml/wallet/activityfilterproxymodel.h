// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_ACTIVITYFILTERPROXYMODEL_H
#define BITCOIN_QML_WALLET_ACTIVITYFILTERPROXYMODEL_H
#include <QSortFilterProxyModel>
#include <QVariantMap>
class ActivityTimelineModel;

class ActivityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filtersChanged)
    Q_PROPERTY(int typeFilter READ typeFilter WRITE setTypeFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString fromDate READ fromDate WRITE setFromDate NOTIFY filtersChanged)
    Q_PROPERTY(QString throughDate READ throughDate WRITE setThroughDate NOTIFY filtersChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    enum Type { All, Received, Sent, Self, Generated, Requests };
    Q_ENUM(Type)
    explicit ActivityFilterProxyModel(ActivityTimelineModel& timeline, QObject* parent = nullptr);
    QString searchText() const { return m_search; }
    int typeFilter() const { return m_type; }
    QString fromDate() const { return m_from; }
    QString throughDate() const { return m_through; }
    QString error() const { return m_error; }
    void setSearchText(const QString& text);
    void setTypeFilter(int type);
    void setFromDate(const QString& date);
    void setThroughDate(const QString& date);
    QVector<QVariantMap> snapshot() const;
    Q_INVOKABLE bool exportToPath(const QString& path);
    Q_INVOKABLE bool chooseExportFile();
Q_SIGNALS:
    void countChanged();
    void filtersChanged();
    void errorChanged();
protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
private:
    ActivityTimelineModel& m_timeline;
    QString m_search, m_from, m_through, m_error;
    int m_type{All};
};
#endif // BITCOIN_QML_WALLET_ACTIVITYFILTERPROXYMODEL_H
