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
#include "TxCqDAG.h"
#include "TxDAG.h"
#include <libethcore/Exceptions.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/ExecutionResult.h>
#include <libexecutive/Executive.h>
#include <omp.h>
#include <exception>
using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::executive;

ExecutiveContext::Ptr BlockVerifier::parallelExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
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
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());
    ///*
    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block.transactions());
    /*
    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, _tr, OnOpFunc(), executiveContext);
        block.setTransactionReceipt(_txId, resultReceipt.second);
        executiveContext->getState()->commit();
        return true;
    });*/
    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        executiveContext->getState()->createContract(_tr.sender());
        executiveContext->getState()->setStorage(_tr.sender(), u256(1), u256(2));
        executiveContext->getState()->commit();
        return true;
    });
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Init DAG takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("haveExecureTxNum", txDag->haveExecuteNumber())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();

    if (m_paraTxExecutor != nullptr)
    {
        m_paraTxExecutor->start(txDag);
        /*#pragma omp for
                for (int i = 0; i < 8; i++)
                {
                    while (!txDag->hasFinished())
                        txDag->executeUnit();
                }*/
    }
    else
    {
        while (!txDag->hasFinished())
            txDag->executeUnit();
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Run para tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", txDag->paraTxsNumber())
                             << LOG_KV("haveExecureTxNum", txDag->haveExecuteNumber())
                             << LOG_KV("num", block.blockHeader().number());
    pastTime = utcTime();

    //*/
    /*
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
            executiveContext->getState()->commit();
        }
        //*/

    h256 stateRoot = executiveContext->getState()->rootHash();
    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    block.calReceiptRoot();
    block.header().setStateRoot(stateRoot);

    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Execute block takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot());
    return executiveContext;
}


ExecutiveContext::Ptr BlockVerifier::parallelCqExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
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
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());
    ///*
    shared_ptr<TxCqDAG> txDag = make_shared<TxCqDAG>();
    txDag->init(block.transactions());
    /*
    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, _tr, OnOpFunc(), executiveContext);
        block.setTransactionReceipt(_txId, resultReceipt.second);
        executiveContext->getState()->commit();
        return true;
    });*/
    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        executiveContext->getState()->createContract(_tr.sender());
        executiveContext->getState()->setStorage(_tr.sender(), u256(1), u256(2));
        executiveContext->getState()->commit();
        return true;
    });
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Init DAG takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("haveExecureTxNum", txDag->haveExecuteNumber())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();

    if (m_paraTxExecutor != nullptr)
    {
        m_paraTxExecutor->start(txDag);
        /*#pragma omp for
                for (int i = 0; i < 8; i++)
                {
                    while (!txDag->hasFinished())
                        txDag->executeUnit();
                }*/
    }
    else
    {
        while (!txDag->hasFinished())
            txDag->executeUnit();
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Run para concurrent queue tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", txDag->paraTxsNumber())
                             << LOG_KV("haveExecureTxNum", txDag->haveExecuteNumber())
                             << LOG_KV("num", block.blockHeader().number());
    pastTime = utcTime();

    //*/
    /*
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
            executiveContext->getState()->commit();
        }
        //*/

    h256 stateRoot = executiveContext->getState()->rootHash();
    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    block.calReceiptRoot();
    block.header().setStateRoot(stateRoot);

    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Execute block takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot());
    return executiveContext;
}


ExecutiveContext::Ptr BlockVerifier::parallelOmpExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
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
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());


    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Init env takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();

/*for (Transaction const& tr : block.transactions())
{
    EnvInfo envInfo(block.blockHeader(), m_pNumberHash,
        block.getTransactionReceipts().size() > 0 ?
            block.getTransactionReceipts().back().gasUsed() :
            0);
    envInfo.setPrecompiledEngine(executiveContext);
    std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
        execute(envInfo, tr, OnOpFunc(), executiveContext);
    block.appendTransactionReceipt(resultReceipt.second);
    executiveContext->getState()->commit();
}*/
#pragma omp parallel for
    for (size_t i = 0; i < block.transactions().size(); i++)
    // for (Transaction const& tr : block.transactions())
    {
        auto& tr = block.transactions()[i];
        executiveContext->getState()->createContract(tr.sender());
        executiveContext->getState()->setStorage(tr.sender(), u256(1), u256(2));
        executiveContext->getState()->commit();
    }

    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Run para openmp tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number());
    pastTime = utcTime();

    h256 stateRoot = executiveContext->getState()->rootHash();
    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    block.calReceiptRoot();
    block.header().setStateRoot(stateRoot);

    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Execute block takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot());
    return executiveContext;
}


