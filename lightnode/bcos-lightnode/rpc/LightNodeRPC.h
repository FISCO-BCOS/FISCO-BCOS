#pragma once

#include <bcos-tars-protocol/impl/TarsHashable.h>

#include "../Log.h"
#include "Converter.h"
#include "bcos-concepts/Basic.h"
#include "bcos-concepts/Hash.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/scheduler/Scheduler.h>
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
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _to,
        [[maybe_unused]] std::string_view _data, RespFunc _respFunc) override
    {
        // call data is json
        bcostars::Transaction transaction;
        decodeData(_data, transaction.data.input);
        transaction.data.to = _to;
        transaction.data.nonce = "0";
        transaction.data.blockLimit = 0;
        transaction.data.chainID = "";
        transaction.data.groupID = "";
        transaction.importTime = 0;

        bcos::bytes txHash;
        bcos::concepts::hash::calculate<Hasher>(transaction, txHash);
        std::string txHashStr;
        txHashStr.reserve(txHash.size() * 2);
        boost::algorithm::hex_lower(txHash.begin(), txHash.end(), std::back_inserter(txHashStr));

        LIGHTNODE_LOG(INFO) << "RPC call request: " << txHashStr;

        bcostars::TransactionReceipt receipt;
        scheduler().call(transaction, receipt);

        Json::Value resp;
        toJsonResp<Hasher>(receipt, txHashStr, resp);

        LIGHTNODE_LOG(INFO) << "RPC send transaction finished";
        _respFunc(nullptr, resp);
    }

    void sendTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _data,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        bcos::bytes binData;
        decodeData(_data, binData);
        bcostars::Transaction transaction;
        bcos::concepts::serialize::decode(binData, transaction);

        bcos::bytes txHash;
        bcos::concepts::hash::calculate<Hasher>(transaction, txHash);
        std::string txHashStr;
        txHashStr.reserve(txHash.size() * 2);
        boost::algorithm::hex_lower(txHash.begin(), txHash.end(), std::back_inserter(txHashStr));

        LIGHTNODE_LOG(INFO) << "RPC send transaction request: " << txHashStr;

        bcostars::TransactionReceipt receipt;
        remoteTransactionPool().submitTransaction(std::move(transaction), receipt);

        Json::Value resp;
        toJsonResp<Hasher>(receipt, txHashStr, resp);

        LIGHTNODE_LOG(INFO) << "RPC send transaction finished";
        _respFunc(nullptr, resp);
    }

    void getTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get transaction request";

        std::array<bcos::h256, 1> hashes{bcos::h256{_txHash, bcos::h256::FromHex}};
        std::vector<bcostars::Transaction> transactions;
        remoteLedger().getTransactionsOrReceipts(hashes, transactions);

        Json::Value resp;
        toJsonResp<Hasher>(transactions[0], resp);

        LIGHTNODE_LOG(INFO) << "RPC get transaction finished";
        _respFunc(nullptr, resp);
    }

    void getTransactionReceipt([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view _txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get receipt request";

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
        LIGHTNODE_LOG(INFO) << "RPC get block by number request: " << _blockNumber << " "
                            << _onlyHeader;

        bcostars::Block block;
        localLedger().template getBlock<bcos::concepts::ledger::HEADER>(_blockNumber, block);

        Json::Value resp;
        toJsonResp<Hasher>(block, resp, _onlyHeader);

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
        LIGHTNODE_LOG(INFO) << "RPC get block number request";

        // auto count = localLedger().getStatus();
        auto count = remoteLedger().getStatus();

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

        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

private:
    auto& localLedger() { return bcos::concepts::getRef(m_localLedger); }
    auto& remoteLedger() { return bcos::concepts::getRef(m_remoteLedger); }
    auto& remoteTransactionPool() { return bcos::concepts::getRef(m_remoteTransactionPool); }
    auto& scheduler() { return bcos::concepts::getRef(m_scheduler); }

    void decodeData(std::string_view _data, bcos::concepts::bytebuffer::ByteBuffer auto& out)
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

        bcos::concepts::bytebuffer::resizeTo(out, length / 2);
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