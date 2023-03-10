#include "ShardingBlockExecutive.h"
#include "SchedulerImpl.h"
#include "ShardingDmcExecutor.h"
#include <bcos-table/src/KeyPageStorage.h>
#include <tbb/parallel_for_each.h>

using namespace bcos::scheduler;
using namespace bcos::storage;

void ShardingBlockExecutive::prepare()
{
    if (m_hasPrepared)
    {
        return;
    }
    BlockExecutive::prepare();

    if (m_staticCall)
    {
        return;  // staticCall no need to preExecute
    }

    SCHEDULER_LOG(TRACE) << BLOCK_NUMBER(number()) << LOG_BADGE("Sharding")
                         << LOG_DESC("dmcExecutor try to preExecute");


    tbb::parallel_for_each(m_dmcExecutors.begin(), m_dmcExecutors.end(),
        [&](auto const& executorIt) { executorIt.second->preExecute(); });
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
                             << LOG_DESC("DMCExecute begin for call")
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
            auto message = "DAGExecute:" + boost::lexical_cast<std::string>(number()) +
                           " with errors! " + boost::lexical_cast<std::string>(batchStatus->error);
            SCHEDULER_LOG(ERROR) << BLOCK_NUMBER(number()) << message;

            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::DAGError, std::move(message)), nullptr,
                m_isSysBlock);
            return;
        }

        SCHEDULER_LOG(INFO) << BLOCK_NUMBER(number()) << LOG_DESC("DAGExecute success")
                            << LOG_KV("dagExecuteT", (utcTime() - startT));

        DMCExecute(std::move(callback));
    };

    tbb::parallel_for_each(
        m_dmcExecutors.begin(), m_dmcExecutors.end(), [&executorCallback](auto const& executorIt) {
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
        executor, m_keyLocks, m_hashImpl, m_dmcRecorder, m_schedulerTermId);
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
        // TODO: config using
        auto stateStorage = std::make_shared<bcos::storage::KeyPageStorage>(
            getStorage(), 10240, m_block->blockHeaderConst()->version(), nullptr, true);
        // auto stateStorage = std::make_shared<StateStorage>(getStorage());
        auto recorder = std::make_shared<Recoder>();
        m_storageWrapper.emplace(stateStorage, recorder);
    }

    auto shard = m_contract2Shard.find(contractAddress);
    std::string shardName;

    if (shard == m_contract2Shard.end())
    {
        WriteGuard l(x_contract2Shard);
        shard = m_contract2Shard.find(contractAddress);
        if (shard == m_contract2Shard.end())
        {
            auto tableName = getContractTableName(contractAddress);
            shardName = ContractShardUtils::getContractShard(m_storageWrapper.value(), tableName);
            m_contract2Shard.emplace(contractAddress, shardName);

            DMC_LOG(DEBUG) << LOG_BADGE("Sharding")
                           << "Update shard cache: " << LOG_KV("contractAddress", contractAddress)
                           << LOG_KV("shardName", shardName);
        }
        else
        {
            shardName = shard->second;
        }
    }
    else
    {
        shardName = shard->second;
    }

    return shardName;
}
