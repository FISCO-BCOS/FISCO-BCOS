/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Block.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Block.h"

#include <ctime>
#include <boost/filesystem.hpp>
#include <boost/timer.hpp>

#include <libdevcore/CommonIO.h>
#include <libdevcore/Assertions.h>
#include <libdevcore/TrieHash.h>
#include <libevmcore/Instruction.h>
#include <libethcore/Exceptions.h>
#include <libethcore/SealEngine.h>
#include <libevm/VMFactory.h>
#include <UTXO/UTXOSharedData.h>
#include <UTXO/UTXOTxQueue.h>

#include "BlockChain.h"
#include "Defaults.h"
#include "ExtVM.h"
#include "Executive.h"
#include "BlockChain.h"
#include "TransactionQueue.h"
#include "GenesisInfo.h"
#include "SystemContractApi.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace fs = boost::filesystem;

#define ETH_TIMED_ENACTMENTS 0

unsigned Block::c_maxSyncTransactions = 100; 


Block::Block(BlockChain const& _bc, OverlayDB const& _db, BaseState _bs, Address const& _author):
    m_state(Invalid256, _db, _bs),
    m_precommit(Invalid256),
    m_author(_author)
{
    noteChain(_bc);
    m_previousBlock.clear();
    m_currentBlock.clear();
//  assert(m_state.root() == m_previousBlock.stateRoot());
}

Block::Block(BlockChain const& _bc, OverlayDB const& _db, h256 const& _root, Address const& _author):
    m_state(Invalid256, _db, BaseState::PreExisting),
    m_precommit(Invalid256),
    m_author(_author)
{
    noteChain(_bc);
    m_state.setRoot(_root);
    m_previousBlock.clear();
    m_currentBlock.clear();
//  assert(m_state.root() == m_previousBlock.stateRoot());
}

Block::Block(Block const& _s):
    m_state(_s.m_state),
    m_transactions(_s.m_transactions),
    m_receipts(_s.m_receipts),
    m_transactionSet(_s.m_transactionSet),
    m_precommit(_s.m_state),
    m_previousBlock(_s.m_previousBlock),
    m_currentBlock(_s.m_currentBlock),
    m_currentBytes(_s.m_currentBytes),
    m_author(_s.m_author),
    m_sealEngine(_s.m_sealEngine),
    m_utxoMgr(_s.m_utxoMgr)
{
    m_committedToSeal = false;
}

Block& Block::operator=(Block const& _s)
{
    if (&_s == this)
        return *this;

    m_state = _s.m_state;
    m_transactions = _s.m_transactions;
    m_receipts = _s.m_receipts;
    m_transactionSet = _s.m_transactionSet;
    m_previousBlock = _s.m_previousBlock;
    m_currentBlock = _s.m_currentBlock;
    m_currentBytes = _s.m_currentBytes;
    m_author = _s.m_author;
    m_sealEngine = _s.m_sealEngine;

    m_precommit = m_state;
    m_committedToSeal = false;

    m_utxoMgr = _s.m_utxoMgr;

    return *this;
}

void Block::resetCurrent(u256 const& _timestamp)
{
    m_transactions.clear();
    m_receipts.clear();
    m_transactionSet.clear();
    m_currentBlock = BlockHeader();
    m_currentBlock.setAuthor(m_author);
    m_currentBlock.setTimestamp(max(m_previousBlock.timestamp() + 1, _timestamp));
    m_currentBytes.clear();

    m_utxoMgr.clearDBRecord();

    sealEngine()->populateFromParent(m_currentBlock, m_previousBlock);

    // TODO: check.
    m_state.setRoot(m_previousBlock.stateRoot());

    m_precommit = m_state;
    m_committedToSeal = false;

    performIrregularModifications();
}

void Block::resetCurrentTime(u256 const& _timestamp) {
    m_currentBlock.setTimestamp(max(m_previousBlock.timestamp() + 1, _timestamp));
}
void Block::setIndex(u256 _idx) {
    m_currentBlock.setIndex(_idx);
}
void Block::setNodeList(h512s const& _nodes) {
    m_currentBlock.setNodeList(_nodes);
}

SealEngineFace* Block::sealEngine() const
{
    if (!m_sealEngine)
        BOOST_THROW_EXCEPTION(ChainOperationWithUnknownBlockChain());
    return m_sealEngine;
}

void Block::noteChain(BlockChain const& _bc)
{
    if (!m_sealEngine)
    {
        m_state.noteAccountStartNonce(_bc.chainParams().accountStartNonce);
        m_precommit.noteAccountStartNonce(_bc.chainParams().accountStartNonce);
        m_sealEngine = _bc.sealEngine();
    }
}

PopulationStatistics Block::populateFromChain(BlockChain const& _bc, h256 const& _h, ImportRequirements::value _ir)
{


    noteChain(_bc);

    PopulationStatistics ret { 0.0, 0.0 };

    if (!_bc.isKnown(_h))
    {
        // Might be worth throwing here.
        LOG(WARNING) << "Invalid block given for state population: " << _h;
        BOOST_THROW_EXCEPTION(BlockNotFound() << errinfo_target(_h));
    }

    auto b = _bc.block(_h);
    BlockHeader bi(b);      // No need to check - it's already in the DB.

    if (bi.number())
    {
        // Non-genesis:

        // 1. Start at parent's end state (state root).
        BlockHeader bip(_bc.block(bi.parentHash()));
        sync(_bc, bi.parentHash(), bip);

        // 2. Enact the block's transactions onto this state.
        m_author = bi.author();
        Timer t;
        auto vb = _bc.verifyBlock(&b, function<void(Exception&)>(), _ir | ImportRequirements::TransactionBasic);
        ret.verify = t.elapsed();
        t.restart();
        enact(vb, _bc);
        ret.enact = t.elapsed();
    }
    else
    {
        // Genesis required:
        // We know there are no transactions, so just populate directly.
        m_state = State(m_state.accountStartNonce(), m_state.db(), BaseState::Empty);   // TODO: try with PreExisting.
        sync(_bc, _h, bi);
    }

    return ret;
}

bool Block::sync(BlockChain const& _bc)
{
    return sync(_bc, _bc.currentHash());
}

