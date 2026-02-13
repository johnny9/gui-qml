// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinaddress.h>

namespace {
constexpr int MAX_BITCOIN_ADDRESS_LENGTH = 90;

bool IsBitcoinAddressChar(const QChar c)
{
    const ushort ch = c.unicode();
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}
} // namespace

BitcoinAddress::BitcoinAddress(QObject* parent)
    : QObject(parent)
{
    // Initialize with empty address
    setAddress("", 0);
}

BitcoinAddress::BitcoinAddress(const QString& address, QObject* parent)
    : QObject(parent)
{
    setAddress(address, 0);
}

QString BitcoinAddress::address() const
{
    return m_address;
}

bool BitcoinAddress::isEmpty() const
{
    return m_address.isEmpty();
}

QString BitcoinAddress::formattedAddress() const
{
    return m_formattedAddress;
}

QString BitcoinAddress::ellipsesAddress() const
{
    return ellipsesAddress(m_address);
}

int BitcoinAddress::setAddress(const QString& input, int cursorPosition)
{
    // 1) Count how many valid chars were before the old cursor.
    int posInClean = 0;
    const int lenIn = qMin(qMax(cursorPosition, 0), input.length());
    for (int i = 0; i < lenIn; ++i) {
        if (IsBitcoinAddressChar(input[i])) {
            ++posInClean;
        }
    }

    // 2) Build the raw (no-space) address, filtering and capping at 90 chars.
    QString raw;
    raw.reserve(qMin(input.length(), MAX_BITCOIN_ADDRESS_LENGTH));
    for (QChar c : input) {
        if (IsBitcoinAddressChar(c)) {
            raw += c;
            if (raw.length() >= MAX_BITCOIN_ADDRESS_LENGTH) {
                break;
            }
        }
    }

    // 3) Format into groups of 4 chars separated by spaces.
    QString fmt = BitcoinAddress::formattedAddress(raw);

    // 4) Map back to a cursor position in the new formatted string.
    int newCursor = 0, seen = 0;
    while (newCursor < fmt.length() && seen < posInClean) {
        if (fmt[newCursor] != QChar(' ')) {
            ++seen;
        }
        ++newCursor;
    }

    if (raw == m_address && fmt == m_formattedAddress) {
        return newCursor;
    }

    m_address = raw;
    m_formattedAddress = fmt;
    Q_EMIT addressChanged();
    Q_EMIT formattedAddressChanged();
    Q_EMIT ellipsesAddressChanged();

    return newCursor;
}

QString BitcoinAddress::formattedAddress(const QString& address)
{
    QString fmt;
    fmt.reserve(address.length() + address.length() / 4);
    for (int i = 0; i < address.length(); ++i) {
        if (i > 0 && (i % 4) == 0) {
            fmt += QChar(' ');
        }
        fmt += address[i];
    }
    return fmt;
}

QString BitcoinAddress::ellipsesAddress(const QString& address)
{
    if (address.length() > 8) {
        QString left = address.left(4) + ' ' + address.mid(4, 4);

        QString right = address.mid(address.length() - 8, 4) + ' ' + address.right(4);

        return left + " ... " + right;
    } else {
        return address;
    }
}
