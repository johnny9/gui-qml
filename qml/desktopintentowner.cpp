// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintentowner.h>
#include <qml/retainedvalue.h>

#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace desktopintent {
struct Owner::ReplyCell {
    std::mutex mutex;
    bool ready{false};
    InspectionReceipt receipt;
    std::weak_ptr<execution::Scheduler> scope;
    execution::TaskId cleanup;
    Ticket ticket;
    Services::Time deadline;
    void complete(InspectionReceipt value) {
        {
            std::lock_guard lock{mutex};
            if (ready) return;
            ready = true;
            receipt = value;
        }
        if (const auto retained = scope.lock()) retained->activateCleanup(cleanup);
    }
};
struct Owner::State {
    Queue queue;
    std::shared_ptr<const Sink> sink;
    std::shared_ptr<const Sink> receiver;
    std::optional<Ticket> presented;
    std::map<uint64_t, std::shared_ptr<ReplyCell>> replies;
    uint64_t sequence{0}, query_reply{0}, publication{0};
    bool wake_pending{false};
};

Owner::Owner(Services services, std::shared_ptr<execution::Scheduler> scope,
             std::shared_ptr<State> state, Notify notify)
    : m_services(std::move(services)), m_scope(std::move(scope)), m_key(m_scope->allocateKey()),
      m_state(std::move(state)), m_notify(std::move(notify)) {}

std::shared_ptr<Owner> Owner::Create(Services services, Notify notify)
{
    if (!services.scheduler || !services.now || services.receipt_timeout <= std::chrono::milliseconds::zero() ||
        services.receipt_timeout > std::chrono::seconds{30}) return {};
    auto scope = services.scheduler->openActionScope(1024);
    if (!scope) return {};
    auto state = execution::RetainValues<State>(scope, [] { return std::make_unique<State>(); });
    if (!state) return {};
    return std::shared_ptr<Owner>{new Owner(std::move(services), std::move(scope), std::move(state), std::move(notify))};
}

execution::Admission Owner::submit(std::function<void()> decision, size_t payload_bytes)
{
    return execution::Submit(*m_scope, [self = shared_from_this(), decision = std::move(decision)] {
        decision(); self->retireObsolete(); self->pump(); self->publish();
    }, [] {}, {}, {execution::Lane::Domain, payload_bytes, m_key});
}

void Owner::publish()
{
    if (m_state->publication == std::numeric_limits<uint64_t>::max()) return;
    auto snapshot = m_state->queue.snapshot();
    snapshot.sequence = ++m_state->publication;
    try { if (m_notify) m_notify(std::move(snapshot)); } catch (...) {}
}

execution::Admission Owner::admit(std::vector<Input> inputs, std::function<void(Admission)> done)
{
    std::lock_guard lock{m_admission_mutex};
    if (m_closed->load() || m_forwarding_closed.load()) return {{}, execution::Rejection::Closed};
    if (inputs.empty() || inputs.size() > Queue::MAX_PENDING) return {{}, execution::Rejection::Invalid};
    size_t payload_bytes{sizeof(inputs) + inputs.size() * sizeof(Input)};
    for (const auto& input : inputs) {
        if (input.value.size() > Queue::MAX_INPUT_CHARS || input.producer_key.size() > Queue::MAX_KEY_CHARS)
            return {{}, execution::Rejection::Capacity};
        payload_bytes += static_cast<size_t>(input.value.size() + input.producer_key.size()) * sizeof(QChar);
    }
    auto retained = execution::RetainValues<std::vector<Input>>(m_scope, [&] { return std::make_unique<std::vector<Input>>(std::move(inputs)); });
    if (!retained) return {{}, execution::Rejection::Capacity};
    return submit([self = shared_from_this(), retained, done = std::move(done)] {
        if (self->m_closed->load()) self->m_state->queue.stop();
        auto receipt = self->m_state->queue.admit(*retained);
        try { if (done) done(std::move(receipt)); } catch (...) {}
    }, payload_bytes);
}