bool Block::sync(BlockChain const& _bc, h256 const& _block, BlockHeader const& _bi)
{
    noteChain(_bc);

    bool ret = false;
    // BLOCK
    BlockHeader bi = _bi ? _bi : _bc.info(_block);
#if ETH_PARANOIA
    if (!bi)
        while (1)
        {
            try
            {
                auto b = _bc.block(_block);
                bi.populate(b);
                break;
            }
            catch (Exception const& _e)
            {
                // TODO: Slightly nicer handling? :-)
                LOG(ERROR) << "ERROR: Corrupt block-chain! Delete your block-chain DB and restart." << "\n";
                LOG(ERROR) << diagnostic_information(_e) << "\n";
            }
            catch (std::exception const& _e)
            {
                // TODO: Slightly nicer handling? :-)
                LOG(ERROR) << "ERROR: Corrupt block-chain! Delete your block-chain DB and restart." << "\n";
                LOG(ERROR) << _e.what() << "\n";
            }
        }
#endif
    if (bi == m_currentBlock)
    {
        // We mined the last block.
        // Our state is good - we just need to move on to next.
        m_previousBlock = m_currentBlock;
        resetCurrent();                 
        ret = true;
    }
    else if (bi == m_previousBlock)
    {
        // No change since last sync.
        // Carry on as we were.
    }
    else
    {
        // New blocks available, or we've switched to a different branch. All change.
        // Find most recent state dump and replay what's left.
        // (Most recent state dump might end up being genesis.)
        if (m_state.db().lookup(bi.stateRoot()).empty())    
        {
            LOG(WARNING) << "Unable to sync to" << bi.hash() << "; state root" << bi.stateRoot() << "not found in database.";
            LOG(WARNING) << "Database corrupt: contains block without stateRoot:" << bi;
            LOG(WARNING) << "Try rescuing the database by running: eth --rescue";
            BOOST_THROW_EXCEPTION(InvalidStateRoot() << errinfo_target(bi.stateRoot()));
        }
        m_previousBlock = bi;
        resetCurrent();
        ret = true;
    }
#if ALLOW_REBUILD
    else
    {
        // New blocks available, or we've switched to a different branch. All change.
        // Find most recent state dump and replay what's left.
        // (Most recent state dump might end up being genesis.)

        std::vector<h256> chain;
        while (bi.number() != 0 && m_db.lookup(bi.stateRoot()).empty()) // while we don't have the state root of the latest block...
        {
            chain.push_back(bi.hash());             // push back for later replay.
            bi.populate(_bc.block(bi.parentHash()));    // move to parent.
        }

        m_previousBlock = bi;
        resetCurrent();

        // Iterate through in reverse, playing back each of the blocks.
        try
        {
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                auto b = _bc.block(*it);
                enact(&b, _bc, _ir);
                cleanup(true);
            }
        }
        catch (...)
        {
            // TODO: Slightly nicer handling? :-)
            LOG(ERROR) << "ERROR: Corrupt block-chain! Delete your block-chain DB and restart." << "\n";
            LOG(ERROR) << boost::current_exception_diagnostic_information() << "\n";
            exit(1);
        }

        resetCurrent();
        ret = true;
    }
#endif
    return ret;
}

pair<TransactionReceipts, bool> Block::sync(BlockChain const& _bc, TransactionQueue& _tq, GasPricer const& _gp, bool _exec, u256 const& _max_block_txs)
{
    LOG(TRACE) << "Block::sync";

    if (isSealed())
        BOOST_THROW_EXCEPTION(InvalidOperationOnSealedBlock());

    noteChain(_bc);

    // TRANSACTIONS
    pair<TransactionReceipts, bool> ret;

    unsigned max_sync_txs = 0;
    if (_max_block_txs == Invalid256) {
        max_sync_txs = c_maxSyncTransactions;
    } else {
        max_sync_txs = static_cast<unsigned>(_max_block_txs > m_transactions.size() ? _max_block_txs - m_transactions.size() : 0);
    }
  
    auto ts = _tq.allTransactions();

    LastHashes lh;
    unsigned goodTxs = 0;
    std::map<h256, bool> parallelUTXOTx;
    size_t parallelUTXOTxCnt = 0;

    if (lh.empty()) { lh = _bc.lastHashes(); }
    m_utxoMgr.setCurBlockInfo(this, lh);
    if (_exec)
    {
        getParallelUTXOTx(ts, parallelUTXOTx, parallelUTXOTxCnt);
        m_state.setParallelUTXOTx(parallelUTXOTx);
        LOG(TRACE) << "Block::sync parallelUTXOTxCnt:" << parallelUTXOTxCnt;
    }
    UTXOModel::UTXOTxQueue utxoTxQueue(parallelUTXOTxCnt, &m_utxoMgr);
    m_onParallelTxQueueReady = false;
    if (_exec && parallelUTXOTxCnt > 0)
    {
        m_parallelEnd = utxoTxQueue.onReady([ = ]() { this->onUTXOTxQueueReady(); });
    }
    //for (int goodTxs = max(0, (int)ts.size() - 1); goodTxs < (int)ts.size(); )
    {
        //goodTxs = 0;
        for (auto const& t : ts)
            if (!m_transactionSet.count(t.sha3()))
            {
                try
                {
                    LOG(TRACE) << "PACK-TX: Hash=" << (t.sha3()) << ",Randid=" << t.randomid() << ",time=" << utcTime();
                    //交易打包前检查
                    for ( size_t pIndex = 0; pIndex < m_transactions.size(); pIndex++) //多做一步，当前块内也不能重复出现
                    {
                        if ( (m_transactions[pIndex].from() == t.from() ) && (m_transactions[pIndex].randomid() == t.randomid()) )
                            BOOST_THROW_EXCEPTION(NonceCheckFail());
                    }//for

                    UTXOType utxoType = t.getUTXOType();
	        		LOG(TRACE) << "Block::sync utxoType:" << utxoType;

                    if (_exec) {
                        u256 _t = _gp.ask(*this);

                        if ( _t )
                            _t = 0;
                        //Timer t;
                        if (lh.empty())
                            lh = _bc.lastHashes();
                        if (parallelUTXOTx[t.sha3()])
                        {
                            utxoTxQueue.enqueue(t);
                        }
                        execute(lh, t, Permanence::Committed, OnOpFunc(), &_bc);
                        ret.first.push_back(m_receipts.back());
                    } else {
                        LOG(TRACE) << "Block::sync no need exec: t=" << toString(t.sha3());
                        m_transactions.push_back(t);
                        m_transactionSet.insert(t.sha3());
                    }
                    ++goodTxs;
                }
                catch ( FilterCheckFail const& in)
                {
                    LOG(WARNING) << t.sha3() << "Block::sync Dropping  transaction (filter check fail!)";
                    _tq.drop(t.sha3());
                }
                catch ( NoDeployPermission const &in)
                {
                    LOG(WARNING) << t.sha3() << "Block::sync Dropping  transaction (NoDeployPermission  fail!)";
                    _tq.drop(t.sha3());
                }
                catch (BlockLimitCheckFail const& in)
                {
                    LOG(WARNING) << t.sha3() << "Block::sync Dropping  transaction (blocklimit  check fail!)";
                    _tq.drop(t.sha3());
                }
                catch (NonceCheckFail const& in)
                {
                    LOG(WARNING) << t.sha3() << "Block::sync Dropping  transaction (nonce check fail!)";
                    _tq.drop(t.sha3());
                }
                catch (InvalidNonce const& in)
                {
                    bigint const& req = *boost::get_error_info<errinfo_required>(in);
                    bigint const& got = *boost::get_error_info<errinfo_got>(in);

                    if (req > got)
                    {
                        // too old
                        LOG(WARNING) << t.sha3() << "Dropping old transaction (nonce too low)";
                        _tq.drop(t.sha3());
                    }
                    else if (got > req + _tq.waiting(t.sender()))
                    {
                        // too new
                        LOG(WARNING) << t.sha3() << "Dropping new transaction (too many nonces ahead)";
                        _tq.drop(t.sha3());
                    }
                    else
                        _tq.setFuture(t.sha3());
                }
                catch (BlockGasLimitReached const& e)
                {
                    bigint const& got = *boost::get_error_info<errinfo_got>(e);
                    if (got > m_currentBlock.gasLimit())
                    {
                        LOG(WARNING) << t.sha3() << "Dropping over-gassy transaction (gas > block's gas limit)";
                        _tq.drop(t.sha3());
                    }
                    else
                    {
                        LOG(WARNING) << t.sha3() << "Temporarily no gas left in current block (txs gas > block's gas limit)";
                    }
                }
                catch (Exception const& _e)
                {
                    // Something else went wrong - drop it.
                    LOG(WARNING) << t.sha3() << "Dropping invalid transaction:" << diagnostic_information(_e);
                    _tq.drop(t.sha3());
                }
                catch (std::exception const&)
                {
                    // Something else went wrong - drop it.
                    _tq.drop(t.sha3());
                    LOG(WARNING) << t.sha3() << "Transaction caused low-level exception :(";
                }

                if (goodTxs >= max_sync_txs) {
                    break;
                }
            }
        ret.second = (goodTxs >= max_sync_txs);
    }
    if (_exec && parallelUTXOTxCnt > 0)
    {
        StartWaitingForResult(parallelUTXOTxCnt);
        LOG(TRACE) << "Block::sync parallelTx end";
        map<h256, UTXOModel::UTXOExecuteState> mapTxResult = utxoTxQueue.getTxResult(); 
        for (auto const& t : ts)
        {
            h256 hash = t.sha3();
            if (parallelUTXOTx[hash])
            {
                if (!mapTxResult.count(hash) || 
                    UTXOModel::UTXOExecuteState::Success != mapTxResult[hash])
                {
                    LOG(TRACE) << hash << " Transaction Error, drop";
                    _tq.drop(hash);
                    BOOST_THROW_EXCEPTION(UTXOTxError());
                }
            }
        }
    }
    return ret;
}

