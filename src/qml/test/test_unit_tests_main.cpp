// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QCoreApplication>

int RunBitcoinAmountTests(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= RunBitcoinAmountTests(argc, argv);

    return status;
}
