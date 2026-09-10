#include "MultiVersionScheduler.h"
#include "Common.h"

bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::checkedSchedulerAt(
    int version) const
{
    if (version < 0 || static_cast<size_t>(version) >= m_schedulers.size())
    {
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported()
                              << errinfo_comment("executor version " + std::to_string(version) +
                                                 " is out of range (wired slots: 0.." +
                                                 std::to_string(m_schedulers.size() - 1) + ")"));
    }
    auto const& scheduler = m_schedulers.at(static_cast<size_t>(version));
    if (!scheduler)
    {
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported()
                              << errinfo_comment("executor version " + std::to_string(version) +
                                                 " has no scheduler wired at node startup"));
    }
    return *scheduler;
}

bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::getScheduler()
{
    return checkedSchedulerAt(m_currentIndex);
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
    auto& scheduler = getScheduler();
    scheduler.stop();
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
                               << LOG_KV("requested", version) << LOG_KV("selected", selected);
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
            << LOG_KV("requested", version) << LOG_KV("keeping", m_currentIndex);
        return;
    }
    m_currentIndex = static_cast<int>(selected);
}
bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::scheduler(
    int version)
{
    return checkedSchedulerAt(version);
}
