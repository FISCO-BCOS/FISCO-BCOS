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

/**
 * @brief : external interface of blockchain
 * @author: mingzhenliu
 * @date: 2018-09-21
 */
#pragma once

#include <libprotocol/Block.h>
#include <libprotocol/Transaction.h>
#include <libprotocol/TransactionReceipt.h>
#include <libutilities/CallbackCollectionHandler.h>
#include <libutilities/FixedBytes.h>
namespace bcos
{
namespace ledger
{
class LedgerParamInterface;
}

namespace blockverifier
{
class ExecutiveContext;
}  // namespace blockverifier
namespace blockchain
{
enum class CommitResult
{
    OK = 0,             // 0
    ERROR_NUMBER = -1,  // 1
    ERROR_PARENT_HASH = -2,
    ERROR_COMMITTING = -3
};
using MerkleProofType = std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>;

class BlockChainInterface
{
public:
    using Ptr = std::shared_ptr<BlockChainInterface>;
    BlockChainInterface() = default;
    virtual ~BlockChainInterface(){};
    virtual int64_t number() = 0;
    virtual bcos::h256 numberHash(int64_t _i) = 0;
    virtual bcos::protocol::Transaction::Ptr getTxByHash(bcos::h256 const& _txHash) = 0;
    virtual bcos::protocol::LocalisedTransaction::Ptr getLocalisedTxByHash(
        bcos::h256 const& _txHash) = 0;
    virtual bcos::protocol::TransactionReceipt::Ptr getTransactionReceiptByHash(
        bcos::h256 const& _txHash) = 0;
    virtual bcos::protocol::LocalisedTransactionReceipt::Ptr getLocalisedTxReceiptByHash(
        bcos::h256 const& _txHash) = 0;
    virtual std::shared_ptr<bcos::protocol::Block> getBlockByHash(
        bcos::h256 const& _blockHash, int64_t _blockNumber = -1) = 0;
    virtual std::shared_ptr<bcos::protocol::Block> getBlockByNumber(int64_t _i) = 0;
    virtual std::shared_ptr<bcos::bytes> getBlockRLPByNumber(int64_t _i) = 0;
    virtual CommitResult commitBlock(std::shared_ptr<bcos::protocol::Block> block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext>) = 0;
    virtual std::pair<int64_t, int64_t> totalTransactionCount() = 0;
    virtual std::pair<int64_t, int64_t> totalFailedTransactionCount() = 0;
    virtual bcos::bytes getCode(bcos::Address _address) = 0;
    virtual std::shared_ptr<std::vector<bcos::protocol::NonceKeyType>> getNonces(
        int64_t _blockNumber) = 0;

    virtual std::pair<bcos::protocol::LocalisedTransaction::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionByHashWithProof(bcos::h256 const& _txHash) = 0;


    virtual std::pair<bcos::protocol::LocalisedTransactionReceipt::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionReceiptByHashWithProof(
        bcos::h256 const& _txHash, bcos::protocol::LocalisedTransaction& _transaction) = 0;

    /// If it is a genesis block, function returns true.
    /// If it is a subsequent block with same extra data, function returns true.
    /// Returns an error in the rest of the cases.
    virtual bool checkAndBuildGenesisBlock(
        std::shared_ptr<bcos::ledger::LedgerParamInterface>, bool _shouldBuild = true) = 0;

    /// get sealer, workingSealer or observer nodes
    virtual bcos::h512s sealerList() = 0;
    virtual bcos::h512s observerList() = 0;
    virtual bcos::h512s workingSealerList() { return bcos::h512s(); }
    virtual bcos::h512s pendingSealerList() { return bcos::h512s(); }

    /// get system config
    virtual std::string getSystemConfigByKey(std::string const& key, int64_t number = -1) = 0;
    virtual std::pair<std::string, bcos::protocol::BlockNumber> getSystemConfigInfoByKey(
        std::string const&, int64_t const& _num = -1)
    {
        return std::make_pair("", _num);
    }

    /// Register a handler that will be called once there is a new transaction imported
    template <class T>
    bcos::Handler<int64_t> onReady(T const& _t)
    {
        return m_onReady.add(_t);
    }

    virtual std::shared_ptr<MerkleProofType> getTransactionReceiptProof(
        bcos::protocol::Block::Ptr, uint64_t const&)
    {
        return nullptr;
    }

    virtual std::shared_ptr<MerkleProofType> getTransactionProof(
        bcos::protocol::Block::Ptr, uint64_t const&)
    {
        return nullptr;
    }

    virtual std::shared_ptr<std::pair<std::shared_ptr<bcos::protocol::BlockHeader>,
        bcos::protocol::Block::SigListPtrType>>
        getBlockHeaderInfo(int64_t)
    {
        return nullptr;
    }
    virtual std::shared_ptr<std::pair<std::shared_ptr<bcos::protocol::BlockHeader>,
        bcos::protocol::Block::SigListPtrType>>
    getBlockHeaderInfoByHash(bcos::h256 const&)
    {
        return nullptr;
    }

protected:
    ///< Called when a subsequent call to import transactions will return a non-empty container. Be
    ///< nice and exit fast.
    bcos::CallbackCollectionHandler<int64_t> m_onReady;
};
}  // namespace blockchain
}  // namespace bcos