TransactionReceipts Block::exec(BlockChain const& _bc, TransactionQueue& _tq)
{
    LOG(TRACE) << "Block::exec ";

    if (isSealed())
        BOOST_THROW_EXCEPTION(InvalidOperationOnSealedBlock());

    noteChain(_bc);

    // TRANSACTIONS
    TransactionReceipts ret;

    LastHashes lh;
    DEV_TIMED_ABOVE("lastHashes", 500)
    lh = _bc.lastHashes();

    std::map<h256, bool> parallelUTXOTx;
    size_t parallelUTXOTxCnt = 0;
    getParallelUTXOTx(m_transactions, parallelUTXOTx, parallelUTXOTxCnt);

    if (parallelUTXOTxCnt > 0)
    {
        return execUTXOInBlock(_bc, _tq, lh, parallelUTXOTx, parallelUTXOTxCnt);
    }

    unsigned i = 0;
    DEV_TIMED_ABOVE("Block::exec txExec,blk=" + toString(info().number()) + ",txs=" + toString(m_transactions.size()) + ",timecost=", 500)
    for (Transaction const& tr : m_transactions)
    {
        try
        {
            LOG(TRACE) << "Block::exec transaction: " << tr.randomid() << tr.from() /*<< state().transactionsFrom(tr.from()) */ << tr.value() << toString(tr.sha3());
            execute(lh, tr, Permanence::OnlyReceipt, OnOpFunc(), &_bc);
        }
        catch (Exception& ex)
        {
            ex << errinfo_transactionIndex(i);
            _tq.drop(tr.sha3());  
            throw;
        }
        catch (std::exception& ex)
        {
	        LOG(ERROR)<<"execute t="<<toString(tr.sha3())<<" failed, error message:"<<ex.what();
            _tq.drop(tr.sha3());  
            throw;
        }
        LOG(TRACE) << "Block::exec: t=" << toString(tr.sha3());
        LOG(TRACE) << "Block::exec: stateRoot=" << toString(m_receipts.back().stateRoot()) << ",gasUsed=" << toString(m_receipts.back().gasUsed()) << ",sha3=" << toString(sha3(m_receipts.back().rlp()));

        RLPStream receiptRLP;
        m_receipts.back().streamRLP(receiptRLP);
        ret.push_back(m_receipts.back());
        ++i;
    }

    return ret;
}

void Block::onUTXOTxQueueReady() 
{
    m_onParallelTxQueueReady = true; 
    m_signalled.notify_all(); 
    LOG(TRACE) << "Block::onUTXOTxQueueReady";
}

void Block::StartWaitingForResult(size_t parallelUTXOTxCnt)
{
    if (parallelUTXOTxCnt > 0 && 
        !m_onParallelTxQueueReady) 
    { 
        unique_lock<Mutex> l(x_queue);
		m_signalled.wait(l);
    }
}

