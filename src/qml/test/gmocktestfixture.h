// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_GMOCKTESTFIXTURE_H
#define BITCOIN_QML_TEST_GMOCKTESTFIXTURE_H

#include <QObject>
#include <QtTest/QtTest>

#ifdef Assert
#pragma push_macro("Assert")
#undef Assert
#define BITCOIN_QML_RESTORE_ASSERT_MACRO
#endif

#include <gmock/gmock.h>

#ifdef BITCOIN_QML_RESTORE_ASSERT_MACRO
#pragma pop_macro("Assert")
#undef BITCOIN_QML_RESTORE_ASSERT_MACRO
#endif

#include <mutex>
#include <string>
#include <vector>

class GmockFailureLog
{
public:
    void add(std::string msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failures.push_back(std::move(msg));
    }

    std::vector<std::string> drain()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> out;
        out.swap(m_failures);
        return out;
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_failures;
};

inline GmockFailureLog& gmockFailureLog()
{
    static GmockFailureLog log;
    return log;
}

class QtestGmockListener : public testing::EmptyTestEventListener
{
public:
    void OnTestPartResult(const testing::TestPartResult& result) override
    {
        if (result.failed()) {
            std::string msg;
            if (result.file_name()) {
                msg += result.file_name();
                msg += ":";
                msg += std::to_string(result.line_number());
                msg += "\n";
            }
            msg += result.message();
            gmockFailureLog().add(std::move(msg));
        }
    }
};

class GmockTestFixture : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        gmockFailureLog().drain();
    }

    void cleanup()
    {
        auto failures = gmockFailureLog().drain();
        if (!failures.empty()) {
            std::string combined{"GMock failure(s):\n"};
            for (const auto& f : failures) {
                combined += f;
                combined += "\n";
            }
            QFAIL(combined.c_str());
        }
    }
};

#endif // BITCOIN_QML_TEST_GMOCKTESTFIXTURE_H
