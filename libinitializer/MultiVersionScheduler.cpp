#include "MultiVersionScheduler.h"

bcos::scheduler::SchedulerInterface&
bcos::scheduler_v1::MultiVersionScheduler::getScheduler()
{
    return *m_schedulers.at(m_currentIndex);
}

bcos::scheduler_v1::MultiVersionScheduler::MultiVersionScheduler(
    std::array<scheduler::SchedulerInterface::Ptr, 2> schedulers)
  : m_schedulers(std::move(schedulers)), m_currentIndex(0)
{}

void bcos::scheduler_v1::MultiVersionScheduler::executeBlock(
    bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool sysBlock)>
        callback)
{
    auto& scheduler = getScheduler();
    scheduler.executeBlock(std::move(block), verify, std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::commitBlock(
    protocol::BlockHeader::Ptr header,
    std::function<void(Error::Ptr&&, ledger::LedgerConfig::Ptr&&)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.commitBlock(std::move(header), std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::status(
    [[maybe_unused]] std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)>
        callback)
{
    auto& scheduler = getScheduler();
    scheduler.status(std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::call(
    protocol::Transaction::Ptr transaction,
    std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback)
{
    auto& scheduler = getScheduler();
    scheduler.call(std::move(transaction), std::move(callback));
}
void bcos::scheduler_v1::MultiVersionScheduler::reset(
    [[maybe_unused]] std::function<void(Error::Ptr&&)> callback)
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
void bcos::scheduler_v1::MultiVersionScheduler::preExecuteBlock(
    [[maybe_unused]] bcos::protocol::Block::Ptr block, [[maybe_unused]] bool verify,
    [[maybe_unused]] std::function<void(Error::Ptr&&)> callback)
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
    m_currentIndex = version;
};
bcos::scheduler::SchedulerInterface& bcos::scheduler_v1::MultiVersionScheduler::scheduler(
    int version)
{
    return *m_schedulers.at(version);
}