TransactionReceipts Block::execUTXOInBlock(BlockChain const& _bc, TransactionQueue& _tq, const LastHashes& lh, std::map<h256, bool>& parallelUTXOTx, size_t parallelUTXOTxCnt)
{
    // TRANSACTIONS
    TransactionReceipts ret;
    
    unsigned i = 0;                                                         // 传入的交易队列中的计数索引
    UTXOModel::UTXOTxQueue utxoTxQueue(parallelUTXOTxCnt, &m_utxoMgr);
    m_onParallelTxQueueReady = false;
    m_parallelEnd = utxoTxQueue.onReady([ = ]() { this->onUTXOTxQueueReady(); });
    m_utxoMgr.setCurBlockInfo(this, lh);
    m_state.setParallelUTXOTx(parallelUTXOTx);
    LOG(TRACE) << "Block::exec parallelUTXOTxCnt:" << parallelUTXOTxCnt;
    DEV_TIMED_ABOVE("Block::exec txExec,blk=" + toString(info().number()) + ",txs=" + toString(m_transactions.size()) + ",timecost=", 1)
    for (Transaction const& tr : m_transactions)
    {
        try
        {
            LOG(TRACE) << "Block::exec transaction: " << tr.randomid() << tr.from() /*<< state().transactionsFrom(tr.from()) */ << tr.value() << toString(tr.sha3());
            if (parallelUTXOTx[tr.sha3()])
            {
                utxoTxQueue.enqueue(tr);
            }
            execute(lh, tr, Permanence::OnlyReceipt, OnOpFunc(), &_bc);
        }
        catch (Exception& ex)
        {
            ex << errinfo_transactionIndex(i);
            _tq.drop(tr.sha3());  // TODO: 是否需要分类处理？
            throw;
        }
        LOG(TRACE) << "Block::exec: t=" << toString(tr.sha3());
        LOG(TRACE) << "Block::exec: stateRoot=" << toString(m_receipts.back().stateRoot()) << ",gasUsed=" << toString(m_receipts.back().gasUsed()) << ",sha3=" << toString(sha3(m_receipts.back().rlp()));

        RLPStream receiptRLP;
        m_receipts.back().streamRLP(receiptRLP);
        ret.push_back(m_receipts.back());
        ++i;
    }
    LOG(TRACE) << "Block::exec ParallelTxQueueReady:" << m_onParallelTxQueueReady;
    StartWaitingForResult(parallelUTXOTxCnt);
    LOG(TRACE) << "Block::exec parallelTx end";
    map<h256, UTXOModel::UTXOExecuteState> mapTxResult = utxoTxQueue.getTxResult();
    for (auto const& t : m_transactions)
    {
        h256 hash = t.sha3();
        if (parallelUTXOTx[hash])
        {
            if (!mapTxResult.count(hash) || 
                UTXOModel::UTXOExecuteState::Success != mapTxResult[hash])
            {
                LOG(TRACE) << hash << " Transaction Error, drop";
                _tq.drop(hash);
                BOOST_THROW_EXCEPTION(UTXOTxError());
            }
        }
    }
    return ret;
}

u256 Block::enactOn(VerifiedBlockRef const& _block, BlockChain const& _bc, bool _statusCheck)
{
    noteChain(_bc);

#if ETH_TIMED_ENACTMENTS
    Timer t;
    double populateVerify;
    double populateGrand;
    double syncReset;
    double enactment;
#endif

    // Check family:
    //从blockchain里面根据父hash取出blockHeader
    BlockHeader biParent = _bc.info(_block.info.parentHash());
    _block.info.verify(CheckNothingNew/*CheckParent*/, biParent);

#if ETH_TIMED_ENACTMENTS
    populateVerify = t.elapsed();
    t.restart();
#endif

    BlockHeader biGrandParent;
    if (biParent.number())
        biGrandParent = _bc.info(biParent.parentHash());

#if ETH_TIMED_ENACTMENTS
    populateGrand = t.elapsed();
    t.restart();
#endif

    sync(_bc, _block.info.parentHash(), BlockHeader());
    LOG(TRACE) << "Block::resetCurrent() number:" << biParent.number();
    resetCurrent();

#if ETH_TIMED_ENACTMENTS
    syncReset = t.elapsed();
    t.restart();
#endif

    m_previousBlock = biParent;
    auto ret = enact(_block, _bc, true, _statusCheck); 

#if ETH_TIMED_ENACTMENTS
    enactment = t.elapsed();
    if (populateVerify + populateGrand + syncReset + enactment > 0.5)
        LOG(INFO) << "popVer/popGrand/syncReset/enactment = " << populateVerify << "/" << populateGrand << "/" << syncReset << "/" << enactment;
#endif
    return ret;
}

