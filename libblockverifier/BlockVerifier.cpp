/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file BlockVerifier.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */
#include "BlockVerifier.h"
#include <libethcore/PrecompiledContract.h>
#include <libethcore/TransactionReceipt.h>
using namespace dev;
using namespace dev::eth;
using namespace dev::blockverifier;

ExecutiveContext::Ptr BlockVerifier::executeBlock(Block& block, int stateType,
    std::unordered_map<Address, PrecompiledContract> const& precompiledContract)
{
    /* LOG(TRACE) << "Block:enact tx_num=" << block.transactions().size();
     try
     {
         dev::blockverifier::BlockInfo blockInfo;
         blockInfo.hash = block.blockHeader().hash();
         blockInfo.number = block.blockHeader().number();
         dev::blockverifier::ExecutiveContext::Ptr executiveContext =
             std::make_shared<dev::blockverifier::ExecutiveContext>();
         executiveContext->setPrecompiledContract(precompiledContract);
         m_executiveContextFactory->initExecutiveContext(blockInfo, executiveContext);
     }
     catch (exception& e)
     {
         LOG(ERROR) << "Error:" << e.what();
     }
     unsigned i = 0;
     DEV_TIMED_ABOVE("Block::enact txExec,blk=" + toString(block.info.number()) +
                         ",txs=" + toString(block.transactions.size()) + " ",
         1)

     for (Transaction const& tr : block.transactions)
     {
         try
         {
             execute(lh, tr, OnOpFunc(), executiveContext);

             // LOG(TRACE) << "Now: " << tr.from() << state().transactionsFrom(tr.from());
             // LOG(TRACE) << m_state;
         }
         catch (Exception& ex)
         {
             ex << errinfo_transactionIndex(i);
             throw;
         }

         LOG(TRACE) << "Block::enact: t=" << toString(tr.sha3());
         LOG(TRACE) << "Block::enact: stateRoot=" << toString(m_receipts.back().stateRoot())
                    << ",gasUsed=" << toString(m_receipts.back().gasUsed())
                    << ",sha3=" << toString(sha3(m_receipts.back().rlp()));

         RLPStream receiptRLP;
         m_receipts.back().streamRLP(receiptRLP);
         receipts.push_back(receiptRLP.out());
         ++i;
     }
     */
}
/*
 dev::eth::TransactionReceipt BlockVerifier::executeTransation(const dev::eth::Transaction&
transaction)
 {
     dev::eth::TransactionReceipt receipt;
     return receipt;
 }


// will throw exception
ExecutionResult BlockVerifier::execute(LastBlockHashesFace const& _lh, Transaction const& _t,
Permanence _p, OnOpFunc const& _onOp, dev::blockverifier::ExecutiveContext::Ptr executiveContext)
{
    dev::precompiled::BlockInfo blockInfo;
    blockInfo.hash = info().hash();
    blockInfo.number = info().number();
    executiveContext->setBlockInfo(blockInfo);
    EnvInfo envInfo(info(), _lh, gasUsed());
    envInfo.setPrecompiledEngine(executiveContext);

    std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
        execute(envInfo, *m_sealEngine, _t, _p, _onOp);

    executiveContext->pushBackTransactionReceipt(resultReceipt.second));
    executiveContext->commit();

    return resultReceipt.first;
}

std::pair<ExecutionResult, TransactionReceipt> BlockVerifier::execute(EnvInfo const& _envInfo,
    Transaction const& _t, OnOpFunc const& _onOp,
    dev::blockverifier::ExecutiveContext::Ptr executiveContext)
{
    StatTxExecLogGuard guard;
    guard << "State::execute";

    LOG(TRACE) << "State::execute ";

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


    executiveContext.commit();

    return make_pair(
        res, TransactionReceipt(rootHash(), startGasUsed + e.gasUsed(), e.logs(), e.newAddress()));
}
*/