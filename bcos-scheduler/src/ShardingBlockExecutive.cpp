#include "ShardingBlockExecutive.h"
#include "SchedulerImpl.h"
#include "ShardingDmcExecutor.h"
#include "ShardingGraphKeyLocks.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <tbb/parallel_for_each.h>

using namespace bcos::scheduler;
using namespace bcos::storage;

ShardingBlockExecutive::ShardingBlockExecutive(bcos::protocol::Block::Ptr block,
    SchedulerImpl* scheduler, size_t startContextID,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::txpool::TxPoolInterface::Ptr _txPool, std::shared_ptr<ShardCache> _contract2ShardCache,
    uint64_t _gasLimit, std::string& _gasPrice, bool _syncBlock, size_t _keyPageSize)
  : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
        _blockFactory, _txPool, _gasLimit, _gasPrice, _syncBlock),
    m_contract2ShardCache(_contract2ShardCache),
    m_keyPageSize(_keyPageSize)
{
    if (scheduler->ledgerConfig().features().get(ledger::Features::Flag::bugfix_dmc_revert))
    {
        auto shardingKeyLocks = std::make_shared<ShardingGraphKeyLocks>();
        shardingKeyLocks->setGetAddrHandler(
            [this](const std::string_view& addr) { return getContractShard(std::string(addr)); });
        m_keyLocks = shardingKeyLocks;
    }
}

void ShardingBlockExecutive::prepare()
{
    if (m_hasPrepared)
    {
        return;
    }

    auto breakPointT = utcTime();
    BlockExecutive::prepare();

    auto schedulerPrepareCost = utcTime() - breakPointT;
    breakPointT = utcTime();

    if (m_staticCall)
    {
        return;  // staticCall no need to preExecute
    }

    SCHEDULER_LOG(TRACE) << BLOCK_NUMBER(number()) << LOG_BADGE("Sharding")
                         << LOG_DESC("dmcExecutor try to preExecute");

    auto self = shared_from_this();
    tbb::parallel_for_each(m_dmcExecutors.begin(), m_dmcExecutors.end(),
        [self](auto const& executorIt) { executorIt.second->preExecute(); });
    SCHEDULER_LOG(TRACE) << BLOCK_NUMBER(number()) << LOG_BADGE("Sharding")
                         << LOG_DESC("ShardingBlockExecutive preExecute finish")
                         << LOG_KV("schedulerPrepareCost", schedulerPrepareCost)
                         << LOG_KV("executorsPrepareCost", utcTime() - breakPointT);
}

void ShardingBlockExecutive::asyncExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    if (m_result)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, "Invalid status"), nullptr,
            m_isSysBlock);
        return;
    }

    if (m_scheduler->executorManager()->size() == 0)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     SchedulerError::ExecutorNotEstablishedError, "The executor has not started!"),
            nullptr, m_isSysBlock);
    }
    m_currentTimePoint = std::chrono::system_clock::now();

    auto startT = utcTime();
    prepare();

    auto createMsgT = utcTime() - startT;
    startT = utcTime();
    if (!m_staticCall)
    {
        // sysBlock need to clear shard cache
        if (m_isSysBlock) [[unlikely]]
        {
            callback = [this, callback = std::move(callback)](Error::UniquePtr error,
                           protocol::BlockHeader::Ptr blockHeader, bool isSysBlock) {
                if (!error)
                {
                    SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number())
                                        << "Clear contract2ShardCache after sysBlock has executed";
                    m_contract2ShardCache->clear();  // sysBlock need to clear shard cache
                }
                callback(std::move(error), std::move(blockHeader), isSysBlock);
            };
        }

        // Execute nextBlock
        batchNextBlock([this, callback = std::move(callback)](Error::UniquePtr error) {
            if (!m_isRunning)
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                    nullptr, m_isSysBlock);
                return;
            }

            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << BLOCK_NUMBER(number()) << "Next block with error!" << error->errorMessage();
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             SchedulerError::NextBlockError, "Next block error!", *error),
                    nullptr, m_isSysBlock);
                return;
            }

            SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                                << LOG_DESC("ShardingExecute begin")
                                << LOG_KV("shardSize", m_dmcExecutors.size());
            shardingExecute(std::move(callback));
        });
    }
    else
    {
        SCHEDULER_LOG(TRACE) << BLOCK_NUMBER(number()) << LOG_BADGE("BlockTrace")
                             << LOG_DESC("ShardingExecute begin for call")
                             << LOG_KV("shardSize", m_dmcExecutors.size());
        shardingExecute(std::move(callback));
    }
}