u256 Block::enact(VerifiedBlockRef const& _block, BlockChain const& _bc, bool _filtercheck, bool _statusCheck)
{
    noteChain(_bc);

    DEV_TIMED_FUNCTION_ABOVE(500);

    // m_currentBlock is assumed to be prepopulated and reset.
#if !ETH_RELEASE
    assert(m_previousBlock.hash() == _block.info.parentHash());
    assert(m_currentBlock.parentHash() == _block.info.parentHash());
#endif

    if (m_currentBlock.parentHash() != m_previousBlock.hash())
        // Internal client error.
        BOOST_THROW_EXCEPTION(InvalidParentHash());

    // Populate m_currentBlock with the correct values.
    m_currentBlock.noteDirty();
    m_currentBlock = _block.info;

    LastHashes lh;
    DEV_TIMED_ABOVE("lastHashes", 500)
    lh = _bc.lastHashes(m_currentBlock.parentHash());

    RLP rlp(_block.block);

    vector<bytes> receipts;

    LOG(TRACE) << "Block:enact tx_num=" << _block.transactions.size();
    // All ok with the block generally. Play back the transactions now...
    m_utxoMgr.setCurBlockInfo(this, lh);
    
    std::map<h256, bool> parallelUTXOTx;
    size_t parallelUTXOTxCnt = 0;
    getParallelUTXOTx(_block.transactions, parallelUTXOTx, parallelUTXOTxCnt);
    if (parallelUTXOTxCnt > 0)
    {
        m_state.setParallelUTXOTx(parallelUTXOTx);
        LOG(TRACE) << "Block::enact parallelUTXOTxCnt:" << parallelUTXOTxCnt;
        unsigned i = 0;
        UTXOModel::UTXOTxQueue utxoTxQueue(parallelUTXOTxCnt, &m_utxoMgr);
        m_onParallelTxQueueReady = false;
        m_parallelEnd = utxoTxQueue.onReady([ = ]() { this->onUTXOTxQueueReady(); });
        DEV_TIMED_ABOVE("Block::enact txExec,blk=" + toString(_block.info.number()) + ",txs=" + toString(_block.transactions.size()) + " ", 1)
        for (Transaction const& tr : _block.transactions)
        {
            try
            {
                LOG(TRACE) << "Enacting transaction: " << tr.randomid() << tr.from() /*<< state().transactionsFrom(tr.from()) */ << tr.value() << toString(tr.sha3());
                if (parallelUTXOTx[tr.sha3()])
                {
                    utxoTxQueue.enqueue(tr);
                }
                // 区分从enactOn和populateFromChain
                execute(lh, tr, Permanence::Committed, OnOpFunc(), (_filtercheck ? (&_bc) : nullptr));

                //LOG(TRACE) << "Now: " << tr.from() << state().transactionsFrom(tr.from());
                //LOG(TRACE) << m_state;
            }
            catch (Exception& ex)
            {
                ex << errinfo_transactionIndex(i);
                throw;
            }

            LOG(TRACE) << "Block::enact: t=" << toString(tr.sha3());
            LOG(TRACE) << "Block::enact: stateRoot=" << toString(m_receipts.back().stateRoot()) << ",gasUsed=" << toString(m_receipts.back().gasUsed()) << ",sha3=" << toString(sha3(m_receipts.back().rlp()));

            RLPStream receiptRLP;
            m_receipts.back().streamRLP(receiptRLP);
            receipts.push_back(receiptRLP.out());
            ++i;
        }
        StartWaitingForResult(parallelUTXOTxCnt);
        map<h256, UTXOModel::UTXOExecuteState> mapTxResult = utxoTxQueue.getTxResult();
        LOG(TRACE) << "Block::enact parallelTx end";
        for (auto const& t : _block.transactions)
        {
            h256 hash = t.sha3();
            if (parallelUTXOTx[hash])
            {
                if (!mapTxResult.count(hash) || 
                    UTXOModel::UTXOExecuteState::Success != mapTxResult[hash])
                {
                    LOG(TRACE) << hash << " Transaction Error";
                    BOOST_THROW_EXCEPTION(UTXOTxError());
                }
            }
        }
    }
    else
    {
        unsigned i = 0;
        DEV_TIMED_ABOVE("Block::enact txExec,blk=" + toString(_block.info.number()) + ",txs=" + toString(_block.transactions.size()) + " ", 1)
        for (Transaction const& tr : _block.transactions)
        {
            try
            {
                LOG(TRACE) << "Enacting transaction: " << tr.randomid() << tr.from() /*<< state().transactionsFrom(tr.from()) */ << tr.value() << toString(tr.sha3());
                // 区分从enactOn和populateFromChain
                execute(lh, tr, Permanence::Committed, OnOpFunc(), (_filtercheck ? (&_bc) : nullptr));

                //LOG(TRACE) << "Now: " << tr.from() << state().transactionsFrom(tr.from());
                //LOG(TRACE) << m_state;
            }
            catch (Exception& ex)
            {
                ex << errinfo_transactionIndex(i);
                throw;
            }

            LOG(TRACE) << "Block::enact: t=" << toString(tr.sha3());
            LOG(TRACE) << "Block::enact: stateRoot=" << toString(m_receipts.back().stateRoot()) << ",gasUsed=" << toString(m_receipts.back().gasUsed()) << ",sha3=" << toString(sha3(m_receipts.back().rlp()));

            RLPStream receiptRLP;
            m_receipts.back().streamRLP(receiptRLP);
            receipts.push_back(receiptRLP.out());
            ++i;
        }
    }

    h256 receiptsRoot;
    DEV_TIMED_ABOVE(".receiptsRoot()", 500)
    receiptsRoot = orderedTrieRoot(receipts);

    if (_statusCheck && receiptsRoot != m_currentBlock.receiptsRoot())
    {
        LOG(TRACE) << "Block::enact receiptsRoot" << toString(receiptsRoot) << ",m_currentBlock.receiptsRoot()=" << toString(m_currentBlock.receiptsRoot()) << ",header" << m_currentBlock;

        InvalidReceiptsStateRoot ex;
        ex << Hash256RequirementError(receiptsRoot, m_currentBlock.receiptsRoot());
        ex << errinfo_receipts(receipts);
//      ex << errinfo_vmtrace(vmTrace(_block.block, _bc, ImportRequirements::None));
        BOOST_THROW_EXCEPTION(ex);
    }

    if (_statusCheck && m_currentBlock.logBloom() != logBloom())
    {
        InvalidLogBloom ex;
        ex << LogBloomRequirementError(logBloom(), m_currentBlock.logBloom());
        ex << errinfo_receipts(receipts);
        BOOST_THROW_EXCEPTION(ex);
    }

    // Initialise total difficulty calculation.
    u256 tdIncrease = m_currentBlock.difficulty();

    // Check uncles & apply their rewards to state.
    if (rlp[2].itemCount() > 2)
    {
        TooManyUncles ex;
        ex << errinfo_max(2);
        ex << errinfo_got(rlp[2].itemCount());
        BOOST_THROW_EXCEPTION(ex);
    }

    vector<BlockHeader> rewarded;
    h256Hash excluded;
    DEV_TIMED_ABOVE("allKin", 500)
    excluded = _bc.allKinFrom(m_currentBlock.parentHash(), 6);
    excluded.insert(m_currentBlock.hash());

    unsigned ii = 0;
    DEV_TIMED_ABOVE("uncleCheck", 500)
    for (auto const& i : rlp[2])
    {
        try
        {
            auto h = sha3(i.data());
            if (excluded.count(h))
            {
                UncleInChain ex;
                ex << errinfo_comment("Uncle in block already mentioned");
                ex << errinfo_unclesExcluded(excluded);
                ex << errinfo_hash256(sha3(i.data()));
                BOOST_THROW_EXCEPTION(ex);
            }
            excluded.insert(h);

            // CheckNothing since it's a VerifiedBlock.
            BlockHeader uncle(i.data(), HeaderData, h);

            BlockHeader uncleParent;
            if (!_bc.isKnown(uncle.parentHash()))
                BOOST_THROW_EXCEPTION(UnknownParent() << errinfo_hash256(uncle.parentHash()));
            uncleParent = BlockHeader(_bc.block(uncle.parentHash()));

            // m_currentBlock.number() - uncle.number()     m_cB.n - uP.n()
            // 1                                            2
            // 2
            // 3
            // 4
            // 5
            // 6                                            7
            //                                              (8 Invalid)
            bigint depth = (bigint)m_currentBlock.number() - (bigint)uncle.number();
            if (depth > 6)
            {
                UncleTooOld ex;
                ex << errinfo_uncleNumber(uncle.number());
                ex << errinfo_currentNumber(m_currentBlock.number());
                BOOST_THROW_EXCEPTION(ex);
            }
            else if (depth < 1)
            {
                UncleIsBrother ex;
                ex << errinfo_uncleNumber(uncle.number());
                ex << errinfo_currentNumber(m_currentBlock.number());
                BOOST_THROW_EXCEPTION(ex);
            }
            // cB
            // cB.p^1       1 depth, valid uncle
            // cB.p^2   ---/  2
            // cB.p^3   -----/  3
            // cB.p^4   -------/  4
            // cB.p^5   ---------/  5
            // cB.p^6   -----------/  6
            // cB.p^7   -------------/
            // cB.p^8
            auto expectedUncleParent = _bc.details(m_currentBlock.parentHash()).parent;
            for (unsigned i = 1; i < depth; expectedUncleParent = _bc.details(expectedUncleParent).parent, ++i) {}
            if (expectedUncleParent != uncleParent.hash())
            {
                UncleParentNotInChain ex;
                ex << errinfo_uncleNumber(uncle.number());
                ex << errinfo_currentNumber(m_currentBlock.number());
                BOOST_THROW_EXCEPTION(ex);
            }
            uncle.verify(CheckNothingNew/*CheckParent*/, uncleParent);

            rewarded.push_back(uncle);
            ++ii;
        }
        catch (Exception& ex)
        {
            ex << errinfo_uncleIndex(ii);
            throw;
        }
    }

    DEV_TIMED_ABOVE("enact:state commit timecost=", 500)
    m_state.commit( State::CommitBehaviour::KeepEmptyAccounts);

    // Hash the state trie and check against the state_root hash in m_currentBlock.
    if (_statusCheck && m_currentBlock.stateRoot() != m_previousBlock.stateRoot() && m_currentBlock.stateRoot() != rootHash())
    {
        auto r = rootHash();
        m_state.db().rollback();
        LOG(WARNING) << "m_currentBlock.stateRoot()=" << m_currentBlock.stateRoot() << ",m_previousBlock.stateRoot()=" << m_previousBlock.stateRoot() << ",rootHash()=" << rootHash();
        // TODO: API in State for this?
        BOOST_THROW_EXCEPTION(InvalidStateRoot() << Hash256RequirementError(r, m_currentBlock.stateRoot()));
    }

    if (_statusCheck && m_currentBlock.gasUsed() != gasUsed())
    {
        // Rollback the trie.
        m_state.db().rollback();        // TODO: API in State for this?
        BOOST_THROW_EXCEPTION(InvalidGasUsed() << RequirementError(bigint(gasUsed()), bigint(m_currentBlock.gasUsed())));
    }

    return tdIncrease;
}

