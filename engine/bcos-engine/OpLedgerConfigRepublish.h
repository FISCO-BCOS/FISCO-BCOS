#pragma once

#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerConfigState.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Error.h>
#include <functional>
#include <memory>
#include <utility>

namespace bcos::engine
{
/// Keeps the admission holder (LedgerConfigState) current on the OP lane. OpScheduler fires
/// the notifier built here after every durable commit, and transaction admission (TxValidator)
/// reads chainId/features/executorVersion from that holder and nowhere else.
///
/// Two things here are load-bearing; both are pinned by
/// engine/test/unittests/engine/OpLedgerConfigRepublishTest.cpp.
///
///   * The published configuration is read from the LEDGER, never taken from the scheduler's
///     commit callback. OpScheduler::loadCommitLedgerConfig builds a LedgerConfig carrying only
///     setBlockNumber + setTimestamp -- chainId() nullopt, features() empty, executorVersion()
///     0 -- so publishing the callback's object refuses every EIP-155 envelope from the first
///     committed block on with -32602 "invalid chain id for signer".
///   * A failed read must not escape. OpScheduler invokes the notifier from inside
///     coCommitBlock, after mergeBackStorage has already succeeded, so a throw would report an
///     already-durable block as a failed commit and leave op-node retrying a block that is on
///     disk. Keeping the previous snapshot for one block is the lesser failure.

/// Reads the full configuration from @p ledger and publishes it into @p holder; returns
/// nullptr on success, or the error that stopped it (holder left with the previous snapshot).
///
/// The read is an inline sequence of point reads -- LedgerMethods.cpp's getLedgerConfig calls
/// getNodeList, getCurrentBlockNumber, fetchAllSystemConfigs (at blockNumber + 1, the effective
/// basis), the block header, the number->hash row and getFeatures -- and every one of them
/// bottoms out in StorageInterface::asyncGetRow on the ledger's state storage, invoked on the
/// calling thread, so syncWait's slow path is never entered and the commit thread is not handed
/// off. On a TiKV backend those are network round trips: "inline" means "on this thread", not
/// "cheap". Bounded by a fixed number of point reads, and the same read boot runs to seed the
/// holder and that MultiVersionScheduler performs as its own last commit step.
inline bcos::Error::Ptr republishLedgerConfig(
    bcos::ledger::LedgerConfigState& holder, bcos::ledger::LedgerInterface& ledger)
{
    try
    {
        holder.set(task::syncWait(ledger::getLedgerConfig(ledger)));
        return nullptr;
    }
    catch (...)
    {
        return BCOS_ERROR_PTR(-1, std::string("republish ledger config after OP commit failed: ") +
                                      boost::current_exception_diagnostic_information());
    }
}

/// The notifier OpScheduler holds. It has ONE slot, so installing the RPC notifier later would
/// otherwise drop the republish.
using BlockNumberNotifier = std::function<void(bcos::protocol::BlockNumber)>;

/// Returns the installer the initializer uses in place of OpScheduler::setBlockNumberNotifier:
/// the returned callable takes the RPC notifier and installs both, republish first (the RPC
/// consumer may assume the holder is already current).
/// @p setNotifier is the delegate's setter, @p republish the notifier built by
/// makeOpLedgerConfigRepublisher.
inline std::function<void(BlockNumberNotifier)> composeOpBlockNumberNotifier(
    std::function<void(BlockNumberNotifier)> setNotifier, BlockNumberNotifier republish)
{
    return [setNotifier = std::move(setNotifier), republish = std::move(republish)](
               BlockNumberNotifier rpc) {
        setNotifier([republish, rpc = std::move(rpc)](bcos::protocol::BlockNumber number) {
            republish(number);
            rpc(number);
        });
    };
}

/// Builds the notifier the initializer installs on the OpScheduler delegate. A failed read is
/// reported through @p onFailure (which must not throw) instead of escaping; the holder keeps
/// the previous snapshot.
inline BlockNumberNotifier makeOpLedgerConfigRepublisher(
    bcos::ledger::LedgerConfigState::Ptr holder,
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger,
    std::function<void(bcos::protocol::BlockNumber, bcos::Error::Ptr)> onFailure)
{
    return [holder = std::move(holder), ledger = std::move(ledger),
               onFailure = std::move(onFailure)](bcos::protocol::BlockNumber number) {
        if (auto error = republishLedgerConfig(*holder, *ledger))
        {
            onFailure(number, std::move(error));
        }
    };
}
}  // namespace bcos::engine
