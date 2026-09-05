// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/feepolicy.h>
#include <QRegularExpression>
#include <util/strencodings.h>

std::optional<CAmount> ParseCustomFeeRate(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)"));
    auto trimmed = text.trimmed();
    if (!pattern.match(trimmed).hasMatch()) return {};
    if (trimmed.endsWith('.')) trimmed += '0';
    CAmount per_kvb{0};
    if (!ParseFixedPoint(trimmed.toStdString(), 3, &per_kvb) || per_kvb <= 0 || !MoneyRange(per_kvb)) return {};
    return per_kvb;
}
