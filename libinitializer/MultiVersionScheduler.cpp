#include "MultiVersionScheduler.h"
#include "Common.h"

bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::checkedSchedulerAt(
    int version) const
{
    if (version < 0)
    {
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported()
                              << errinfo_comment("executor version " + std::to_string(version) +
                                                 " is not supported (must be >= 0)"));
    }
    if (static_cast<size_t>(version) >= m_schedulers.size())
    {
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported()
                              << errinfo_comment("executor version " + std::to_string(version) +
                                                 " is not supported (max wired slot is " +
                                                 std::to_string(m_schedulers.size() - 1) + ")"));
    }
    auto const& scheduler = m_schedulers.at(static_cast<size_t>(version));
    if (!scheduler)
    {
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported() << errinfo_comment(
                                  "executor_version " + std::to_string(version) +
                                  " requires a wired scheduler at slot " + std::to_string(version) +
                                  " but none was assembled at node startup"));
    }
    return *scheduler;
}

bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::getScheduler()
{
    return checkedSchedulerAt(m_currentIndex.load());
}

bcos::scheduler_v1::MultiVersionScheduler::MultiVersionScheduler(
    std::array<scheduler::SchedulerInterface::Ptr, SUPPORTED_EXECUTOR_VERSION_COUNT> schedulers,
    ledger::LedgerConfigState::Ptr ledgerConfigState)
  : m_schedulers(std::move(schedulers)),
    m_currentIndex(0),
    m_ledgerConfigState(std::move(ledgerConfigState))
{}

