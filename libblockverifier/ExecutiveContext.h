/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file ExecutiveContext.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "Common.h"
#include "libprecompiled/Precompiled.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include <libexecutive/StateFace.h>
#include <libstorage/Table.h>
// for concurrent_map
#define TBB_PREVIEW_CONCURRENT_ORDERED_CONTAINERS 1
#include <tbb/concurrent_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace dev
{
namespace storage
{
class Table;
class Storage;
}  // namespace storage

namespace executive
{
class StateFace;
}
namespace precompiled
{
class Precompiled;
class PrecompiledExecResultFactory;
class ParallelConfigPrecompiled;
struct ParallelConfig;
}  // namespace precompiled
namespace blockverifier
{
class ExecutiveContext : public std::enable_shared_from_this<ExecutiveContext>
{
public:
    using Ptr = std::shared_ptr<ExecutiveContext>;
    using ParallelConfigKey = std::pair<Address, uint32_t>;
    ExecutiveContext() : m_addressCount(0x10000) {}
    virtual ~ExecutiveContext()
    {
        if (m_memoryTableFactory)
        {
            m_memoryTableFactory->commit();
        }
    };

    virtual dev::precompiled::PrecompiledExecResult::Ptr call(
        Address const& address, bytesConstRef param, Address const& origin, Address const& sender);

    virtual Address registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p);

    virtual bool isPrecompiled(Address address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(Address address) const;

    void setAddress2Precompiled(
        Address address, std::shared_ptr<precompiled::Precompiled> precompiled)
    {
        if (!precompiled->precompiledExecResultFactory())
        {
            precompiled->setPrecompiledExecResultFactory(m_precompiledExecResultFactory);
        }
        m_address2Precompiled.insert(std::make_pair(address, precompiled));
    }

    void registerParallelPrecompiled(std::shared_ptr<dev::precompiled::Precompiled> _precompiled);

    void setPrecompiledExecResultFactory(
        dev::precompiled::PrecompiledExecResultFactory::Ptr _precompiledExecResultFactory);

    BlockInfo const& blockInfo() { return m_blockInfo; }
    void setBlockInfo(BlockInfo blockInfo) { m_blockInfo = blockInfo; }

    std::shared_ptr<dev::executive::StateFace> getState();
    void setState(std::shared_ptr<dev::executive::StateFace> state);

    virtual bool isEthereumPrecompiled(Address const& _a) const;

    virtual std::pair<bool, bytes> executeOriginPrecompiled(
        Address const& _a, bytesConstRef _in) const;

    virtual bigint costOfPrecompiled(Address const& _a, bytesConstRef _in) const;

    void setPrecompiledContract(
        std::unordered_map<Address, dev::eth::PrecompiledContract> const& precompiledContract);

    void dbCommit(dev::eth::Block& block);

    void setMemoryTableFactory(std::shared_ptr<dev::storage::TableFactory> memoryTableFactory)
    {
        m_memoryTableFactory = std::move(memoryTableFactory);
    }

    std::shared_ptr<dev::storage::TableFactory> getMemoryTableFactory()
    {
        return m_memoryTableFactory;
    }

    uint64_t txGasLimit() const { return m_txGasLimit; }
    void setTxGasLimit(uint64_t _txGasLimit) { m_txGasLimit = _txGasLimit; }

    // Get transaction criticals, return nullptr if critical to all
    std::shared_ptr<std::vector<std::string>> getTxCriticals(const dev::eth::Transaction& _tx);

    std::shared_ptr<dev::storage::Storage> stateStorage();
    void setStateStorage(std::shared_ptr<dev::storage::Storage> _stateStorage);

    bool enableReconfirmCommittee() const { return m_enableReconfirmCommittee; }
    void setEnableReconfirmCommittee(bool _enableReconfirmCommittee)
    {
        m_enableReconfirmCommittee = _enableReconfirmCommittee;
    }

private:
    bool m_enableReconfirmCommittee;
    tbb::concurrent_unordered_map<Address, std::shared_ptr<precompiled::Precompiled>,
        std::hash<Address>>
        m_address2Precompiled;
    std::atomic<int> m_addressCount;
    BlockInfo m_blockInfo;
    std::shared_ptr<dev::executive::StateFace> m_stateFace;
    std::unordered_map<Address, dev::eth::PrecompiledContract> m_precompiledContract;
    std::shared_ptr<dev::storage::TableFactory> m_memoryTableFactory;
    uint64_t m_txGasLimit = 300000000;

    std::shared_ptr<dev::precompiled::PrecompiledExecResultFactory> m_precompiledExecResultFactory;
    std::shared_ptr<dev::precompiled::ParallelConfigPrecompiled> m_parallelConfigPrecompiled;

    // map between {receiveAddress, selector} to {ParallelConfig}
    // avoid multiple concurrent transactions of openTable to obtain ParallelConfig
    tbb::concurrent_map<ParallelConfigKey, std::shared_ptr<dev::precompiled::ParallelConfig>>
        m_parallelConfigCache;
    std::shared_ptr<dev::storage::Storage> m_stateStorage;
};

}  // namespace blockverifier

}  // namespace dev
