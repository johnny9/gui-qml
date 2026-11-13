// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/receiverequestoperation.h>

#include <QPointer>

void ReceiveRequestOperation::start(const std::shared_ptr<walletrequests::Store>& store,
                                   walletrequests::Command command, const std::shared_ptr<QObject>& bridge)
{
    if (m_started) return;
    m_started = true;
    const QPointer<ReceiveRequestOperation> observer{this};
    auto notify = [observer, bridge] {
        QMetaObject::invokeMethod(bridge.get(), [observer] { if (observer) observer->consume(); }, Qt::QueuedConnection);
    };
    walletrequests::Admission admission;
    if (store && bridge) admission = store->submit(std::move(command), notify);
    else admission.rejection = execution::Rejection::Closed;
    m_operation = std::move(admission.operation);
    if (m_operation) m_address_intent = m_operation->address_intent;
    if (!m_operation) {
        m_result.emplace();
        m_result->error = admission.rejection == execution::Rejection::Closed
            ? walletrequests::Error::Closed : walletrequests::Error::Rejected;
        // Even an immediate rejection is delivered after the caller has bound
        // its view to this particular operation.
        if (bridge) notify();
    }
}

void ReceiveRequestOperation::consume()
{
    if (m_operation) {
        auto result = m_operation->result();
        if (!result) return;
        m_result = std::move(result);
        m_operation.reset();
    }
    if (m_result) Q_EMIT changed();
}

QString ReceiveRequestOperation::errorText() const
{
    return ErrorText(m_result ? m_result->error : walletrequests::Error::None, requestSaved());
}

QString ReceiveRequestOperation::ErrorText(walletrequests::Error error, bool request_saved)
{
    switch (static_cast<Error>(error)) {
    case Error::None: return {};
    case Error::Loading: return tr("Payment requests are still loading.");
    case Error::Checking: return tr("Payments are still being checked. Try again when checking finishes.");
    case Error::Invalid: return tr("Check the payment request fields.");
    case Error::Missing: return tr("This payment request is no longer available.");
    case Error::Changed: return tr("This payment request changed. Reopen it before saving.");
    case Error::Paid: return tr("This address has received a payment. Its public request fields cannot be changed.");
    case Error::IdExhausted: return tr("Another payment request cannot be created in this wallet.");
    case Error::NeedsUnlock: return tr("Unlock the wallet to create a receiving address.");
    case Error::Rejected: return tr("The operation could not be scheduled. Try again.");
    case Error::Closed: return tr("This wallet opening has closed.");
    case Error::Persistence: return tr("The payment request could not be saved. Try again.");
    case Error::LabelPersistence: return request_saved
        ? tr("The request was saved, but its address note could not be saved.")
        : tr("The address note could not be saved.");
    case Error::Partial: return tr("Only part of the change was saved. Reload before retrying.");
    case Error::Uncertain: return tr("The result could not be confirmed. Reload before retrying.");
    case Error::Unsupported: return tr("This operation is not supported by the wallet interface.");
    case Error::AlreadySaved: return tr("This receiving address was already used for a request. Create a new receiving address.");
    case Error::IncorrectPassphrase: return tr("The wallet password you entered was incorrect.");
    case Error::AddressCreation: return tr("A receiving address could not be generated. Please try again.");
    case Error::ReservationPersistence: return tr("The receiving-address reservation could not be saved. Retry before creating a request.");
    }
    return {};
}