execution::Admission Owner::prepareForwarding(std::vector<Input> arguments, std::function<void(ForwardingSnapshot)> done)
{
    std::lock_guard lock{m_admission_mutex};
    if (m_closed->load() || m_forwarding_closed.load()) return {{}, execution::Rejection::Closed};
    if (arguments.size() > Queue::MAX_PENDING) return {{}, execution::Rejection::Invalid};
    size_t bytes{sizeof(arguments) + arguments.size() * sizeof(Input)};
    for (const auto& input : arguments) {
        if (input.value.size() > Queue::MAX_INPUT_CHARS || input.producer_key.size() > Queue::MAX_KEY_CHARS)
            return {{}, execution::Rejection::Capacity};
        bytes += static_cast<size_t>(input.value.size() + input.producer_key.size()) * sizeof(QChar);
    }
    auto retained = execution::RetainValues<std::vector<Input>>(m_scope, [&] { return std::make_unique<std::vector<Input>>(std::move(arguments)); });
    if (!retained) return {{}, execution::Rejection::Capacity};
    m_forwarding_closed.store(true);
    const auto admitted = submit([self = shared_from_this(), retained, done = std::move(done)] {
        if (self->m_closed->load()) self->m_state->queue.stop();
        auto snapshot = self->m_state->queue.prepareForwarding(*retained);
        try { if (done) done(std::move(snapshot)); } catch (...) {}
    }, bytes);
    if (!admitted) m_forwarding_closed.store(false);
    return admitted;
}

execution::Admission Owner::readiness(uint64_t revision, bool ready)
{
    std::lock_guard lock{m_admission_mutex};
    if (m_forwarding_closed.load()) return {{}, execution::Rejection::Closed};
    return submit([self = shared_from_this(), revision, ready] {
        self->m_state->queue.readiness(revision, ready && !self->m_closed->load());
    });
}

std::shared_ptr<Owner::ReplyCell> Owner::reserve(ReplyKind kind, Ticket ticket)
{
    auto& state = *m_state;
    if (state.sequence == std::numeric_limits<uint64_t>::max()) return {};
    const auto sequence = ++state.sequence;
    auto cell = std::make_shared<ReplyCell>();
    cell->scope = m_scope;
    cell->ticket = ticket;
    const auto now = m_services.now();
    const auto latest = Services::Time::max() - m_services.receipt_timeout;
    cell->deadline = now > latest ? Services::Time::max() : now + m_services.receipt_timeout;
    const auto reserved = execution::Submit(*m_scope, [cell] {
        std::lock_guard lock{cell->mutex}; return cell->receipt;
    }, [self = shared_from_this(), sequence, kind, ticket](InspectionReceipt receipt) {
        self->received(sequence, kind, ticket, receipt);
    }, {}, {execution::Lane::Cleanup, sizeof(ReplyCell), m_scope->allocateKey()}, true);
    if (!reserved) return {};
    cell->cleanup = reserved.id;
    try { state.replies.emplace(sequence, cell); }
    catch (...) { cell->complete({}); return {}; }
    if (kind == ReplyKind::Query) state.query_reply = sequence;
    return cell;
}

execution::Admission Owner::review(std::shared_ptr<const Sink> sink)
{
    return submit([self = shared_from_this(), sink = std::move(sink)] { self->deliver(sink); });
}
execution::Admission Owner::registerReceiver(std::shared_ptr<const Sink> sink)
{
    return submit([self = shared_from_this(), sink = std::move(sink)] {
        if (!self->m_closed->load() && sink && sink->identity && sink->inspect && sink->query) self->m_state->receiver = sink;
    });
}
execution::Admission Owner::releasePresentation(Ticket ticket)
{
    return submit([self = shared_from_this(), ticket] {
        if (self->m_state->presented != ticket) return;
        self->m_state->presented.reset();
        self->m_state->queue.receiverCapacity(true);
        if (self->m_state->queue.snapshot().active != ticket) self->m_state->sink.reset();
    });
}
void Owner::pump()
{
    const auto snapshot = m_state->queue.snapshot();
    if (m_state->receiver && !m_state->presented && snapshot.phase == Phase::Ready && snapshot.pending) deliver(m_state->receiver);
}
void Owner::deliver(std::shared_ptr<const Sink> sink)
{
    auto& state = *m_state;
    if (m_closed->load() || m_forwarding_closed.load() || state.presented || !sink || !sink->identity || !sink->inspect || !sink->query) return;
    const auto delivery = state.queue.deliver(sink->identity);
    if (!delivery) return;
    try {
        if (m_services.available && !m_services.available()) {
            state.queue.rejected(delivery->ticket); return;
        }
    } catch (...) { state.queue.rejected(delivery->ticket); return; }
    std::shared_ptr<ReplyCell> cell;
    try { cell = reserve(ReplyKind::Inspect, delivery->ticket); } catch (...) {}
    if (!cell) { state.queue.rejected(delivery->ticket); return; }
    state.sink = sink;
    if (state.receiver && state.receiver->identity == sink->identity) { state.presented = delivery->ticket; state.queue.receiverCapacity(false); }
    try {
        if (!sink->inspect(*delivery, [cell](InspectionReceipt reply) { cell->complete(reply); }))
            cell->complete({Knowledge::NotAccepted, 0});
    } catch (...) { cell->complete({}); }
    wake();
}
void Owner::received(uint64_t reply, ReplyKind kind, Ticket ticket, InspectionReceipt receipt)
{
    auto& state = *m_state;
    state.replies.erase(reply);
    if (state.query_reply == reply) state.query_reply = 0;
    if (state.queue.snapshot().active != ticket) { retireObsolete(); publish(); return; }
    switch (receipt.knowledge) {
    case Knowledge::Accepted:
        if (!receipt.inspection) state.queue.unknown(ticket);
        else { state.queue.accepted(ticket, receipt.inspection); if (state.presented != ticket) state.sink.reset(); }
        break;
    case Knowledge::NotAccepted:
        state.queue.rejected(ticket); state.sink.reset();
        if (state.presented == ticket) { state.presented.reset(); state.queue.receiverCapacity(true); }
        break;
    case Knowledge::Unknown:
        state.queue.unknown(ticket);
        break;
    }
    (void)kind;
    retireObsolete(); wake(); pump(); publish();
}

