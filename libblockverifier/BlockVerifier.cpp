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
#include "TxDAG.h"
#include <libethcore/Exceptions.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/ExecutionResult.h>
#include <libexecutive/Executive.h>
#include <libstorage/Table.h>
#include <tbb/parallel_for.h>
#include <exception>
#include <thread>

using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::executive;
using namespace dev::storage;

ExecutiveContext::Ptr BlockVerifier::executeBlock(Block& block, BlockInfo const& parentBlockInfo)
{
    if (g_BCOSConfig.version() >= RC2_VERSION && m_enableParallel)
    {
        return parallelExecuteBlock(block, parentBlockInfo);
    }
    else
    {
        return serialExecuteBlock(block, parentBlockInfo);
    }
}

ExecutiveContext::Ptr BlockVerifier::serialExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("hash", block.header().hash().abridged())
                            << LOG_KV("height", block.header().number())
                            << LOG_KV("receiptRoot", block.header().receiptsRoot())
                            << LOG_KV("stateRoot", block.header().stateRoot())
                            << LOG_KV("dbHash", block.header().dbHash())
                            << LOG_KV("parentHash", parentBlockInfo.hash.abridged())
                            << LOG_KV("parentNum", parentBlockInfo.number)
                            << LOG_KV("parentStateRoot", parentBlockInfo.stateRoot);

    uint64_t startTime = utcTime();

    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            parentBlockInfo, parentBlockInfo.stateRoot, executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_DESC("[#executeBlock] Error during initExecutiveContext")
                                 << LOG_KV("blkNum", block.blockHeader().number())
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());


    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Init env takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();

    try
    {
        for (size_t i = 0; i < block.transactions().size(); i++)
        {
            auto& tx = block.transactions()[i];
            EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
            envInfo.setPrecompiledEngine(executiveContext);
            std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
                execute(envInfo, tx, OnOpFunc(), executiveContext);
            block.setTransactionReceipt(i, resultReceipt.second);
            executiveContext->getState()->commit();
        }
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_BADGE("executeBlock")
                                 << LOG_DESC("Error during serial block execution")
                                 << LOG_KV("blkNum", block.blockHeader().number())
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            BlockExecutionFailed() << errinfo_comment("Error during serial block execution"));
    }


    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Run serial tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number());

    h256 stateRoot = executiveContext->getState()->rootHash();
    // set stateRoot in receipts
    // block.setStateRootToAllReceipt(stateRoot); no need
    block.updateSequenceReceiptGas();
    block.calReceiptRoot();
    block.header().setStateRoot(stateRoot);
    block.header().setDBhash(executiveContext->getMemoryTableFactory()->hash());
    /// if executeBlock is called by consensus module, no need to compare receiptRoot and stateRoot
    /// since origin value is empty if executeBlock is called by sync module, need to compare
    /// receiptRoot, stateRoot and dbHash
    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BLOCKVERIFIER_LOG(ERROR)
                << "Invalid Block with bad stateRoot or receiptRoot or dbHash"
                << LOG_KV("blkNum", block.blockHeader().number())
                << LOG_KV("originHash", tmpHeader.hash().abridged())
                << LOG_KV("curHash", block.header().hash().abridged())
                << LOG_KV("orgReceipt", tmpHeader.receiptsRoot().abridged())
                << LOG_KV("curRecepit", block.header().receiptsRoot().abridged())
                << LOG_KV("orgState", tmpHeader.stateRoot().abridged())
                << LOG_KV("curState", block.header().stateRoot().abridged())
                << LOG_KV("orgDBHash", tmpHeader.dbHash().abridged())
                << LOG_KV("curDBHash", block.header().dbHash().abridged());
            BOOST_THROW_EXCEPTION(
                InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                    "Invalid Block with bad stateRoot or ReceiptRoot, orgBlockHash " +
                    block.header().hash().abridged()));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Execute block takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot());
    return executiveContext;
}

ExecutiveContext::Ptr BlockVerifier::parallelExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)