ExecutiveContext::Ptr BlockVerifier::queueExecuteBlock(
    Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
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
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());
    ///*
    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    txDag->init(executiveContext, block.transactions());
    /*
    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash, 0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, _tr, OnOpFunc(), executiveContext);
        block.setTransactionReceipt(_txId, resultReceipt.second);
        executiveContext->getState()->commit();
        return true;
    });*/
    txDag->setTxExecuteFunc([&](Transaction const& _tr, ID _txId) {
        executiveContext->getState()->createContract(_tr.sender());
        executiveContext->getState()->setStorage(_tr.sender(), u256(1), u256(2));
        executiveContext->getState()->commit();
        return true;
    });
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Init DAG takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("haveExecureTxNum", txDag->haveExecuteNumber())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();

    // if (m_paraTxExecutor != nullptr)
    //{
    //  m_paraTxExecutor->start(txDag);
    /*#pragma omp for
            for (int i = 0; i < 8; i++)
            {
                while (!txDag->hasFinished())
                    txDag->executeUnit();
            }*/
    //}
    // else
    {
        while (!txDag->hasFinished())
            txDag->executeUnit();
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Run serial queue tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", txDag->paraTxsNumber())
                             << LOG_KV("haveExecureTxNum", txDag->haveExecuteNumber())
                             << LOG_KV("num", block.blockHeader().number());
    pastTime = utcTime();

    //*/
    /*
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
            executiveContext->getState()->commit();
        }
        //*/

    h256 stateRoot = executiveContext->getState()->rootHash();
    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    block.calReceiptRoot();
    block.header().setStateRoot(stateRoot);

    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Execute block takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot());
    return executiveContext;
}


ExecutiveContext::Ptr BlockVerifier::executeBlock(Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("parentHash", parentBlockInfo.hash)
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
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    block.resizeTransactionReceipt(block.transactions().size());


    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Init env takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number());
    uint64_t pastTime = utcTime();

    /*for (Transaction const& tr : block.transactions())
    {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash,
            block.getTransactionReceipts().size() > 0 ?
                block.getTransactionReceipts().back().gasUsed() :
                0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, tr, OnOpFunc(), executiveContext);
        block.appendTransactionReceipt(resultReceipt.second);
        executiveContext->getState()->commit();
    }*/
    //#pragma omp parallel for
    for (size_t i = 0; i < block.transactions().size(); i++)
    // for (Transaction const& tr : block.transactions())
    {
        auto& tr = block.transactions()[i];
        executiveContext->getState()->createContract(tr.sender());
        executiveContext->getState()->setStorage(tr.sender(), u256(1), u256(2));
        executiveContext->getState()->commit();
    }

    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Run serial tx takes")
                             << LOG_KV("time(ms)", utcTime() - pastTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number());
    pastTime = utcTime();

    h256 stateRoot = executiveContext->getState()->rootHash();
    // set stateRoot in receipts
    block.setStateRootToAllReceipt(stateRoot);
    block.updateSequenceReceiptGas();
    block.calReceiptRoot();
    block.header().setStateRoot(stateRoot);

    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReciptRoot"));
        }
    }
    BLOCKVERIFIER_LOG(DEBUG) << LOG_BADGE("executeBlock") << LOG_BADGE("Report")
                             << LOG_DESC("Execute block takes")
                             << LOG_KV("time(ms)", utcTime() - startTime)
                             << LOG_KV("txNum", block.transactions().size())
                             << LOG_KV("num", block.blockHeader().number())
                             << LOG_KV("blockHash", block.headerHash())
                             << LOG_KV("stateRoot", block.header().stateRoot())
                             << LOG_KV("transactionRoot", block.transactionRoot())
                             << LOG_KV("receiptRoot", block.receiptRoot());
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
    e.initialize(_t);

    // OK - transaction looks valid - execute.
    u256 startGasUsed = _envInfo.gasUsed();
    if (!e.execute())
        e.go(onOp);
    e.finalize();

    return make_pair(res, TransactionReceipt(EmptySHA3, startGasUsed + e.gasUsed(), e.logs(),
                              e.status(), e.takeOutput().takeBytes(), e.newAddress()));
}
