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
/** @file BlockVerifier.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */
#include "BlockVerifier.h"
#include "ExecutiveContext.h"
#include <libethcore/Exceptions.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/ExecutionResult.h>
#include <libexecutive/Executive.h>
#include <exception>
using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::executive;

ExecutiveContext::Ptr BlockVerifier::executeBlock(Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << "[#executeBlock] [txNum/num/parentHash/parentNum/parentStateRoot]:"
                            << "[ " << block.transactions().size() << "/ "
                            << block.blockHeader().number() << "/ " << parentBlockInfo.hash << "/ "
                            << parentBlockInfo.number << "/ " << parentBlockInfo.stateRoot;

    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            parentBlockInfo, parentBlockInfo.stateRoot, executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << "[#executeBlock] Error during initExecutiveContext [errorMsg]: "
                                 << boost::diagnostic_information(e);

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    unsigned i = 0;
    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    for (Transaction const& tr : block.transactions())
    {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash,
            block.getTransactionReceipts().size() > 0 ?
                block.getTransactionReceipts().back().gasUsed() :
                0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, tr, OnOpFunc(), executiveContext);
        block.appendTransactionReceipt(resultReceipt.second);
    }
    executiveContext->getState()->commit();
    block.calReceiptRoot();
    block.header().setStateRoot(executiveContext->getState()->rootHash());
    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    return executiveContext;
}

std::pair<ExecutionResult, TransactionReceipt> BlockVerifier::executeTransaction(
    const BlockHeader& blockHeader, dev::eth::Transaction const& _t)
{
    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    BlockInfo blockInfo{blockHeader.hash(), blockHeader.number(), blockHeader.stateRoot()};
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            blockInfo, blockHeader.stateRoot(), executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR)
            << "[#executeTransaction] Error during execute initExecutiveContext [errorMsg]: "
            << boost::diagnostic_information(e);
    }

    EnvInfo envInfo(blockHeader, m_pNumberHash, 0);
    envInfo.setPrecompiledEngine(executiveContext);
    return execute(envInfo, _t, OnOpFunc(), executiveContext);
}

std::pair<ExecutionResult, TransactionReceipt> BlockVerifier::execute(EnvInfo const& _envInfo,
    Transaction const& _t, OnOpFunc const& _onOp, ExecutiveContext::Ptr executiveContext)
{
    auto onOp = _onOp;
#if ETH_VMTRACE
    if (isChannelVisible<VMTraceChannel>())
        onOp = Executive::simpleTrace();  // override tracer
#endif

    // Create and initialize the executive. This will throw fairly cheaply and quickly if the
    // transaction is bad in any way.
    Executive e(executiveContext->getState(), _envInfo);
    ExecutionResult res;
    e.setResultRecipient(res);
    e.initialize(_t);

    // OK - transaction looks valid - execute.
    u256 startGasUsed = _envInfo.gasUsed();
    if (!e.execute())
        e.go(onOp);
    e.finalize();

    return make_pair(res,
        TransactionReceipt(executiveContext->getState()->rootHash(), startGasUsed + e.gasUsed(),
            e.logs(), e.status(), e.takeOutput().takeBytes(), e.newAddress()));
}
