//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ShardingTransactionExecutive.h"
#include "ShardingSyncStorageWrapper.h"
#include "bcos-table/src/ContractShardUtils.h"
#include <range/v3/algorithm/all_of.hpp>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/all.hpp>

using namespace bcos::executor;
using namespace bcos::storage;

TransactionExecutive::Ptr ShardingTransactionExecutive::buildChildExecutive(
    const std::string& _contractAddress, int64_t contextID, int64_t seq)
{
    ShardingExecutiveFactory executiveFactory = ShardingExecutiveFactory(
        m_blockContext, m_evmPrecompiled, m_precompiled, m_staticPrecompiled, m_gasInjector);

    if (m_blockContext.features().get(
            ledger::Features::Flag::bugfix_sharding_call_in_child_executive))
    {
        return executiveFactory.buildChild(this, _contractAddress, contextID, seq);
    }

    return executiveFactory.build(_contractAddress, contextID, seq, ExecutiveType::common);
}

bool ShardingTransactionExecutive::isUsePromise() const
{
    return m_usePromise;
}

ShardingTransactionExecutive::ShardingTransactionExecutive(const BlockContext& blockContext,
    std::string contractAddress, int64_t contextID, int64_t seq,
    const wasm::GasInjector& gasInjector, ThreadPool::Ptr pool, bool usePromise)
  : PromiseTransactionExecutive(
        pool, std::move(blockContext), std::move(contractAddress), contextID, seq, gasInjector),
    m_usePromise(usePromise)
{
    if (m_blockContext.features().get(
            ledger::Features::Flag::bugfix_sharding_call_in_child_executive))
    {
        m_syncStorageWrapper = std::make_shared<ShardingSyncStorageWrapper>(
            std::make_shared<ShardingKeyLocks>(), m_blockContext.storage(),
            m_syncStorageWrapper->takeExternalAcquireKeyLocks(), m_recoder);
        m_storageWrapper = m_syncStorageWrapper.get();
        m_storageWrapper->setCodeCache(m_blockContext.getCodeCache());
        m_storageWrapper->setCodeHashCache(m_blockContext.getCodeHashCache());
    }
}

CallParameters::UniquePtr ShardingTransactionExecutive::start(CallParameters::UniquePtr input)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Sharding")
                             << "ShardingTransactionExecutive start: " << input->toFullString()
                             << LOG_KV("usePromise", m_usePromise);
    }

    CallParameters::UniquePtr ret;
    if (m_usePromise)
    {
        ret = PromiseTransactionExecutive::start(std::move(input));
    }
    else
    {
        ret = CoroutineTransactionExecutive::start(std::move(input));
    }
    ret->contextID = contextID();
    ret->seq = seq();
    return ret;
}

CallParameters::UniquePtr ShardingTransactionExecutive::externalCall(
    CallParameters::UniquePtr input)
{
    // set DMC contextID and seq
    input->contextID = contextID();
    input->seq = seq();

    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Sharding")
                             << "ShardingTransactionExecutive externalCall: "
                             << input->toFullString() << LOG_KV("usePromise", m_usePromise);
    }

    if (!std::empty(input->receiveAddress))
    {
        if (!m_shardName.has_value())
        {
            m_shardName = getContractShard(m_contractAddress);
        }

        std::string_view to = input->receiveAddress;

        if (m_blockContext.features().get(
                ledger::Features::Flag::bugfix_sharding_call_in_child_executive))
        {
            if (input->data == bcos::protocol::GET_CODE_INPUT_BYTES)
            {
                to = input->codeAddress;
            }
        }

        std::string toShardName = getContractShard(to);

        if (toShardName != m_shardName.value())
        {
            EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Sharding")
                                 << "ShardingTransactionExecutive call other shard: "
                                 << LOG_KV("toShard", toShardName)
                                 << LOG_KV("usePromise", m_usePromise)
                                 << LOG_KV("input", input->toFullString());
            if (m_usePromise)
            {
                return PromiseTransactionExecutive::externalCall(std::move(input));
            }
            else
            {
                return CoroutineTransactionExecutive::externalCall(std::move(input));
            }
        }
    }

    EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Sharding") << "ShardingTransactionExecutive call local"
                         << input->toFullString();
    return TransactionExecutive::externalCall(std::move(input));
}

CallParameters::UniquePtr ShardingTransactionExecutive::resume()
{
    if (m_usePromise)
    {
        return PromiseTransactionExecutive::resume();
    }
    else
    {
        return CoroutineTransactionExecutive::resume();
    }
}

std::string ShardingTransactionExecutive::getContractShard(const std::string_view& contractAddress)
{
    auto tableName = getContractTableName(contractAddress, m_blockContext.isWasm());
    return ContractShardUtils::getContractShard(storage(), tableName);
}

CallParameters::UniquePtr ShardingChildTransactionExecutive::start(CallParameters::UniquePtr input)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Sharding")
                             << "ShardingChildTransactionExecutive start: "
                             << input->toFullString();
    }

    return TransactionExecutive::start(std::move(input));
}

CallParameters::UniquePtr ShardingChildTransactionExecutive::externalCall(
    CallParameters::UniquePtr input)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Sharding")
                             << "ShardingChildTransactionExecutive externalCall: "
                             << input->toFullString();
    }

    return ShardingTransactionExecutive::externalCall(std::move(input));
}

std::optional<CoroutineTransactionExecutive::Coroutine::pull_type>&
ShardingChildTransactionExecutive::getPullMessage()
{
    return m_pullMessageRef;
}

std::optional<CoroutineTransactionExecutive::Coroutine::push_type>&
ShardingChildTransactionExecutive::getPushMessage()
{
    return m_pushMessageRef;
}

CallParameters::UniquePtr& ShardingChildTransactionExecutive::getExchangeMessageRef()
{
    return m_exchangeMessageRef;
}

ShardingChildTransactionExecutive::ShardingChildTransactionExecutive(
    ShardingTransactionExecutive* parent, const BlockContext& blockContext,
    std::string contractAddress, int64_t contextID, int64_t seq,
    const wasm::GasInjector& gasInjector, ThreadPool::Ptr pool, bool usePromise)
  : ShardingTransactionExecutive(
        blockContext, contractAddress, contextID, seq, gasInjector, pool, usePromise),

    // for coroutine
    m_pullMessageRef(parent->getPullMessage()),
    m_pushMessageRef(parent->getPushMessage()),
    m_exchangeMessageRef(parent->getExchangeMessageRef())
{
    auto parentKeyLocks =
        dynamic_pointer_cast<ShardingSyncStorageWrapper>(parent->getSyncStorageWrapper())
            ->getKeyLocks();

    m_syncStorageWrapper = std::make_shared<ShardingSyncStorageWrapper>(parentKeyLocks,
        m_blockContext.storage(), m_syncStorageWrapper->takeExternalAcquireKeyLocks(), m_recoder);
    m_storageWrapper = m_syncStorageWrapper.get();
    m_storageWrapper->setCodeCache(m_blockContext.getCodeCache());
    m_storageWrapper->setCodeHashCache(m_blockContext.getCodeHashCache());

    // for promise executive
    setPromiseMessageSwapper(parent->getPromiseMessageSwapper());
}