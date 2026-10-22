#include "PromiseTransactionExecutive.h"
#include "bcos-framework/executor/ExecuteError.h"
#include <future>
#include <thread>

using namespace bcos::executor;

// ---------------------------------------------------------------------------
// MessagePromiseSwapper
// ---------------------------------------------------------------------------
// spawnAndCall must execute spawnCall on a thread that is NOT part of the
// main IOServicePool.  Otherwise, when the exec flow holds a pool thread
// (e.g. inside tbb::flow::graph::wait_for_all or during DMC-resume
// processing), the posted work could never be picked up → deadlock.
//
// We use std::thread(...).detach() to launch a dedicated on-demand thread
// in fire-and-forget fashion.  std::async is deliberately NOT used here
// because its returned std::future joins on destruction, which would
// deadlock when spawnCall internally blocks on a nested spawnAndCall
// (cross-shard DMC externalCall) — the inner promise is only fulfilled by
// a later resume(), which cannot happen until the outer spawnAndCall
// returns, but the outer future's destructor would be waiting to join the
// still-blocked worker thread.
//
// Threads are created only when PromiseTransactionExecutive is actually
// invoked and exit after the task completes. ShardingTransactionExecutive selects
// this receiver only with usePromise=true; ShardingExecutiveFactory currently sets
// needUsePromise=false, so its default sharding receiver uses coroutines.
// ---------------------------------------------------------------------------

MessagePromiseSwapper::MessagePromiseSwapper(IOServicePool::Ptr /*_pool*/) {}

void MessagePromiseSwapper::spawnAndCall(std::function<CallParameters::UniquePtr()> spawnCall,
    std::function<void(CallParameters::UniquePtr)> waitAndDo)
{
    auto lastPromise = m_currentPromise;
    m_currentPromise = std::make_shared<std::promise<CallParameters::UniquePtr>>();
    auto currentPromise = m_currentPromise;

    // Launch work on a DEDICATED on-demand thread, never on the main
    // IOServicePool.  This breaks the cross-dependency that would otherwise
    // deadlock on low-core-count systems.
    //
    // IMPORTANT: std::thread(...).detach() is used instead of std::async
    // because the spawned task may internally call spawnAndCall again
    // (cross-shard DMC externalCall) and block on the nested promise.
    // A std::async future would join on destruction and deadlock waiting
    // for the still-blocked thread — the nested promise is only fulfilled
    // by a future resume() that requires this spawnAndCall to return first.
    // The lambda MUST read m_currentPromise (the member) at completion
    // time, NOT a frozen snapshot.  Under the serialized ping-pong
    // protocol the member has been advanced by nested spawnAndCall /
    // resume() calls while execute() was in-flight, and the last
    // completer must deliver to the latest promise.
    //
    // Data-race safety: every m_currentPromise write in a subsequent
    // spawnAndCall happens-before this thread's wake-up via the
    // thread-creation + promise set_value/get chain; the scheduler is
    // always blocked in get() when this thread's completion runs.
    std::thread([this, lastPromise, spawnCall = std::move(spawnCall)]() mutable {
        try
        {
            auto message = spawnCall();
            auto promise = lastPromise ? lastPromise : m_currentPromise;
            promise->set_value(std::move(message));
        }
        catch (...)
        {
            auto promise = lastPromise ? lastPromise : m_currentPromise;
            promise->set_exception(std::current_exception());
        }
    }).detach();

    auto message = currentPromise->get_future().get();
    waitAndDo(std::move(message));
}

PromiseTransactionExecutive::PromiseTransactionExecutive(IOServicePool::Ptr pool,
    const BlockContext& blockContext, std::string contractAddress, int64_t contextID, int64_t seq)
  : CoroutineTransactionExecutive(blockContext, std::move(contractAddress), contextID, seq),
    m_messageSwapper(std::make_shared<MessagePromiseSwapper>(std::move(pool)))
{}

CallParameters::UniquePtr PromiseTransactionExecutive::resume()
{
    CallParameters::UniquePtr exchangeMessage;
    m_messageSwapper->spawnAndCall([this]() { return std::move(m_exchangeMessage); },
        [&exchangeMessage](
            CallParameters::UniquePtr message) { exchangeMessage = std::move(message); });

    return exchangeMessage;
}

MessagePromiseSwapper::Ptr PromiseTransactionExecutive::getPromiseMessageSwapper()
{
    return m_messageSwapper;
}

void PromiseTransactionExecutive::setPromiseMessageSwapper(MessagePromiseSwapper::Ptr swapper)
{
    m_messageSwapper = std::move(swapper);
}

CallParameters::UniquePtr PromiseTransactionExecutive::start(CallParameters::UniquePtr input)
{
    auto spawnCall = [this, &input]() {
        auto& blockContext = m_blockContext;

        if (!input->keyLocks.empty())
        {
            m_syncStorageWrapper->importExistsKeyLocks(input->keyLocks);
        }

        auto exchangeMessage = execute(std::move(input));

        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_dmc_revert))
        {
            exchangeMessage = waitingFinish(std::move(exchangeMessage));
        }

        if (exchangeMessage->type == CallParameters::Type::FINISHED)
        {
            // Execute is finished, erase the key locks
            exchangeMessage->keyLocks.clear();
        }
        return exchangeMessage;
    };

    CallParameters::UniquePtr exchangeMessage;
    auto waitAndDo = [&exchangeMessage](CallParameters::UniquePtr message) {
        exchangeMessage = std::move(message);
    };

    m_messageSwapper->spawnAndCall(spawnCall, waitAndDo);

    return exchangeMessage;
}

CallParameters::UniquePtr PromiseTransactionExecutive::suspendAndWait(
    CallParameters::UniquePtr input)
{
    CallParameters::UniquePtr output;
    m_messageSwapper->spawnAndCall([&input]() { return std::move(input); },
        [&output](CallParameters::UniquePtr message) { output = std::move(message); });
    return output;
}
