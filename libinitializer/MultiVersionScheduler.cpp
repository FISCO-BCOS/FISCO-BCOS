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
    std::array<scheduler::SchedulerInterface::Ptr, SUPPORTED_EXECUTOR_VERSION_COUNT> schedulers)
  : m_schedulers(std::move(schedulers)), m_currentIndex(0)
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
    scheduler.commitBlock(std::move(header), std::move(callback));
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
    // tx that writes an unwired executor_version must therefore NOT make this throw: slot 3 is
    // null on every non-OP node, and one signed tx would otherwise halt the chain permanently.
    // Keep running on a wired scheduler and make the misconfiguration loud. The hard failure
    // for executor_version>=3 without the OP wiring belongs at boot, and lives in
    // Initializer::init's opStackMode gate.
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
            << LOG_KV("requested", version) << LOG_KV("keeping", m_currentIndex);
        return;
    }
    if (selected != static_cast<size_t>(version))
    {
        INITIALIZER_LOG(ERROR) << LOG_DESC(
                                      "executor_version above the wired set; running the newest "
                                      "wired executor")
                               << LOG_KV("requested", version) << LOG_KV("selected", selected);
    }
    m_currentIndex = static_cast<int>(selected);
}
bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::scheduler(
    int version)
{
    return checkedSchedulerAt(version);
}