// will throw exception
ExecutionResult Block::execute(LastHashes const& _lh, Transaction const& _t, Permanence _p, OnOpFunc const& _onOp, BlockChain const *_bcp)
{
    LOG(TRACE) << "Block::execute " << _t.sha3() << ",to=" << _t.to() << "permanence=" << (int)_p << "_bcp=" << (_bcp!=nullptr ? "not null" : "is null");
    if (isSealed())
        BOOST_THROW_EXCEPTION(InvalidOperationOnSealedBlock());

    // Uncommitting is a non-trivial operation - only do it once we've verified as much of the
    // transaction as possible.
    uncommitToSeal();

    if ( _bcp != nullptr )
    {
        u256 check = _bcp->filterCheck(_t, FilterCheckScene::BlockExecuteTransation);
        if ( (u256)SystemContractCode::Ok != check )
        {
            LOG(WARNING) << "Block::execute " << _t.sha3() << " transition filterCheck Fail" << check;
            BOOST_THROW_EXCEPTION(FilterCheckFail());
        }
    }

    std::pair<ExecutionResult, TransactionReceipt> resultReceipt = m_state.execute(EnvInfo(info(), _lh, gasUsed(), m_evmCoverLog, m_evmEventLog), *m_sealEngine, _t, _p, _onOp, &m_utxoMgr);

    if (_p == Permanence::Committed)
    {
        // Add to the user-originated transactions that we've executed.
        m_transactions.push_back(_t);
        LOG(TRACE) << "Block::execute: t=" << toString(_t.sha3());
        m_receipts.push_back(resultReceipt.second);
        LOG(TRACE) << "Block::execute: stateRoot=" << toString(resultReceipt.second.stateRoot()) << ",gasUsed=" << toString(resultReceipt.second.gasUsed()) << ",sha3=" << toString(sha3(resultReceipt.second.rlp()));
        m_transactionSet.insert(_t.sha3());


        if (_bcp) {
            (_bcp)->updateCache(_t.to());
        }

    }
   
    if (_p == Permanence::OnlyReceipt)
    {
        m_receipts.push_back(resultReceipt.second);
        LOG(TRACE) << "Block::execute: stateRoot=" << toString(resultReceipt.second.stateRoot()) << ",gasUsed=" << toString(resultReceipt.second.gasUsed()) << ",sha3=" << toString(sha3(resultReceipt.second.rlp()));
    }

    return resultReceipt.first;
}

ExecutionResult Block::executeByUTXO(LastHashes const& _lh, Transaction const& _t, Permanence _p, OnOpFunc const& _onOp)
{
    LOG(TRACE) << "Block::executeByUTXO " << _t.sha3();
    if (isSealed())
        BOOST_THROW_EXCEPTION(InvalidOperationOnSealedBlock());

    // Uncommitting is a non-trivial operation - only do it once we've verified as much of the
    // transaction as possible.
    uncommitToSeal();

    //初始化环境EnvInfo 每次执行交易都会初始化环境，
    std::pair<ExecutionResult, TransactionReceipt> resultReceipt = m_state.execute(EnvInfo(info(), _lh, gasUsed(), m_evmCoverLog, m_evmEventLog), *m_sealEngine, _t, _p, _onOp, &m_utxoMgr);

    return resultReceipt.first;
}

void Block::applyRewards(vector<BlockHeader> const& _uncleBlockHeaders, u256 const& _blockReward)
{
    return ;
    u256 r = _blockReward;
    for (auto const& i : _uncleBlockHeaders)
    {
        m_state.addBalance(i.author(), _blockReward * (8 + i.number() - m_currentBlock.number()) / 8);
        r += _blockReward / 32;
    }
    m_state.addBalance(m_currentBlock.author(), r);
}

