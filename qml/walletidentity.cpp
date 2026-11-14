// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletidentity.h>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace walletidentity {
namespace {
QString Text(const char* text) { return QCoreApplication::translate("WalletCreationModel", text); }
}
QString Canonical(const QString& backend_name, const QString& wallet_directory)
{
    if (backend_name.isEmpty()) return {};
    auto cleaned = QDir::cleanPath(backend_name);
    if (!QDir::isAbsolutePath(cleaned) || wallet_directory.isEmpty()) return cleaned;
    const auto relative = QDir(wallet_directory).relativeFilePath(cleaned);
    return relative != QLatin1String("..") && !relative.startsWith(QLatin1String("../")) && !QDir::isAbsolutePath(relative)
        ? (relative == QLatin1String(".") ? QString{} : relative) : cleaned;
}
QString Display(const QString& id)
{
    if (id.isEmpty()) return Text("Default wallet");
    if (id == id.trimmed()) return id;
    QString escaped = id; escaped.replace(QLatin1Char(' '), QChar{0x2420}); escaped.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n")); escaped.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    return escaped;
}
QString CreationError(const QString& name)
{
    if (name.trimmed().isEmpty()) return Text("Enter a wallet name.");
    if (name.size() > 32768) return Text("The wallet name is too long.");
    if (name != name.trimmed()) return Text("Use a wallet name without leading or trailing whitespace.");
    if (QString::fromUtf8(name.toUtf8()) != name || name.contains(QChar::Null)) return Text("The wallet name contains unsupported characters.");
    if (QDir::isAbsolutePath(name) || name.contains(QLatin1Char('\\')) || Canonical(name) != name || name == QLatin1String(".") || name == QLatin1String("..") || name.startsWith(QLatin1String("../")))
        return Text("Use a relative wallet name without a trailing separator or parent path.");
    return {};
}
QString AliasKey(const QString& id) { return QStringLiteral("walletDisplayNames/%1").arg(id); }
QString ReadAlias(QSettings& settings, const QString& id)
{
    // Keep the persisted format, including exact significant whitespace. Never
    // borrow another wallet's alias by trimming its backend identity.
    return settings.value(AliasKey(id)).toString().trimmed();
}
std::map<QString, QString> ReadAliases(QSettings& settings)
{
    std::map<QString, QString> aliases;
    settings.beginGroup(QStringLiteral("walletDisplayNames"));
    for (const auto& id : settings.allKeys()) aliases.emplace(id, settings.value(id).toString().trimmed());
    settings.endGroup();
    return aliases;
}
void WriteAlias(QSettings& settings, const QString& id, const QString& alias)
{
    if (alias.isEmpty() || alias == id) settings.remove(AliasKey(id));
    else settings.setValue(AliasKey(id), alias.trimmed());
}
} // namespace walletidentity
