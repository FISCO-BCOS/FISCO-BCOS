#pragma once

#include <bcos-tars-protocol/impl/TarsHashable.h>

#include "../Log.h"
#include "Converter.h"
#include "bcos-concepts/Basic.h"
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Hash.h"
#include "bcos-tars-protocol/tars/TransactionMetaData.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/scheduler/Scheduler.h>
#include <bcos-concepts/transaction_pool/TransactionPool.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <json/value.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>

namespace bcos::rpc
{

template <bcos::concepts::ledger::Ledger LocalLedgerType,
    bcos::concepts::ledger::Ledger RemoteLedgerType,
    bcos::concepts::transacton_pool::TransactionPool TransactionPoolType,
    bcos::concepts::scheduler::Scheduler SchedulerType, bcos::crypto::hasher::Hasher Hasher>
class LightNodeRPC : public bcos::rpc::JsonRpcInterface
{
public:
    LightNodeRPC(LocalLedgerType localLedger, RemoteLedgerType remoteLedger,
        TransactionPoolType remoteTransactionPool, SchedulerType scheduler, std::string chainID,
        std::string groupID)
      : m_localLedger(std::move(localLedger)),
        m_remoteLedger(std::move(remoteLedger)),
        m_remoteTransactionPool(std::move(remoteTransactionPool)),
        m_scheduler(std::move(scheduler)),
        m_chainID(std::move(chainID)),
        m_groupID(std::move(groupID))
    {}

    void call([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view to,
        [[maybe_unused]] std::string_view hexTransaction, RespFunc respFunc) override
    {
        // call data is json
        bcostars::Transaction transaction;
        decodeData(hexTransaction, transaction.data.input);
        transaction.data.to = to;
        transaction.data.nonce = "0";
        transaction.data.blockLimit = 0;
        transaction.data.chainID = "";
        transaction.data.groupID = "";
        transaction.importTime = 0;

        LIGHTNODE_LOG(INFO) << "RPC call request, to: " << to;

        bcostars::TransactionReceipt receipt;
        scheduler().call(transaction, receipt);

        Json::Value resp;
        toJsonResp<Hasher>(receipt, {}, resp);

        LIGHTNODE_LOG(INFO) << "RPC call transaction finished";
        respFunc(nullptr, resp);
    }

    void sendTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, std::string_view hexTransaction,
        [[maybe_unused]] bool requireProof, RespFunc respFunc) override
    {
        bcos::bytes binData;
        decodeData(hexTransaction, binData);
        bcostars::Transaction transaction;
        bcos::concepts::serialize::decode(binData, transaction);

        bcos::bytes txHash;
        bcos::concepts::hash::calculate<Hasher>(transaction, txHash);
        std::string txHashStr;
        txHashStr.reserve(txHash.size() * 2);
        boost::algorithm::hex_lower(txHash.begin(), txHash.end(), std::back_inserter(txHashStr));

        LIGHTNODE_LOG(INFO) << "RPC send transaction request: "
                            << "0x" << txHashStr;

        bcostars::TransactionReceipt receipt;
        remoteTransactionPool().submitTransaction(std::move(transaction), receipt);

        Json::Value resp;
        toJsonResp<Hasher>(receipt, txHashStr, resp);

        LIGHTNODE_LOG(INFO) << "RPC send transaction finished";
        respFunc(nullptr, resp);
    }

    void getTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get transaction request: " << txHash;

        std::array<bcos::h256, 1> hashes{bcos::h256{txHash, bcos::h256::FromHex}};
        std::vector<bcostars::Transaction> transactions;
        remoteLedger().getTransactions(hashes, transactions);

        Json::Value resp;
        toJsonResp<Hasher>(transactions[0], resp);

        LIGHTNODE_LOG(INFO) << "RPC get transaction finished";
        _respFunc(nullptr, resp);
    }

    void getTransactionReceipt([[maybe_unused]] std::string_view groupID,
        [[maybe_unused]] std::string_view nodeName, [[maybe_unused]] std::string_view txHash,
        [[maybe_unused]] bool requireProof, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get receipt request: " << txHash;

        std::array<bcos::h256, 1> hashes{bcos::h256{txHash, bcos::h256::FromHex}};
        std::vector<bcostars::TransactionReceipt> receipts;
        remoteLedger().getTransactions(hashes, receipts);

        Json::Value resp;
        toJsonResp<Hasher>(receipts[0], txHash, resp);

        _respFunc(nullptr, resp);
    }