void Block::performIrregularModifications()
{
    u256 daoHardfork = m_sealEngine->chainParams().u256Param("daoHardforkBlock");
    if (daoHardfork != 0 && info().number() == daoHardfork)
    {
        Address recipient("0xbf4ed7b27f1d666546e30d74d50d173d20bca754");
        Addresses allDAOs = childDaos();
        for (Address const& dao : allDAOs)
            m_state.transferBalance(dao, recipient, m_state.balance(dao));
        m_state.commit(State::CommitBehaviour::KeepEmptyAccounts);
    }
}

void Block::commitToSeal(BlockChain const& _bc, bytes const& _extraData)
{
    if (isSealed())
        BOOST_THROW_EXCEPTION(InvalidOperationOnSealedBlock());

    noteChain(_bc);

    if (m_committedToSeal)
        uncommitToSeal();
    else
        m_precommit = m_state;

    vector<BlockHeader> uncleBlockHeaders;

    RLPStream unclesData;
    unsigned unclesCount = 0;
    if (m_previousBlock.number() != 0)
    {
        // Find great-uncles (or second-cousins or whatever they are) - children of great-grandparents, great-great-grandparents... that were not already uncles in previous generations.
        LOG(TRACE) << "Checking " << m_previousBlock.hash() << ", parent=" << m_previousBlock.parentHash();
        h256Hash excluded = _bc.allKinFrom(m_currentBlock.parentHash(), 6);
        auto p = m_previousBlock.parentHash();
        for (unsigned gen = 0; gen < 6 && p != _bc.genesisHash() && unclesCount < 2; ++gen, p = _bc.details(p).parent)
        {
            auto us = _bc.details(p).children;
            assert(us.size() >= 1); // must be at least 1 child of our grandparent - it's our own parent!
            for (auto const& u : us)
                if (!excluded.count(u)) // ignore any uncles/mainline blocks that we know about.
                {
                    uncleBlockHeaders.push_back(_bc.info(u));
                    unclesData.appendRaw(_bc.headerData(u));
                    ++unclesCount;
                    if (unclesCount == 2)
                        break;
                    excluded.insert(u);
                }
        }
    }

    BytesMap transactionsMap;
    BytesMap receiptsMap;

    RLPStream txs;
    txs.appendList(m_transactions.size());

    for (unsigned i = 0; i < m_transactions.size(); ++i)
    {
        RLPStream k;
        k << i;

        if (m_receipts.size() > i) { // parallel Pbft first time pack transction has not receipt
            RLPStream receiptrlp; 
            m_receipts[i].streamRLP(receiptrlp);
            receiptsMap.insert(std::make_pair(k.out(), receiptrlp.out()));
        }

        RLPStream txrlp;
        m_transactions[i].streamRLP(txrlp);
        transactionsMap.insert(std::make_pair(k.out(), txrlp.out()));

        txs.appendRaw(txrlp.out());

//#if ETH_PARANOIA
        /*      if (fromPending(i).transactionsFrom(m_transactions[i].from()) != m_transactions[i].nonce())
                {
                    LOG(WARNING) << "GAAA Something went wrong! " << fromPending(i).transactionsFrom(m_transactions[i].from()) << "!=" << m_transactions[i].nonce();
                }*/
//#endif
    }

    txs.swapOut(m_currentTxs);

    RLPStream(unclesCount).appendRaw(unclesData.out(), unclesCount).swapOut(m_currentUncles);


    DEV_TIMED_ABOVE("commitToSeal:state commit timecost=", 500)
    m_state.commit(State::CommitBehaviour::KeepEmptyAccounts);// 不要RemoveEmptyAccounts了

    m_currentBlock.setLogBloom(logBloom());
    m_currentBlock.setGasUsed(gasUsed());
    m_currentBlock.setRoots(hash256(transactionsMap), hash256(receiptsMap), sha3(m_currentUncles), m_state.rootHash());
    LOG(TRACE) << "Block::commitToSeal() number:" << m_currentBlock.number();
    h256 UTXOHash = m_utxoMgr.getHash();
    h256s hashList = m_currentBlock.hashList();
    if (hashList.size() == 0)
    {
        hashList.push_back(UTXOHash);
    }
    else if (hashList.size() >= 1)
    {
        hashList[0] = UTXOHash;
    }
    m_currentBlock.setHashList(hashList);

    m_currentBlock.setParentHash(m_previousBlock.hash());
    m_currentBlock.setExtraData(_extraData);
    if (m_currentBlock.extraData().size() > 32)
    {
        auto ed = m_currentBlock.extraData();
        ed.resize(32);
        m_currentBlock.setExtraData(ed);
    }

    m_committedToSeal = true;
}

void Block::commitToSealAfterExecTx(BlockChain const&) {
    BytesMap receiptsMap;
    for (unsigned i = 0; i < m_receipts.size(); ++i) {
        RLPStream receiptrlp;
        m_receipts[i].streamRLP(receiptrlp);
        RLPStream k;
        k << i;
        receiptsMap.insert(std::make_pair(k.out(), receiptrlp.out()));
    }

    //bool removeEmptyAccounts = m_currentBlock.number() >= _bc.chainParams().u256Param("EIP158ForkBlock");
    DEV_TIMED_ABOVE("commitToSealAfterExecTx:state commit timecost=", 500)
    m_state.commit(State::CommitBehaviour::KeepEmptyAccounts);

    //LOG(INFO) << "Post-reward stateRoot:" << m_state.rootHash();
    //LOG(INFO) << m_state;
    //LOG(INFO) << *this;

    m_currentBlock.setLogBloom(logBloom());
    m_currentBlock.setGasUsed(gasUsed());
    m_currentBlock.setRoots(m_currentBlock.transactionsRoot(), hash256(receiptsMap), m_currentBlock.sha3Uncles(), m_state.rootHash());
    LOG(TRACE) << "Block::commitToSealAfterExecTx() number:" << m_currentBlock.number();
    h256 UTXOHash = m_utxoMgr.getHash();
    h256s hashList = m_currentBlock.hashList();
    if (hashList.size() == 0)
    {
        hashList.push_back(UTXOHash);
    }
    else if (hashList.size() >= 1)
    {
        hashList[0] = UTXOHash;
    }
    m_currentBlock.setHashList(hashList);

    m_committedToSeal = true;
}

void Block::uncommitToSeal()
{
    if (m_committedToSeal)
    {
        m_state = m_precommit;
        m_committedToSeal = false;
    }
}

