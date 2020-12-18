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
/** @file BlockVerifier.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "BlockVerifierInterface.h"
#include "ExecutiveContext.h"
#include "ExecutiveContextFactory.h"
#include "libprecompiled/Precompiled.h"
#include <libdevcrypto/Common.h>
#include <libexecutive/Executive.h>
#include <libprotocol/Block.h>
#include <libprotocol/Protocol.h>
#include <libprotocol/Transaction.h>
#include <libprotocol/TransactionReceipt.h>
#include <libutilities/FixedBytes.h>
#include <libutilities/ThreadPool.h>
#include <boost/function.hpp>
#include <algorithm>
#include <memory>
#include <thread>

namespace bcos
{
namespace protocol
{
class TransactionReceipt;

}  // namespace protocol

namespace blockverifier
{
class BlockVerifier : public BlockVerifierInterface,
                      public std::enable_shared_from_this<BlockVerifier>
{
public:
    typedef std::shared_ptr<BlockVerifier> Ptr;
    typedef boost::function<bcos::h256(int64_t x)> NumberHashCallBackFunction;
    BlockVerifier(bool _enableParallel = false) : m_enableParallel(_enableParallel)
    {
        if (_enableParallel)
        {
            m_threadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
        }
    }

    virtual ~BlockVerifier() {}

    ExecutiveContext::Ptr executeBlock(
        bcos::protocol::Block& block, BlockInfo const& parentBlockInfo);
    ExecutiveContext::Ptr serialExecuteBlock(
        bcos::protocol::Block& block, BlockInfo const& parentBlockInfo);
    ExecutiveContext::Ptr parallelExecuteBlock(
        bcos::protocol::Block& block, BlockInfo const& parentBlockInfo);


    bcos::protocol::TransactionReceipt::Ptr executeTransaction(
        const bcos::protocol::BlockHeader& blockHeader, bcos::protocol::Transaction::Ptr _t);

    bcos::protocol::TransactionReceipt::Ptr execute(bcos::protocol::Transaction::Ptr _t,
        bcos::blockverifier::ExecutiveContext::Ptr executiveContext,
        bcos::executive::Executive::Ptr executive);


    void setExecutiveContextFactory(ExecutiveContextFactory::Ptr executiveContextFactory)
    {
        m_executiveContextFactory = executiveContextFactory;
    }
    ExecutiveContextFactory::Ptr getExecutiveContextFactory() { return m_executiveContextFactory; }
    void setNumberHash(const NumberHashCallBackFunction& _pNumberHash)
    {
        m_pNumberHash = _pNumberHash;
    }

    bcos::executive::Executive::Ptr createAndInitExecutive(
        std::shared_ptr<executive::StateFace> _s, bcos::executive::EnvInfo const& _envInfo);
    void setEvmFlags(VMFlagType const& _evmFlags) { m_evmFlags = _evmFlags; }

private:
    ExecutiveContextFactory::Ptr m_executiveContextFactory;
    NumberHashCallBackFunction m_pNumberHash;
    bool m_enableParallel;
    unsigned int m_threadNum = -1;

    std::mutex m_executingMutex;
    std::atomic<int64_t> m_executingNumber = {0};

    VMFlagType m_evmFlags = 0;
};

}  // namespace blockverifier

}  // namespace bcos