{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
                            << LOG_KV("parentNum", parentBlockInfo.number)
                            << LOG_KV("parentStateRoot", parentBlockInfo.stateRoot);

    auto start_time = utcTime();
    auto record_time = utcTime();
    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            parentBlockInfo, parentBlockInfo.stateRoot, executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_DESC("[#executeBlock] Error during initExecutiveContext")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    auto memoryTableFactory = executiveContext->getMemoryTableFactory();

    auto initExeCtx_time_cost = utcTime() - record_time;
    record_time = utcTime();

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());
    auto perpareBlock_time_cost = utcTime() - record_time;
    record_time = utcTime();

    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block.transactions());

    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, _tr, OnOpFunc(), executiveContext);
        block.setTransactionReceipt(_txId, resultReceipt.second);
        executiveContext->getState()->commit();
        return true;
    });
    auto initDag_time_cost = utcTime() - record_time;
    record_time = utcTime();

    auto parallelTimeOut = utcTime() + 30000;  // 30 timeout

    try
    {
        tbb::parallel_for(tbb::blocked_range<unsigned int>(0, m_threadNum),
            [&](const tbb::blocked_range<unsigned int>& _r) {
                (void)_r;
                while (!txDag->hasFinished())
                {
                    if (utcTime() >= parallelTimeOut)
                    {
                        BLOCKVERIFIER_LOG(WARNING)
                            << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block timeout")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("blockNumber", block.blockHeader().number());

#if 0
                        BOOST_THROW_EXCEPTION(BlockExecutionFailed()
                                              << errinfo_comment("Para execute block timeout"));
#endif
                    }

                    txDag->executeUnit();
                }
            });
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_BADGE("executeBlock")
                                 << LOG_DESC("Error during parallel block execution")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            BlockExecutionFailed() << errinfo_comment("Error during parallel block execution"));
    }

    auto exe_time_cost = utcTime() - record_time;
    record_time = utcTime();

    h256 stateRoot = executiveContext->getState()->rootHash();
    auto getRootHash_time_cost = utcTime() - record_time;
    record_time = utcTime();

    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    auto setAllReceipt_time_cost = utcTime() - record_time;
    record_time = utcTime();

    block.calReceiptRoot();
    auto getReceiptRoot_time_cost = utcTime() - record_time;
    record_time = utcTime();

    block.header().setStateRoot(stateRoot);
    block.header().setDBhash(stateRoot);
    auto setStateRoot_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BLOCKVERIFIER_LOG(ERROR)
                << "Invalid Block with bad stateRoot or receiptRoot or dbHash"
                << LOG_KV("blkNum", block.blockHeader().number())
                << LOG_KV("originHash", tmpHeader.hash().abridged())
                << LOG_KV("curHash", block.header().hash().abridged())
                << LOG_KV("orgReceipt", tmpHeader.receiptsRoot().abridged())
                << LOG_KV("curRecepit", block.header().receiptsRoot().abridged())
                << LOG_KV("orgState", tmpHeader.stateRoot().abridged())
                << LOG_KV("curState", block.header().stateRoot().abridged())
                << LOG_KV("orgDBHash", tmpHeader.dbHash().abridged())
                << LOG_KV("curDBHash", block.header().dbHash().abridged());
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block takes")
                             << LOG_KV("time(ms)", utcTime() - start_time)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("blockNumber", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot())
                             << LOG_KV("initExeCtxTimeCost", initExeCtx_time_cost)
                             << LOG_KV("perpareBlockTimeCost", perpareBlock_time_cost)
                             << LOG_KV("initDagTimeCost", initDag_time_cost)
                             << LOG_KV("exeTimeCost", exe_time_cost)
                             << LOG_KV("getRootHashTimeCost", getRootHash_time_cost)
                             << LOG_KV("setAllReceiptTimeCost", setAllReceipt_time_cost)
                             << LOG_KV("getReceiptRootTimeCost", getReceiptRoot_time_cost)
                             << LOG_KV("setStateRootTimeCost", setStateRoot_time_cost);
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
            << LOG_DESC("[#executeTransaction] Error during execute initExecutiveContext")
            << LOG_KV("errorMsg", boost::diagnostic_information(e));
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

    // OK - transaction looks valid - execute.
    try
    {
        e.initialize(_t);
        if (!e.execute())
            e.go(onOp);
        e.finalize();
    }
    catch (Exception const& _e)
    {
        // should not throw exceptions to here, but catch it in case
        BLOCKVERIFIER_LOG(ERROR) << diagnostic_information(_e);
    }
    catch (std::exception const& _e)
    {
        BLOCKVERIFIER_LOG(ERROR) << _e.what();
    }
    /// mptstate calculates every transactions
    /// storagestate ignore hash calculation
    return make_pair(
        res, TransactionReceipt(executiveContext->getState()->rootHash(false), e.gasUsed(),
                 e.logs(), e.status(), e.takeOutput().takeBytes(), e.newAddress()));
}
