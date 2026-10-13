// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_process.h>
#include <test/integration/runner/test_cases.h>

#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

int RunIntegrationProcesses(int argc, char* argv[], const IntegrationTestModule* selected_module,
                            std::optional<WalletMode> wallet_mode)
{
    QCoreApplication app(argc, argv);
    int status{EXIT_SUCCESS};
    bool ran{false};
    for (const auto& module : IntegrationTestModules()) {
        if (selected_module && &module != selected_module) continue;
        for (const auto mode : module.wallet_modes) {
            if (wallet_mode && mode != *wallet_mode) continue;
            ran = true;
            QTemporaryDir profile;
            if (!profile.isValid()) return EXIT_FAILURE;
            auto arguments = app.arguments().mid(1);
            arguments.append(QStringLiteral("--module=") + QString::fromLatin1(module.name));
            arguments.append(QStringLiteral("--wallet=") + QString::fromLatin1(WalletModeName(mode)));
            arguments.append(QStringLiteral("--test-profile=") + profile.path());
            std::cout << "Running " << module.name << " with wallets " << WalletModeName(mode) << std::endl;
            QProcess process;
            process.setProcessChannelMode(QProcess::ForwardedChannels);
            process.start(app.applicationFilePath(), arguments);
            if (!process.waitForStarted(5'000)) {
                std::cerr << "Could not start integration process: " << process.errorString().toStdString() << '\n';
                status = EXIT_FAILURE;
                continue;
            }
            if (!process.waitForFinished(120'000)) {
                std::cerr << "Integration process did not finish: " << process.errorString().toStdString() << '\n';
                process.kill();
                process.waitForFinished(5'000);
                status = EXIT_FAILURE;
            } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != EXIT_SUCCESS) {
                std::cerr << "Integration module " << module.name << " failed with wallets " << WalletModeName(mode)
                          << " (exit " << process.exitCode() << ")\n";
                status = EXIT_FAILURE;
            }
        }
    }
    if (!ran) std::cerr << "No integration modules support the selected wallet mode\n";
    return ran ? status : EXIT_FAILURE;
}