void bcos::scheduler_v1::MultiVersionScheduler::executeBlock(bcos::protocol::Block::Ptr block,
    bool verify,
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool sysBlock)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.executeBlock(std::move(block), verify, std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::commitBlock(protocol::BlockHeader::Ptr header,
    std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr)> callback)
{
    auto& scheduler = getScheduler();
    // Every scheduler refetches the configuration as its last commit step and hands it back
    // through this callback. Published before the caller learns the commit succeeded, so nothing
    // sealed on top of this block is admitted against the previous one. The holder is captured
    // by value: this callback outlives the call, and the dispatcher is not needed for it.
    scheduler.commitBlock(
        std::move(header), [holder = m_ledgerConfigState, callback = std::move(callback)](
                               Error::Ptr error, ledger::LedgerConfig::Ptr ledgerConfig) {
            if (!error)
            {
                holder->set(ledgerConfig);
            }
            callback(std::move(error), std::move(ledgerConfig));
        });
}
void bcos::scheduler_v1::MultiVersionScheduler::status(
    [[maybe_unused]] std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.status(std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::call(protocol::Transaction::Ptr transaction,
    std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.call(std::move(transaction), std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::callAtBlock(protocol::Transaction::Ptr transaction,
    protocol::BlockNumber blockNumber,
    std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.callAtBlock(std::move(transaction), blockNumber, std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::adoptProbeAsPending(
    bcos::protocol::Block::Ptr block,
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.adoptProbeAsPending(std::move(block), std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::reset(
    [[maybe_unused]] std::function<void(Error::Ptr)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.reset(std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::getCode(
    std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.getCode(contract, std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::getABI(
    std::string_view contract, std::function<void(Error::Ptr, std::string)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.getABI(contract, std::move(callback));
}
bcos::task::Task<std::optional<bcos::storage::Entry>>
bcos::scheduler_v1::MultiVersionScheduler::getPendingStorageAt(
    std::string_view address, std::string_view key, bcos::protocol::BlockNumber number)
{
    auto& scheduler = getScheduler();
    return scheduler.getPendingStorageAt(address, key, number);
}
void bcos::scheduler_v1::MultiVersionScheduler::preExecuteBlock(
    [[maybe_unused]] bcos::protocol::Block::Ptr block, [[maybe_unused]] bool verify,
    [[maybe_unused]] std::function<void(Error::Ptr)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.preExecuteBlock(std::move(block), verify, std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::stop()
{
    // Every slot must be stopped, not just the active one: Initializer wires the same shared
    // MPT commit observer into each BaselineScheduler slot, and a slot's stop() is what
    // detaches it — an inactive slot left running would keep dereferencing the pruner while
    // Initializer::stop() drops it and tears down the storage backend beneath. Each slot's
    // stop() is idempotent and safe on a never-started slot (BaselineScheduler::stop() only
    // resets its observer under m_commitMutex; SchedulerManager::stop() short-circuits on
    // STOPPED and tolerates a null scheduler).
    // A slot can also be UNWIRED: Initializer publishes a null slot when the lane is not wired on
    // this node, so the sweep must skip nulls — an unwired slot has no observer to detach, and
    // calling through the null pointer is a virtual call on address 0.
    for (auto const& scheduler : m_schedulers)
    {
        if (scheduler)
        {
            scheduler->stop();
        }
    }
}
void bcos::scheduler_v1::MultiVersionScheduler::setVersion(
    int version, [[maybe_unused]] ledger::LedgerConfig::Ptr ledgerConfig)
{
    if (version < 0)
    {
        // BCOS exception (not std::out_of_range) so it stays within the codebase's
        // exception taxonomy and carries the same error-channel conventions.
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported()
                              << errinfo_comment("executor version " + std::to_string(version) +
                                                 " is not supported "
                                                 "(must be >= 0)"));
    }
    // OP mode is genesis-only (see scheduler_v1::validateOpModeGenesisOnly): a chain already
    // RUNNING the OP executor must not be moved off it by a governance write. Keyed on the
    // running slot, not on feature_l2_ethereum_compat — that flag is the ledger's L2 state
    // shape and the Eth lane may carry it as well, so keying on it would freeze (and
    // mislabel) an Eth-lane L2 chain. Both sides use the shared lane predicate: a requested
    // value above OPSTACK saturates back onto the OP slot below, so only a requested non-OP
    // value is a real "move off". Keep the current executor and log loudly instead of
    // switching: a throw here would halt the commit callbacks.
    if (bcos::ledger::isOpLaneVersion(m_currentIndex) && !bcos::ledger::isOpLaneVersion(version))
    {
        INITIALIZER_LOG(ERROR) << LOG_DESC(
                                      "executor_version change rejected: OP mode is genesis-frozen")
                               << LOG_KV("requested", version) << LOG_KV("keeping", m_currentIndex);
        return;
    }
    // The on-chain row is the drift reference in the logs below; the bare argument is the
    // fallback for the boot call, where Initializer::init passes a null config.
    auto const onChainVersion = ledgerConfig && ledgerConfig->executorVersion() > 0 ?
                                    ledgerConfig->executorVersion() :
                                    version;
    // Saturate the upper bound onto the newest WIRED slot: the version space stays
    // open-ended above the newest known executor, and an empty slot above it (OP wiring
    // absent on this node) never becomes the target.
    auto selected = static_cast<size_t>(version);
    if (selected >= m_schedulers.size())
    {
        selected = m_schedulers.size() - 1;
        while (selected > 0 && !m_schedulers.at(selected))
        {
            --selected;
        }
        INITIALIZER_LOG(ERROR) << LOG_DESC(
                                      "executor version above the newest wired executor; "
                                      "saturating to the newest wired one")
                               << LOG_KV("requested", version) << LOG_KV("selected", selected)
                               << LOG_KV("onChain", onChainVersion);
    }
    if (!m_schedulers.at(selected))
    {
        // Second line of defence, not the primary one: from compatibility_version 3.18.0 on,
        // SystemConfigPrecompiled rejects a governance write of executor_version >=
        // OPSTACK_EXECUTOR_VERSION outright (it is a genesis property), and that precompile is
        // the only RUNTIME writer of the row, so the unwired-slot 3 case is no longer reachable
        // from governance on a 3.18.0 chain. What remains reachable: an in-range slot a build
        // did not wire, replay of a pre-3.18.0 block that already wrote such a value, and a
        // chain still at compatibility_version < 3.18.0, which that guard does not cover.
        // Runtime callers here are the two commit
        // callbacks (LedgerStorage::onStableCheckPointCommitted and DownloadingQueue): both
        // catch-and-log a throw and then stop advancing, so throwing would halt the chain on a
        // value that is now node-local. Keep the current executor and make it loud instead; the
        // hard refusal for an unwired version belongs at boot (Initializer::init).
        INITIALIZER_LOG(ERROR)
            << LOG_DESC("executor_version has no wired scheduler; keeping the current executor")
            << LOG_KV("requested", version) << LOG_KV("onChain", onChainVersion)
            << LOG_KV("keeping", m_currentIndex.load());
        if (onChainVersion != m_currentIndex)
        {
            INITIALIZER_LOG(ERROR)
                << LOG_DESC("executor_version drift: on-chain config != runtime executor")
                << LOG_KV("onChain", onChainVersion) << LOG_KV("runtime", m_currentIndex.load());
        }
        return;
    }
    auto const previousIndex = m_currentIndex.load();
    m_currentIndex.store(static_cast<int>(selected));
    if (previousIndex != m_currentIndex && onChainVersion == m_currentIndex && ledgerConfig)
    {
        // The drift log below keys on onChainVersion != m_currentIndex, so a governance tx
        // that downgrades executor_version to a WIRED lower slot (e.g. 3 -> 2 on an OP-wired
        // node) is otherwise silent: consensus commits move to the generic lane while the
        // engine service keeps answering on its wired engine. Make the switch loud.
        // ledgerConfig gates out the boot call (Initializer.cpp passes a null config):
        // at boot previousIndex(0) -> wired is the initial selection, not a runtime switch,
        // and firing here would label every OP node's startup as a lane switch.
        INITIALIZER_LOG(WARNING)
            << LOG_DESC("executor_version switched at runtime: consensus commits moved lanes")
            << LOG_KV("from", previousIndex) << LOG_KV("to", m_currentIndex)
            << LOG_KV("onChain", onChainVersion)
            << LOG_DESC("the wired engine service still answers Engine API on its own scheduler");
    }
    if (onChainVersion != m_currentIndex)
    {
        INITIALIZER_LOG(ERROR)
            << LOG_DESC("executor_version drift: on-chain config != runtime executor")
            << LOG_KV("onChain", onChainVersion) << LOG_KV("runtime", m_currentIndex.load())
            << LOG_DESC(
                   "governance wrote a version this node cannot wire; blocks still execute on "
                   "the runtime executor above");
    }
}
bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::scheduler(
    int version)
{
    return checkedSchedulerAt(version);
}
