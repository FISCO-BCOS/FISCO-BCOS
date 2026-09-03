#include "MultiVersionScheduler.h"
#include "Common.h"
#include <algorithm>

bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::getScheduler()
{
    return *m_schedulers.at(m_currentIndex);
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
void bcos::scheduler_v1::MultiVersionScheduler::stop()
{
    auto& scheduler = getScheduler();
    scheduler.stop();
}
void bcos::scheduler_v1::MultiVersionScheduler::setVersion(
    int version, ledger::LedgerConfig::Ptr ledgerConfig)
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
    // Saturate the upper bound: any version >= the last scheduler index selects the
    // newest executor (the v3 OP scheduler in OP mode, else the v2 EthereumExecutor).
    // This keeps the version space open-ended above the top slot so a future executor
    // version needs no array/schema change.
    if (static_cast<size_t>(version) >= m_schedulers.size())
    {
        INITIALIZER_LOG(WARNING) << LOG_DESC(
                                        "executor version above the newest known executor; "
                                        "saturating to the newest")
                                 << LOG_KV("requested", version)
                                 << LOG_KV("selected", m_schedulers.size() - 1);
    }
    m_currentIndex =
        static_cast<int>(std::min<size_t>(static_cast<size_t>(version), m_schedulers.size() - 1));
}
bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::scheduler(
    int version)
{
    return *m_schedulers.at(version);
}
