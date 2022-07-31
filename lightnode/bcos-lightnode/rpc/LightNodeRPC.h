#pragma once

#include <bcos-tars-protocol/impl/TarsHashable.h>

#include "Converter.h"
#include "bcos-concepts/Hash.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/transaction_pool/TransactionPool.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace bcos::rpc
{

template <bcos::concepts::ledger::Ledger LocalLedgerType,
    bcos::concepts::ledger::Ledger RemoteLedgerType,
    bcos::concepts::transacton_pool::TransactionPool TransactionPoolType,
    bcos::crypto::hasher::Hasher Hasher>
class LightNodeRPC : public bcos::rpc::JsonRpcInterface
{
public:
    LightNodeRPC(LocalLedgerType localLedger, RemoteLedgerType remoteLedger,
        TransactionPoolType remoteTransactionPool, std::string chainID, std::string groupID)
      : m_localLedger(std::move(localLedger)),
        m_remoteLedger(std::move(remoteLedger)),
        m_remoteTransactionPool(std::move(remoteTransactionPool)),
        m_chainID(std::move(chainID)),
        m_groupID(std::move(groupID))
    {}

    void call([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _to,
        [[maybe_unused]] std::string_view _data, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void sendTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _data,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        auto binData = decodeData(_data);
        bcostars::Transaction transaction;
        bcos::concepts::serialize::decode(binData, transaction);

        bcos::bytes txHash;
        bcos::concepts::hash::calculate<Hasher>(transaction, txHash);
        std::string txHashStr;
        txHashStr.reserve(txHash.size() * 2);
        boost::algorithm::hex_lower(txHash.begin(), txHash.end(), std::back_inserter(txHashStr));

        bcostars::TransactionReceipt receipt;
        remoteTransactionPool().submitTransaction(std::move(transaction), receipt);

        Json::Value resp;
        toJsonResp<Hasher>(receipt, txHashStr, resp);

        _respFunc(nullptr, resp);
    }

    void getTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        std::array<bcos::h256, 1> hashes{bcos::h256{_txHash, bcos::h256::FromHex}};
        std::vector<bcostars::Transaction> transactions;
        remoteLedger().getTransactionsOrReceipts(hashes, transactions);

        Json::Value resp;
        toJsonResp<Hasher>(transactions[0], resp);

        _respFunc(nullptr, resp);
    }

    void getTransactionReceipt([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        std::array<bcos::h256, 1> hashes{bcos::h256{_txHash, bcos::h256::FromHex}};
        std::vector<bcostars::TransactionReceipt> receipts;
        remoteLedger().getTransactionsOrReceipts(hashes, receipts);

        Json::Value resp;
        toJsonResp<Hasher>(receipts[0], _txHash, resp);

        _respFunc(nullptr, resp);
    }

    void getBlockByHash([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _blockHash,
        [[maybe_unused]] bool _onlyHeader, [[maybe_unused]] bool _onlyTxHash,
        RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getBlockByNumber([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, int64_t _blockNumber, bool _onlyHeader,
        bool _onlyTxHash, RespFunc _respFunc) override
    {
        bcostars::Block block;
        remoteLedger().template getBlock<bcos::concepts::ledger::HEADER>(_blockNumber, block);

        Json::Value resp;
        toJsonResp<Hasher>(block, resp);

        _respFunc(nullptr, resp);
    }

    void getBlockHashByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getBlockNumber(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        auto count = localLedger().getStatus();

        Json::Value resp = count.blockNumber;
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
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getGroupBlockNumber(RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

private:
    auto& localLedger() { return bcos::concepts::getRef(m_localLedger); }
    auto& remoteLedger() { return bcos::concepts::getRef(m_remoteLedger); }
    auto& remoteTransactionPool() { return bcos::concepts::getRef(m_remoteTransactionPool); }
    bcos::bytes decodeData(std::string_view _data)
    {
        auto begin = _data.begin();
        auto end = _data.end();
        auto length = _data.size();

        if ((length == 0) || (length % 2 != 0)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"Unexpect hex string"});
        }

        if (*begin == '0' && *(begin + 1) == 'x')
        {
            begin += 2;
            length -= 2;
        }

        bcos::bytes data;
        data.reserve(length / 2);
        boost::algorithm::unhex(begin, end, std::back_inserter(data));
        return data;
    }

    LocalLedgerType m_localLedger;
    RemoteLedgerType m_remoteLedger;
    TransactionPoolType m_remoteTransactionPool;
    std::string m_chainID;
    std::string m_groupID;
};
}  // namespace bcos::rpc