bool Block::sealBlock(bytesConstRef _header)
{
    
    if (!sealBlock(_header, m_currentBytes)) {
        return false;
    }
    m_currentBlock = BlockHeader(_header, HeaderData);
//  LOG(INFO) << "Mined " << m_currentBlock.hash() << "(parent: " << m_currentBlock.parentHash() << ")";
    // TODO: move into SealEngine

    m_state = m_precommit;

    // m_currentBytes is now non-empty; we're in a sealed state so no more transactions can be added.

    return true;
}

bool Block::sealBlock(bytesConstRef _header, bytes & _out)
{
    if (!m_committedToSeal) {
        LOG(DEBUG) << "sealBlock return false, for m_committedToSeal is false";
        return false;
    }

    auto tmpBlock = BlockHeader(_header, HeaderData);
    if (tmpBlock.hash(WithoutSeal) != m_currentBlock.hash(WithoutSeal)) {
        LOG(DEBUG) << "sealBlock return false, for tmpBlock=" << tmpBlock.hash(WithoutSeal) << ",m_currentBlock=" << m_currentBlock.hash(WithoutSeal);
        return false;
    }

    LOG(INFO) << "Sealing block!";

    // Compile block:
    RLPStream ret;
    ret.appendList(5);
    ret.appendRaw(_header);
    ret.appendRaw(m_currentTxs);
    ret.appendRaw(m_currentUncles);
    
    ret.append(m_currentBlock.hash(WithoutSeal));
   
    std::vector<std::pair<u256, Signature>> sig_list;
    ret.appendVector(sig_list);

    ret.swapOut(_out);

    return true;
}



State Block::fromPending(unsigned _i) const
{
    State ret = m_state;
    _i = min<unsigned>(_i, m_transactions.size());
    if (!_i)
        ret.setRoot(m_previousBlock.stateRoot());
    else
        ret.setRoot(m_receipts[_i - 1].stateRoot());
    return ret;
}

LogBloom Block::logBloom() const
{
    LogBloom ret;
    for (TransactionReceipt const& i : m_receipts)
        ret |= i.bloom();
    return ret;
}

void Block::cleanup(bool _fullCommit)
{
    if (_fullCommit)
    {
        // Commit the new trie to disk.
        LOG(TRACE) << "Committing to disk: stateRoot" << m_currentBlock.stateRoot() << "=" << rootHash() << "=" << toHex(asBytes(db().lookup(rootHash())));

        try
        {
            EnforceRefs er(db(), true);
            rootHash();
        }
        catch (BadRoot const&)
        {
            LOG(WARNING) << "Trie corrupt! :-(";
            throw;
        }


        m_state.db().commit();  // TODO: State API for this?

        LOG(TRACE) << "Committed: stateRoot" << m_currentBlock.stateRoot() << "=" << rootHash() << "=" << toHex(asBytes(db().lookup(rootHash())));


        m_previousBlock = m_currentBlock;
        sealEngine()->populateFromParent(m_currentBlock, m_previousBlock);


        LOG(TRACE) << "finalising enactment. current -> previous, hash is" << m_previousBlock.hash();
    }
    else
        m_state.db().rollback();    // TODO: State API for this?

    resetCurrent();
}

void Block::commitAll() {
    // Commit the new trie to disk.
    LOG(TRACE) << "Committing to disk: stateRoot" << m_currentBlock.stateRoot() << "=" << rootHash() << "=" << toHex(asBytes(db().lookup(rootHash())));
    LOG(TRACE) << "Block::commitAll() number:" << m_currentBlock.number();
    m_utxoMgr.commitDB();

    try
    {
        EnforceRefs er(db(), true);
        rootHash();
    }
    catch (BadRoot const&)
    {
        LOG(WARNING) << "Trie corrupt! :-(";
        throw;
    }

    m_state.db().commit();  // TODO: State API for this?

    LOG(TRACE) << "Committed: stateRoot" << m_currentBlock.stateRoot() << "=" << rootHash() << "=" << toHex(asBytes(db().lookup(rootHash())));

    //m_previousBlock = m_currentBlock;
    //sealEngine()->populateFromParent(m_currentBlock, m_previousBlock);

    LOG(TRACE) << "finalising enactment. current -> previous, hash is" << m_previousBlock.hash();
}

void dev::eth::Block::clearCurrentBytes() {
    m_currentBytes.clear();
}


string Block::vmTrace(bytesConstRef _block, BlockChain const& _bc, ImportRequirements::value _ir)
{
    noteChain(_bc);

    RLP rlp(_block);

    cleanup(false);
    BlockHeader bi(_block);
    m_currentBlock = bi;
    m_currentBlock.verify((_ir & ImportRequirements::ValidSeal) ? CheckEverything : IgnoreSeal, _block);
    m_currentBlock.noteDirty();

    LastHashes lh = _bc.lastHashes(m_currentBlock.parentHash());

    string ret;
    unsigned i = 0;
    for (auto const& tr : rlp[1])
    {
        StandardTrace st;
        st.setShowMnemonics();
        execute(lh, Transaction(tr.data(), CheckTransaction::Everything), Permanence::Committed, st.onOp());
        ret += (ret.empty() ? "[" : ",") + st.json();
        ++i;
    }
    return ret.empty() ? "[]" : (ret + "]");
}

void Block::getParallelUTXOTx(const Transactions& transactions, std::map<h256, bool>& ret, size_t& cnt)
{
    std::vector<bool> artificialTx;
    isArtificialTx(transactions, artificialTx);
    for (size_t i = 0; i < transactions.size(); i++)
    {
        const Transaction& tr = transactions[i];
        bool temp = (tr.getUTXOType() != UTXOType::InValid && !tr.isUTXOEvmTx() && !artificialTx[i]);
        ret[tr.sha3()] = temp;
        if (temp) { cnt++; }
    }
}

void Block::isArtificialTx(const Transactions& txList, std::vector<bool>& flag)
{
    std::vector<bool> tmp(txList.size(), false);
    flag = tmp;

    std::map<string, size_t> tokenKeyMap;
    for (size_t txIdx = 0; txIdx < txList.size(); txIdx++)
    {
        std::vector<UTXOModel::UTXOTxIn> inList = txList[txIdx].getUTXOTxIn();
        for(const UTXOModel::UTXOTxIn& in : inList)
        {
            if (tokenKeyMap.count(in.tokenKey))
            {
                flag[txIdx] = true;
                flag[tokenKeyMap[in.tokenKey]] = true;
            }
            tokenKeyMap[in.tokenKey] = txIdx;
        }
    }
}

std::ostream& dev::eth::operator<<(std::ostream& _out, Block const& _s)
{
    (void)_s;
    return _out;
}
