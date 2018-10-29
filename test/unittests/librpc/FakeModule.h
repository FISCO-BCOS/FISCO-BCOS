/**
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
 *
 * @brief : main Fakes of p2p module
 *
 * @file FakeModule.h
 * @author: caryliao
 * @date 2018-10-26
 */
#pragma once

#include <jsonrpccpp/common/exception.h>
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libconsensus/ConsensusInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libsync/SyncInterface.h>
#include <libledger/LedgerManager.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/easylog.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libexecutive/ExecutionResult.h>
#include <libtxpool/TxPoolInterface.h>
#include <test/tools/libutils/Common.h>

using namespace dev;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::sync;
using namespace dev::ledger;

namespace dev
{
namespace test
{

class FakeService : public Service
{
public:
	FakeService(std::shared_ptr<Host> _host, std::shared_ptr<P2PMsgHandler> _p2pMsgHandler)
	      : Service(_host, _p2pMsgHandler)
    {

    }

	SessionInfos sessionInfos()
	{
		  SessionInfos infos;
//		  infos.push_back(SessionInfo(h512, i.second->nodeIPEndpoint(), *(i.second->topics())));
//		  infos.push_back(SessionInfo(i.first, i.second->nodeIPEndpoint(), *(i.second->topics())));
		  return infos;
	}
};

class FakeBlockChain : public BlockChainInterface
{
public:
	FakeBlockChain()
    {
        m_blockNumber = 1;
        bytes m_blockHeaderData = bytes();
        bytes m_blockData = bytes();
        BlockHeader blockHeader;
        blockHeader.setSealer(u256(1));
        blockHeader.setNumber(0);
        blockHeader.setTimestamp(0);
        Block block;
        blockHeader.encode(m_blockHeaderData);
        block.encode(m_blockData, ref(m_blockHeaderData));
        block.decode(ref(m_blockData));
        m_blockHash[block.blockHeaderHash()] = 0;
        m_blockChain.push_back(std::make_shared<Block>(block));
    }

	virtual ~FakeBlockChain() {}

    int64_t number() const { return m_blockNumber; }

    dev::h256 numberHash(int64_t _i) const { return m_blockChain[_i]->headerHash(); }

    std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }
    dev::eth::LocalisedTransaction getLocalisedTxByHash(dev::h256 const& _txHash) override
    {
        return LocalisedTransaction();
    }
    dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) override { return Transaction(); }
    dev::eth::TransactionReceipt getTransactionReceiptByHash(dev::h256 const& _txHash) override
    {
        return TransactionReceipt();
    }

    std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i));
    }

    void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override
    {
        std::cout << "##### commitBlock:" << block.blockHeader().number() << std::endl;
        m_blockHash[block.blockHeader().hash()] = block.blockHeader().number();
        m_blockChain.push_back(std::make_shared<Block>(block));
        m_blockNumber = block.blockHeader().number() + 1;
        m_onReady();
    }

    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
};

class FakeBlockVerifier : public BlockVerifierInterface
{
public:
	FakeBlockVerifier()
    {
        m_executiveContext = std::make_shared<ExecutiveContext>();
        std::srand(std::time(nullptr));
    };
    virtual ~ FakeBlockVerifier(){};
    std::shared_ptr<ExecutiveContext> executeBlock(dev::eth::Block& block) override
    {
        /// execute time: 1000
        usleep(1000 * (block.getTransactionSize()));
        return m_executiveContext;
    };
    virtual std::pair<dev::executive::ExecutionResult, dev::eth::TransactionReceipt> executeTransaction(
        const dev::eth::BlockHeader& blockHeader, dev::eth::Transaction const& _t)
    {
        dev::executive::ExecutionResult res;
        dev::eth::TransactionReceipt reciept;
        return std::make_pair(res, reciept);
    }

private:
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};

class FakeLedger : public Ledger
{
public:
    FakeLedger(std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair, std::string const& _baseDir, std::string const& _configFile)
      : Ledger(service, _groupId, _keyPair, _baseDir, _configFile)
    {
    	/// init blockChain
    	initBlockChain();
		/// init blockVerifier
		initBlockVerifier();
		/// init txPool
//		initTxPool();
    }

    /// init blockverifier related
    void initBlockChain() override { m_blockChain = std::make_shared<FakeBlockChain>(); }
    void initBlockVerifier() override { m_blockVerifier = std::make_shared<FakeBlockVerifier>(); }
};

}  // namespace test
}  // namespace dev
