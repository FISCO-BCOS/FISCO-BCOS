#pragma once

#include <bcos-framework/ledger/IL2ConfigLoader.h>
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
/// Three things here are load-bearing; all are pinned by
/// engine/test/unittests/engine/OpLedgerConfigRepublishTest.cpp.
///
///   * The published configuration is read from the LEDGER, never taken from the scheduler's
///     commit callback. OpScheduler::loadCommitLedgerConfig builds a LedgerConfig carrying only
///     setBlockNumber + setTimestamp -- chainId() nullopt, features() empty, executorVersion()
///     0 -- so publishing the callback's object refuses every EIP-155 envelope from the first
///     committed block on with -32602 "invalid chain id for signer".
///   * On the OP lane the sealing parameters come from the SystemConfig predeploy
///     (0x43...00c0), not from the SYS_CONFIG rows getLedgerConfig reads. When an
///     IL2ConfigLoader is supplied, the ledger's config is passed through it before it is
///     published, evaluated at committed + 1 -- the block the snapshot will be used to seal --
///     so a governance write with enableNumber <= next block takes effect on the next block,
///     the same basis getLedgerConfig uses for SYS_CONFIG (blockNumber + 1).
///   * A failed read must not escape. OpScheduler invokes the notifier from inside
///     coCommitBlock, after mergeBackStorage has already succeeded, so a throw would report an
///     already-durable block as a failed commit and leave op-node retrying a block that is on
///     disk. Keeping the previous snapshot for one block is the lesser failure. The loader's
///     own failures (missing key, malformed slot) are reported the same way; at boot the
///     initializer turns that report into a startup refusal instead.

/// Reads the full configuration from @p ledger, overlays the SystemConfig keys through
/// @p l2Loader when one is given, and publishes the result into @p holder; returns nullptr on
/// success, or the error that stopped it (holder left with the previous snapshot).
///
/// The read is an inline sequence of point reads -- LedgerMethods.cpp's getLedgerConfig calls
/// getNodeList, getCurrentBlockNumber, fetchAllSystemConfigs (at blockNumber + 1, the effective
/// basis), the block header, the number->hash row and getFeatures -- and every one of them
/// bottoms out in StorageInterface::asyncGetRow on the ledger's state storage, invoked on the
/// calling thread, so syncWait's slow path is never entered and the commit thread is not handed
/// off. The loader adds one batched readSome of four slots on the committed state. On a remote
/// storage backend those are network round trips: "inline" means "on this thread", not "cheap".
inline bcos::Error::Ptr republishLedgerConfig(bcos::ledger::LedgerConfigState& holder,
    bcos::ledger::LedgerInterface& ledger, bcos::ledger::IL2ConfigLoader* l2Loader = nullptr)
{
    try
    {
        auto config = task::syncWait(ledger::getLedgerConfig(ledger));
        if (l2Loader != nullptr)
        {
            task::syncWait(l2Loader->loadIntoLedgerConfig(config->blockNumber() + 1, *config));
        }
        holder.set(std::move(config));
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
/// the previous snapshot. @p l2Loader (may be null) overlays the SystemConfig keys on every
/// republish; the initializer passes the same loader it used for the boot load.
inline BlockNumberNotifier makeOpLedgerConfigRepublisher(
    bcos::ledger::LedgerConfigState::Ptr holder,
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger,
    std::function<void(bcos::protocol::BlockNumber, bcos::Error::Ptr)> onFailure,
    bcos::ledger::IL2ConfigLoader::Ptr l2Loader = nullptr)
{
    return
        [holder = std::move(holder), ledger = std::move(ledger), onFailure = std::move(onFailure),
            l2Loader = std::move(l2Loader)](bcos::protocol::BlockNumber number) {
            if (auto error = republishLedgerConfig(*holder, *ledger, l2Loader.get()))
            {
                onFailure(number, std::move(error));
            }
        };
}
}  // namespace bcos::engine
