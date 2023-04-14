#pragma once

#include "RollbackableStorage.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "vm/HostContext.h"
#include "vm/VMFactory.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-task/Task.h>
#include <boost/log/core/record.hpp>
#include <boost/throw_exception.hpp>

namespace bcos::transaction_executor
{

// clang-format off
struct NotFoundCode: public bcos::Error {};
// clang-format on

// Per call
template <storage2::Storage<storage::Entry> Storage>
class TransactionExecutive
{
public:
    task::Task<evmc_result> execute(const evmc_message& message)
    {
        // TODO:
        // 1: Check auth
        // 2: Check is precompiled
        // 3. Do something create or call

        // TODO: if revert, rollback the storage and clear the log
    }

    task::Task<EVMCResult> call(const evmc_message& message)
    {
        std::string tableName;  // TODO: calculate the table name
        RollbackableStorage<Storage> recordableStorage;
        auto codeEntry = m_storage.getRow(tableName, ACCOUNT_CODE);
        if (!codeEntry)
        {
            BOOST_THROW_EXCEPTION(NotFoundCode{} << bcos::Error::ErrorMessage(
                                      "Not found contract code: " + tableName));
        }
        auto code = codeEntry->get();

        HostContext<decltype(recordableStorage)> hostContext(
            m_blockHeader, message, recordableStorage, std::move(tableName),
            [this](const evmc_message& message) -> task::Task<evmc_result> {
                return execute(message);
            },
            [this](int64_t) -> task::Task<h256> { co_return {}; });

        auto vmInstance = VMFactory::create();
        auto mode = toRevision(hostContext.vmSchedule());

        auto result = vmInstance.execute(
            hostContext.interface, &hostContext, mode, &message, code.data(), code.size());
        if (result.m_evmcResult.status_code != 0)
        {
            // TODO: Rollback the state, clear the log
            recordableStorage.rollback();
        }

        co_return result;
    }

private:
    const protocol::BlockHeader& m_blockHeader;
    Storage& m_storage;
};
}  // namespace bcos::transaction_executor