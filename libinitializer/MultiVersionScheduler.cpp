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
void bcos::scheduler_v1::MultiVersionScheduler::adoptProbeAsPending(
    bcos::protocol::Block::Ptr block,
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.adoptProbeAsPending(std::move(block), std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::stop()
{
    auto& scheduler = getScheduler();
    scheduler.stop();
}
void bcos::scheduler_v1::MultiVersionScheduler::setVersion(
    int version, [[maybe_unused]] ledger::LedgerConfig::Ptr ledgerConfig)
{
    // Runtime callers are the two commit callbacks (LedgerStorage::onStableCheckPointCommitted
    // and DownloadingQueue), which catch-and-log a throw and then stop advancing. A governance
    // tx that writes an unwired executor_version must therefore NOT make this throw: an unwired
    // slot (e.g. OP executor on a non-OP node) would otherwise halt the chain permanently.
    // Keep running on a wired scheduler and make the misconfiguration loud. The hard failure
    // for executor_version>=3 without the OP wiring belongs at boot (Initializer::init).
    auto const onChainVersion = ledgerConfig && ledgerConfig->executorVersion() > 0 ?
                                    ledgerConfig->executorVersion() :
                                    version;
    if (version < 0)
    {
        BOOST_THROW_EXCEPTION(ExecutorVersionNotSupported()
                              << errinfo_comment("executor version " + std::to_string(version) +
                                                 " is not supported (must be >= 0)"));
    }
    auto selected = static_cast<size_t>(version);
    if (selected >= m_schedulers.size())
    {
        // Unknown version above the wired set: saturate to the newest wired slot, as before.
        selected = m_schedulers.size() - 1;
        while (selected > 0 && !m_schedulers.at(selected))
        {
            --selected;
        }
    }
    if (!m_schedulers.at(selected))
    {
        INITIALIZER_LOG(ERROR)
            << LOG_DESC("executor_version has no wired scheduler; keeping the current executor")
            << LOG_KV("requested", version) << LOG_KV("onChain", onChainVersion)
            << LOG_KV("keeping", m_currentIndex)
            << LOG_DESC(
                   "align genesis/boot config with on-chain executor_version or enable the "
                   "matching engine wiring (OP mode / single-node consensus)");
        if (onChainVersion != m_currentIndex)
        {
            INITIALIZER_LOG(ERROR)
                << LOG_DESC("executor_version drift: on-chain config != runtime executor")
                << LOG_KV("onChain", onChainVersion) << LOG_KV("runtime", m_currentIndex);
        }
        return;
    }
    if (selected != static_cast<size_t>(version))
    {
        INITIALIZER_LOG(ERROR) << LOG_DESC(
                                      "executor_version above the wired set; running the newest "
                                      "wired executor")
                               << LOG_KV("requested", version) << LOG_KV("selected", selected)
                               << LOG_KV("onChain", onChainVersion);
    }
    auto const previousIndex = m_currentIndex;
    m_currentIndex = static_cast<int>(selected);
    if (previousIndex != m_currentIndex && onChainVersion == m_currentIndex)
    {
        // The drift log below keys on onChainVersion != m_currentIndex, so a governance tx
        // that downgrades executor_version to a WIRED lower slot (e.g. 3 -> 2 on an OP-wired
        // node) is otherwise silent: consensus commits move to the generic lane while the
        // engine service keeps answering on its wired engine. Make the switch loud.
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
            << LOG_KV("onChain", onChainVersion) << LOG_KV("runtime", m_currentIndex)
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
