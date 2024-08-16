#pragma once
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/TransactionSubmitResultFactory.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-tool/NodeConfig.h"
#include <rocksdb/db.h>
#include <memory>

namespace bcos::transaction_scheduler
{
class BaselineSchedulerInitializer
{
public:
    static bcos::task::Task<void> checkRequirements(
        bcos::ledger::LedgerInterface& ledger, bool dmc, bool wasm);

    static std::tuple<std::function<std::shared_ptr<scheduler::SchedulerInterface>()>,
        std::function<void(std::function<void(protocol::BlockNumber)>)>>
    build(::rocksdb::DB& rocksDB, std::shared_ptr<protocol::BlockFactory> blockFactory,
        std::shared_ptr<txpool::TxPoolInterface> txpool,
        std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory,
        std::shared_ptr<ledger::LedgerInterface> ledger,
        tool::NodeConfig::BaselineSchedulerConfig const& config);
};
}  // namespace bcos::transaction_scheduler