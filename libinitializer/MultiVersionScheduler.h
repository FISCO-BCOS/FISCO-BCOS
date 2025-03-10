#pragma once

#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include <boost/atomic/atomic_flag.hpp>

namespace bcos::transaction_scheduler
{

class MultiVersionScheduler : public bcos::scheduler::SchedulerInterface
{
private:
    std::array<scheduler::SchedulerInterface::Ptr, 2> m_schedulers;
    int m_currentIndex;

    bcos::scheduler::SchedulerInterface& getScheduler();

public:
    bcos::scheduler::SchedulerInterface& scheduler(int version);

    MultiVersionScheduler(std::array<scheduler::SchedulerInterface::Ptr, 2> schedulers);

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool sysBlock)>
            callback) override;

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr&&, ledger::LedgerConfig::Ptr&&)> callback) override;

    void status(
        [[maybe_unused]] std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)>
            callback) override;

    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)> callback) override;

    void reset([[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override;

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override;

    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify,
        [[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override;

    void stop() override;

    void setVersion(int version, ledger::LedgerConfig::Ptr ledgerConfig) override;
};
}  // namespace bcos::transaction_scheduler