void Owner::query()
{
    auto& state = *m_state;
    const auto current = state.queue.snapshot();
    if (!current.active || state.query_reply || !state.sink || !state.sink->query) return;
    const auto cell = reserve(ReplyKind::Query, *current.active);
    if (!cell) { state.queue.unknown(*current.active); wake(); return; }
    try {
        if (!state.sink->query(*current.active, [cell](InspectionReceipt reply) { cell->complete(reply); }))
            cell->complete({});
    } catch (...) { cell->complete({}); }
    wake();
}
void Owner::retireObsolete()
{
    const auto active = m_state->queue.snapshot().active;
    for (const auto& [identity, cell] : m_state->replies) {
        (void)identity;
        if (active != cell->ticket) cell->complete({});
    }
}

void Owner::wake()
{
    if (m_state->wake_pending || !m_state->queue.snapshot().active || !m_services.wake) return;
    m_state->wake_pending = true;
    try {
        m_services.wake(m_services.receipt_timeout, [weak = weak_from_this()] {
            if (const auto self = weak.lock()) self->tick();
        });
    } catch (...) {
        m_state->wake_pending = false;
        if (const auto active = m_state->queue.snapshot().active) m_state->queue.unknown(*active);
    }
}

execution::Admission Owner::tick()
{
    return submit([self = shared_from_this()] {
        auto& state = *self->m_state;
        state.wake_pending = false;
        const auto now = self->m_services.now();
        for (const auto& [identity, cell] : state.replies) {
            (void)identity;
            if (now >= cell->deadline) cell->complete({});
        }
        const auto snapshot = state.queue.snapshot();
        if (snapshot.phase == Phase::ReconcilingDelivery || snapshot.phase == Phase::Stopping) self->query();
        self->wake();
    });
}

execution::Admission Owner::reconcile() { return submit([self = shared_from_this()] { self->query(); }); }
execution::Admission Owner::dismiss() { return submit([self = shared_from_this()] { self->m_state->queue.dismiss(); }); }
execution::Admission Owner::lookup(QString producer_key, std::function<void(std::optional<Receipt>)> done)
{
    if (producer_key.size() > Queue::MAX_KEY_CHARS) return {{}, execution::Rejection::Invalid};
    return submit([self = shared_from_this(), producer_key = std::move(producer_key), done = std::move(done)] {
        try { if (done) done(self->m_state->queue.lookup(producer_key)); } catch (...) {}
    });
}
execution::Admission Owner::stop()
{
    m_closed->store(true);
    return submit([self = shared_from_this()] {
        auto& state = *self->m_state;
        const auto active = state.queue.snapshot().active;
        const auto presented = state.presented;
        state.queue.stop();
        const auto retire = active ? active : presented;
        state.presented.reset(); state.receiver.reset();
        if (retire && state.sink && state.sink->retire) {
            try { state.sink->retire(*retire); } catch (...) {}
        }
        if (!active) state.sink.reset();
        self->query();
    });
}
} // namespace desktopintent