    void getBlockByHash([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view blockHash,
        [[maybe_unused]] bool _onlyHeader, [[maybe_unused]] bool _onlyTxHash,
        RespFunc _respFunc) override
    {
        int64_t blockNumber = -1;
        std::array<std::byte, Hasher::HASH_SIZE> hash;
        hex2Bin(blockHash, hash);

        localLedger().getBlockNumberByHash(hash, blockNumber);

        LIGHTNODE_LOG(INFO) << "RPC get block by hash request: 0x" << blockHash << " "
                            << blockNumber << " " << _onlyHeader;
        if (blockNumber < 0)
            BOOST_THROW_EXCEPTION(std::runtime_error{"Unable to find block hash!"});

        getBlockByNumber(
            _groupID, _nodeName, blockNumber, _onlyHeader, _onlyTxHash, std::move(_respFunc));
    }

    void getBlockByNumber([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, int64_t _blockNumber, bool _onlyHeader,
        bool _onlyTxHash, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block by number request: " << _blockNumber << " "
                            << _onlyHeader;

        bcostars::Block block;
        if (_onlyHeader)
        {
            localLedger().template getBlock<bcos::concepts::ledger::HEADER>(_blockNumber, block);
        }
        else
        {
            remoteLedger().template getBlock<bcos::concepts::ledger::ALL>(_blockNumber, block);

            if (!RANGES::empty(block.transactionsMetaData))
            {
                // Check transaction merkle
                crypto::merkle::Merkle<Hasher> merkle;
                auto hashesRange = block.transactionsMetaData | RANGES::views::transform([
                ](const bcostars::TransactionMetaData& transactionMetaData) -> auto& {
                    return transactionMetaData.hash;
                });
                std::vector<std::array<std::byte, Hasher::HASH_SIZE>> merkles;
                merkle.generateMerkle(hashesRange, merkles);

                if (RANGES::empty(merkles))
                    BOOST_THROW_EXCEPTION(
                        std::runtime_error{"Unable to generate transaction merkle root!"});

                if (!bcos::concepts::bytebuffer::equalTo(
                        block.blockHeader.data.txsRoot, *RANGES::rbegin(merkles)))
                    BOOST_THROW_EXCEPTION(std::runtime_error{"No match transaction root!"});
            }

            // Check parentBlock
            if (_blockNumber > 0)
            {
                decltype(block) parentBlock;
                localLedger().template getBlock<bcos::concepts::ledger::HEADER>(
                    _blockNumber - 1, parentBlock);

                std::array<std::byte, Hasher::HASH_SIZE> parentHash;
                bcos::concepts::hash::calculate<Hasher>(parentBlock, parentHash);

                if (RANGES::empty(block.blockHeader.data.parentInfo) ||
                    (block.blockHeader.data.parentInfo[0].blockNumber !=
                        parentBlock.blockHeader.data.blockNumber) ||
                    !bcos::concepts::bytebuffer::equalTo(
                        block.blockHeader.data.parentInfo[0].blockHash, parentHash))
                {
                    LIGHTNODE_LOG(ERROR) << "No match parentHash!";
                    BOOST_THROW_EXCEPTION(std::runtime_error{"No match parentHash!"});
                }
            }
        }

        Json::Value resp;
        toJsonResp<Hasher>(block, resp, _onlyHeader);

        _respFunc(nullptr, resp);
    }

    void getBlockHashByNumber([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, int64_t blockNumber,
        RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block hash by number request: " << blockNumber;

        bcos::h256 hash;
        localLedger().getBlockHashByNumber(blockNumber, hash);

        Json::Value resp = bcos::toHexStringWithPrefix(hash);

        respFunc(nullptr, resp);
    }

    void getBlockNumber(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block number request";

        auto count = localLedger().getStatus();

        Json::Value resp = count.blockNumber;

        LIGHTNODE_LOG(INFO) << "RPC get block number finished: " << count.blockNumber;
        _respFunc(nullptr, resp);
    }

    void getCode(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getABI(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getSealerList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getObserverList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getPbftView(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getPendingTxSize(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getSyncStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getConsensusStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getSystemConfigByKey(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _keyValue, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getTotalTransactionCount(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getGroupPeers(std::string_view _groupID, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getPeers(RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getGroupList(RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getGroupInfo(std::string_view _groupID, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getGroupInfoList(RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getGroupNodeInfo(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get group node info request";

        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getGroupBlockNumber(RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get group block number request";

        auto count = localLedger().getStatus();

        Json::Value resp(Json::objectValue);
        resp[m_groupID] = count.blockNumber;

        _respFunc(nullptr, resp);
    }

private:
    auto& localLedger() { return bcos::concepts::getRef(m_localLedger); }
    auto& remoteLedger() { return bcos::concepts::getRef(m_remoteLedger); }
    auto& remoteTransactionPool() { return bcos::concepts::getRef(m_remoteTransactionPool); }
    auto& scheduler() { return bcos::concepts::getRef(m_scheduler); }

    void decodeData(bcos::concepts::bytebuffer::ByteBuffer auto const& input,
        bcos::concepts::bytebuffer::ByteBuffer auto& out)
    {
        auto begin = RANGES::begin(input);
        auto end = RANGES::end(input);
        auto length = RANGES::size(input);

        if ((length == 0) || (length % 2 != 0)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"Unexpect hex string"});
        }

        if (*begin == '0' && *(begin + 1) == 'x')
        {
            begin += 2;
            length -= 2;
        }

        bcos::concepts::resizeTo(out, length / 2);
        boost::algorithm::unhex(begin, end, RANGES::begin(out));
    }

    LocalLedgerType m_localLedger;
    RemoteLedgerType m_remoteLedger;
    TransactionPoolType m_remoteTransactionPool;
    SchedulerType m_scheduler;

    std::string m_chainID;
    std::string m_groupID;
};
}  // namespace bcos::rpc