void ShardingBlockExecutive::shardingExecute(
    std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback)
{
    if (m_dmcExecutors.empty())
    {
        // if empty block, use DMC logic to handle
        DMCExecute(std::move(callback));
        return;
    }

    auto batchStatus = std::make_shared<BatchStatus>();
    batchStatus->total = m_dmcExecutors.size();
    auto startT = utcTime();

    auto executorCallback = [this, startT, batchStatus, callback = std::move(callback)](
                                bcos::Error::UniquePtr error, DmcExecutor::Status status) {
        if (error || status == DmcExecutor::Status::ERROR)
        {
            batchStatus->error++;
            batchStatus->errorMessage = error.get()->errorMessage();
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number()) << LOG_BADGE("ShardingExecutor")
                                 << "shardGo() with error"
                                 << LOG_KV("code", error ? error->errorCode() : -1)
                                 << LOG_KV("msg", error ? error.get()->errorMessage() : "null");

            if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
            {
                SCHEDULER_LOG(ERROR)
                    << "shardGo() SCHEDULER_TERM_ID_ERROR:" << error->errorMessage()
                    << ". trigger switch";
                triggerSwitch();
            }
        }
        else
        {
            batchStatus->finished++;
        }

        // check batch
        if ((batchStatus->error + batchStatus->paused + batchStatus->finished) !=
            batchStatus->total)
        {
            return;
        }

        // block many threads
        if (batchStatus->callbackExecuted)
        {
            return;
        }
        {
            WriteGuard lock(batchStatus->x_lock);
            if (batchStatus->callbackExecuted)
            {
                return;
            }
            batchStatus->callbackExecuted = true;
        }

        if (!m_isRunning)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::Stopped, "BlockExecutive is stopped"),
                nullptr, m_isSysBlock);
            return;
        }

        if (batchStatus->error > 0)
        {
            auto message = "ShardingExecute:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(batchStatus->error);
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number()) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::DAGError, std::move(message)), nullptr,
                m_isSysBlock);
            return;
        }

        SCHEDULER_LOG(DEBUG) << BLOCK_NUMBER(number()) << LOG_DESC("ShardingExecute success")
                             << LOG_KV("shardingExecuteT", (utcTime() - startT));

        DMCExecute(std::move(callback));
    };

    std::map<std::string, std::shared_ptr<DmcExecutor>, std::less<>> dmcExecutors;
    {
        bcos::ReadGuard l(x_dmcExecutorLock);
        // copy to another object for m_dmcExecutors may change during parallel shardGo
        dmcExecutors = m_dmcExecutors;
    }

    tbb::parallel_for_each(
        dmcExecutors.begin(), dmcExecutors.end(), [&executorCallback](auto const& executorIt) {
            std::dynamic_pointer_cast<ShardingDmcExecutor>(executorIt.second)
                ->shardGo(executorCallback);
        });
}

std::shared_ptr<DmcExecutor> ShardingBlockExecutive::registerAndGetDmcExecutor(
    std::string contractAddress)
{
    auto shardName = getContractShard(contractAddress);
    auto dmcExecutor = BlockExecutive::registerAndGetDmcExecutor(shardName);
    dmcExecutor->setGetAddrHandler(
        [this](const std::string_view& addr) { return getContractShard(std::string(addr)); });
    return dmcExecutor;
}

DmcExecutor::Ptr ShardingBlockExecutive::buildDmcExecutor(const std::string& name,
    const std::string& contractAddress,
    bcos::executor::ParallelTransactionExecutorInterface::Ptr executor)
{
    auto dmcExecutor = std::make_shared<ShardingDmcExecutor>(name, contractAddress, m_block,
        executor, m_keyLocks, m_hashImpl, m_dmcRecorder, m_schedulerTermId, isCall());
    return dmcExecutor;
}

inline std::string getContractTableName(const std::string_view& _address)
{
    constexpr static std::string_view prefix("/apps/");
    std::string out;
    if (_address[0] == '/')
    {
        out.reserve(prefix.size() + _address.size() - 1);
        std::copy(prefix.begin(), prefix.end(), std::back_inserter(out));
        std::copy(_address.begin() + 1, _address.end(), std::back_inserter(out));
    }
    else
    {
        out.reserve(prefix.size() + _address.size());
        std::copy(prefix.begin(), prefix.end(), std::back_inserter(out));
        std::copy(_address.begin(), _address.end(), std::back_inserter(out));
    }

    return out;
}

std::string ShardingBlockExecutive::getContractShard(const std::string& contractAddress)
{
    if (contractAddress.length() == 0)
    {
        return {};
    }

    if (!m_storageWrapper.has_value())
    {
        storage::StateStorageInterface::Ptr stateStorage;
        if (m_keyPageSize > 0)
        {
            stateStorage = std::make_shared<bcos::storage::KeyPageStorage>(getStorage(), false,
                m_keyPageSize, m_block->blockHeaderConst()->version(), nullptr, true);
        }
        else
        {
            stateStorage = std::make_shared<bcos::storage::StateStorage>(getStorage(), false);
        }


        auto recorder = std::make_shared<Recoder>();
        m_storageWrapper.emplace(stateStorage, recorder);
    }

    std::string shardName;
    ShardCache::WriteAccessor::Ptr accessor;
    bool has = m_contract2ShardCache->find<ShardCache::WriteAccessor>(accessor, contractAddress);
    if (has)
    {
        shardName = accessor->value();
    }
    else
    {
        auto tableName = getContractTableName(contractAddress);
        shardName = ContractShardUtils::getContractShard(m_storageWrapper.value(), tableName);
        m_contract2ShardCache->insert(accessor, {contractAddress, shardName});
        DMC_LOG(DEBUG) << LOG_BADGE("Sharding")
                       << "Update shard cache: " << LOG_KV("contractAddress", contractAddress)
                       << LOG_KV("shardName", shardName);
    }

    return shardName;
}
