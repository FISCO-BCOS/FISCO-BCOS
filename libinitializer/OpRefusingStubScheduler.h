#pragma once

#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-utilities/Error.h"
#include <string>
#include <string_view>

namespace bcos::initializer
{
/// MultiVersionScheduler slot 3 when OP mode is off (executor_version < 3).
class OpRefusingStubScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    static constexpr std::string_view kRefuseMessage =
        "OpRefusingStubScheduler: OP scheduler not assembled (executor_version<3)";

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> cb) override
    {
        cb(BCOS_ERROR_PTR(
               bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)),
            nullptr, false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(
               bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)),
            nullptr);
    }
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(
               bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)),
            nullptr);
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(
            bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)));
    }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)> cb) override
    {
        cb(BCOS_ERROR_PTR(
               bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)),
            {});
    }
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)> cb) override
    {
        cb(BCOS_ERROR_PTR(
               bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)),
            {});
    }
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        throw *BCOS_ERROR_PTR(
            bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage));
        co_return std::nullopt;
    }
    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> cb) override
    {
        cb(BCOS_ERROR_PTR(
               bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)),
            nullptr);
    }
    void reset(std::function<void(bcos::Error::Ptr)> cb) override
    {
        cb(BCOS_ERROR_PTR(
            bcos::scheduler::SchedulerError::UnknownError, std::string(kRefuseMessage)));
    }
};
}  // namespace bcos::initializer
