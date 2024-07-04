#pragma once

#include "../Log.h"
#include "Converter.h"
#include "bcos-concepts/Basic.h"
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Exception.h"
#include "bcos-concepts/Hash.h"
#include "bcos-tars-protocol/impl/TarsSerializable.h"
#include "bcos-tars-protocol/tars/TransactionMetaData.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/scheduler/Scheduler.h>
#include <bcos-concepts/transaction-pool/TransactionPool.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <bcos-tars-protocol/impl/TarsHashable.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-task/Wait.h>
#include <json/value.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace bcos::rpc
{
// clang-format off
struct NotFoundTransactionHash: public bcos::error::Exception {};
struct CheckMerkleRootFailed: public bcos::error::Exception {};
// clang-format on

template <bcos::concepts::ledger::Ledger LocalLedgerType,
    bcos::concepts::ledger::Ledger RemoteLedgerType,
    bcos::concepts::transacton_pool::TransactionPool TransactionPoolType,
    bcos::concepts::scheduler::Scheduler SchedulerType, bcos::crypto::hasher::Hasher Hasher>
class LightNodeRPC : public bcos::rpc::JsonRpcInterface
{
public:
    LightNodeRPC(LocalLedgerType localLedger, RemoteLedgerType remoteLedger,
        TransactionPoolType remoteTransactionPool, SchedulerType scheduler, std::string chainID,
        std::string groupID, Hasher hasher)
      : m_localLedger(std::move(localLedger)),
        m_remoteLedger(std::move(remoteLedger)),
        m_remoteTransactionPool(std::move(remoteTransactionPool)),
        m_scheduler(std::move(scheduler)),
        m_chainID(std::move(chainID)),
        m_groupID(std::move(groupID)),
        m_hasher(std::move(hasher))
    {}

    void toErrorResp(std::exception_ptr error, RespFunc respFunc)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (std::exception& e)
        {
            toErrorResp(e, std::move(respFunc));
        }
    }

    void toErrorResp(std::exception& error, RespFunc respFunc)
    {
        try
        {
            auto& bcosError = dynamic_cast<bcos::Error&>(error);
            toErrorResp(std::make_shared<bcos ::Error>(std::move(bcosError)), std::move(respFunc));
        }
        catch ([[maybe_unused]] std::bad_cast&)
        {
            // no bcos error
            auto bcosError = BCOS_ERROR(-1, boost::diagnostic_information(error));
            toErrorResp(bcosError, std::move(respFunc));
        }
    }

    void toErrorResp(bcos::Error::Ptr error, RespFunc respFunc)
    {
        Json::Value resp;
        respFunc(std::move(error), resp);
    }

    void call([[maybe_unused]] std::string_view groupID, [[maybe_unused]] std::string_view nodeName,
        [[maybe_unused]] std::string_view to, [[maybe_unused]] std::string_view hexTransaction,
        RespFunc respFunc) override
    {
        bcos::task::wait([](decltype(this) self, std::string_view hexTransaction,
                             std::string_view to, RespFunc respFunc) -> task::Task<void> {
            // call data is json
            bcostars::Transaction transaction;
            self->decodeData(hexTransaction, transaction.data.input);
            transaction.data.to = to;
            transaction.data.nonce = "0";
            transaction.data.blockLimit = 0;
            transaction.data.chainID = "";
            transaction.data.groupID = "";
            transaction.importTime = 0;

            LIGHTNODE_LOG(INFO) << "RPC call request, to: " << to;
            if (transaction.dataHash.empty())
            {
                bcos::concepts::hash::calculate(
                    transaction, self->m_hasher.clone(), transaction.dataHash);
            }

            bcostars::TransactionReceipt receipt;
            try
            {
                co_await self->scheduler().call(transaction, receipt);
            }
            catch (std::exception& e)
            {
                self->toErrorResp(e, respFunc);
                co_return;
            }

            Json::Value resp;
            toJsonResp(self->m_hasher.clone(), receipt, {}, resp);

            LIGHTNODE_LOG(INFO) << "RPC call transaction finished";
            respFunc(nullptr, resp);
        }(this, hexTransaction, to, std::move(respFunc)));
    }

    void call(std::string_view _groupID, std::string_view _nodeName, std::string_view _to,
        std::string_view _data, [[maybe_unused]] std::string_view _sign,
        RespFunc _respFunc) override
    {
        call(_groupID, _nodeName, _to, _data, _respFunc);
    }

    void sendTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, std::string_view hexTransaction,
        [[maybe_unused]] bool requireProof, RespFunc respFunc) override
    {
        bcos::task::wait([](decltype(this) self, std::string_view hexTransaction,
                             RespFunc respFunc) -> task::Task<void> {
            try
            {
                bcos::bytes binData;
                self->decodeData(hexTransaction, binData);
                bcostars::Transaction transaction;
                bcos::concepts::serialize::decode(binData, transaction);

                if (transaction.dataHash.empty())
                {
                    bcos::concepts::hash::calculate(
                        transaction, self->m_hasher.clone(), transaction.dataHash);
                }
                auto& txHash = transaction.dataHash;
                std::string txHashStr;
                txHashStr.reserve(txHash.size() * 2);
                boost::algorithm::hex_lower(
                    txHash.begin(), txHash.end(), std::back_inserter(txHashStr));

                LIGHTNODE_LOG(INFO) << "RPC send transaction request: "
                                    << "0x" << txHashStr;

                bcostars::TransactionReceipt receipt;
                co_await self->remoteTransactionPool().submitTransaction(
                    std::move(transaction), receipt);

                Json::Value resp;
                toJsonResp(self->m_hasher.clone(), receipt, transaction, txHashStr, resp);

                LIGHTNODE_LOG(INFO) << "RPC send transaction finished";
                respFunc(nullptr, resp);
            }
            catch (std::exception& error)
            {
                self->toErrorResp(error, std::move(respFunc));
            }
        }(this, hexTransaction, std::move(respFunc)));
    }

    void getTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        bcos::task::wait(
            [](decltype(this) self, std::string txHash, RespFunc respFunc) -> task::Task<void> {
                try
                {
                    LIGHTNODE_LOG(INFO) << "RPC get transaction request: " << txHash;

                    std::array<bcos::h256, 1> hashes{bcos::h256{txHash, bcos::h256::FromHex}};
                    std::vector<bcostars::Transaction> transactions;

                    co_await self->remoteLedger().getTransactions(hashes, transactions);

                    Json::Value resp;
                    toJsonResp(self->m_hasher.clone(), transactions[0], resp);

                    respFunc(nullptr, resp);
                }
                catch (std::exception& error)
                {
                    self->toErrorResp(error, std::move(respFunc));
                }
            }(this, std::string(txHash), std::move(_respFunc)));
    }

    void getTransactionReceipt([[maybe_unused]] std::string_view groupID,
        [[maybe_unused]] std::string_view nodeName, [[maybe_unused]] std::string_view txHash,
        [[maybe_unused]] bool requireProof, RespFunc respFunc) override
    {
        bcos::task::wait([](decltype(this) self, auto remoteLedger, std::string txHash,
                             RespFunc respFunc) -> task::Task<void> {
            try
            {
                LIGHTNODE_LOG(INFO) << "RPC get receipt request: " << txHash;

                std::array<bcos::h256, 1> hashes{bcos::h256{txHash, bcos::h256::FromHex}};
                std::vector<bcostars::TransactionReceipt> receipts(1);
                std::vector<bcostars::Transaction> transactions(1);

                co_await remoteLedger.getTransactions(hashes, receipts);
                co_await remoteLedger.getTransactions(hashes, transactions);


                Json::Value resp;
                toJsonResp(self->m_hasher.clone(), receipts[0], transactions[0], txHash, resp);

                respFunc(nullptr, resp);
            }
            catch (std::exception& error)
            {
                self->toErrorResp(error, std::move(respFunc));
            }
        }(this, remoteLedger(), std::string(txHash), std::move(respFunc)));
    }

    void getBlockByHash([[maybe_unused]] std::string_view groupID,
        [[maybe_unused]] std::string_view nodeName, [[maybe_unused]] std::string_view blockHash,
        bool onlyHeader, bool onlyTxHash, RespFunc respFunc) override
    {
        bcos::task::wait([](decltype(this) self, std::string groupID, std::string nodeName,
                             std::string blockHash, bool onlyHeader, bool onlyTxHash,
                             RespFunc respFunc) -> task::Task<void> {
            try
            {
                auto blockNumber = int64_t(-1);
                auto hash = std::array<std::byte, Hasher::HASH_SIZE>();
                hex2Bin(blockHash, hash);
                co_await self->localLedger().getBlockNumberByHash(hash, blockNumber);

                LIGHTNODE_LOG(INFO)
                    << "RPC get block by hash request: 0x" << blockNumber << " " << onlyHeader;
                if (blockNumber < 0)
                {
                    BOOST_THROW_EXCEPTION(std::runtime_error{"Unable to find block hash!"});
                }

                self->getBlockByNumber(
                    groupID, nodeName, blockNumber, onlyHeader, onlyTxHash, std::move(respFunc));
            }
            catch (bcos::Error& error)
            {
                self->toErrorResp(error, respFunc);
            }
        }(this, std::string(groupID), std::string(nodeName), std::string(blockHash), onlyHeader,
                                                    onlyTxHash, std::move(respFunc)));
    }

    void getBlockByNumber([[maybe_unused]] std::string_view groupID,
        [[maybe_unused]] std::string_view nodeName, int64_t blockNumber, bool onlyHeader,
        bool onlyTxHash, RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block by number request: " << blockNumber << " "
                            << onlyHeader;

        bcos::task::wait([](decltype(this) self, bool onlyHeader, int64_t blockNumber,
                             RespFunc respFunc) -> task::Task<void> {
            bcostars::Block block;
            try
            {
                if (onlyHeader)
                {
                    co_await self->localLedger().template getBlock<bcos::concepts::ledger::HEADER>(
                        blockNumber, block);
                }
                else
                {
                    try
                    {
                        co_await self->remoteLedger()
                            .template getBlock<bcos::concepts::ledger::ALL>(blockNumber, block);
                    }
                    catch (const std::exception& e)
                    {
                        BOOST_THROW_EXCEPTION(std::runtime_error{e.what()});
                    }
                    if (RANGES::empty(block.transactionsMetaData) && blockNumber != 0)
                    {
                        LIGHTNODE_LOG(ERROR)
                            << LOG_DESC("getBlockByNumber")
                            << LOG_KV("block has no transaction, blockNumber", blockNumber);
                        auto error = bcos::Error();
                        self->toErrorResp(error, respFunc);
                        co_return;
                    }
                    if (!RANGES::empty(block.transactionsMetaData))
                    {
                        // Check transaction merkle
                        crypto::merkle::Merkle<Hasher> merkle(self->m_hasher.clone());
                        auto hashesRange =
                            block.transactionsMetaData |
                            RANGES::views::transform(
                                [](const bcostars::TransactionMetaData& transactionMetaData)
                                    -> auto& { return transactionMetaData.hash; });
                        std::vector<bcos::bytes> merkles;
                        merkle.generateMerkle(hashesRange, merkles);

                        if (RANGES::empty(merkles))
                        {
                            BOOST_THROW_EXCEPTION(
                                std::runtime_error{"Unable to generate transaction merkle root!"});
                        }
                        LIGHTNODE_LOG(DEBUG)
                            << LOG_KV("blockNumber", blockNumber)
                            << LOG_KV("blockTxsRoot",
                                   toHexStringWithPrefix(block.blockHeader.data.txsRoot))
                            << LOG_KV("transaction number", block.transactions.size());

                        if (!bcos::concepts::bytebuffer::equalTo(
                                block.blockHeader.data.txsRoot, *RANGES::rbegin(merkles)))
                        {
                            auto merkleRoot = *RANGES::rbegin(merkles);
                            std::ostringstream strHex;
                            strHex << "0x" << std::hex << std::setfill('0');
                            for (size_t i = 0; i < Hasher::HASH_SIZE; ++i)
                            {
                                strHex << std::setw(2) << static_cast<unsigned int>(merkleRoot[i]);
                            }
                            LIGHTNODE_LOG(DEBUG)
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_KV("blockTxsRoot",
                                       toHexStringWithPrefix(block.blockHeader.data.txsRoot))
                                << LOG_KV("transaction number", block.transactions.size())
                                << LOG_KV("merkleRoot", strHex.str());
                            auto error = bcos::Error::buildError(
                                "TxsRoot check failed", -1, "CheckMerkleRootFailed");
                            self->toErrorResp(error, respFunc);
                            co_return;
                            BOOST_THROW_EXCEPTION(
                                CheckMerkleRootFailed{} << bcos::error::ErrorMessage{
                                    "Check block transactionMerkle failed!"});
                        }
                    }

                    // Check parentBlock
                    if (blockNumber > 0)
                    {
                        decltype(block) parentBlock;
                        co_await self->localLedger()
                            .template getBlock<bcos::concepts::ledger::HEADER>(
                                blockNumber - 1, parentBlock);

                        std::array<std::byte, Hasher::HASH_SIZE> parentHash;
                        bcos::concepts::hash::calculate(
                            parentBlock, self->m_hasher.clone(), parentHash);

                        if (RANGES::empty(block.blockHeader.data.parentInfo) ||
                            (block.blockHeader.data.parentInfo[0].blockNumber !=
                                parentBlock.blockHeader.data.blockNumber) ||
                            !bcos::concepts::bytebuffer::equalTo(
                                block.blockHeader.data.parentInfo[0].blockHash, parentHash))
                        {
                            std::ostringstream parentHashHex;
                            parentHashHex << "0x" << std::hex << std::setfill('0');
                            for (size_t i = 0; i < Hasher::HASH_SIZE; ++i)
                            {
                                parentHashHex << std::setw(2)
                                              << static_cast<unsigned int>(parentHash[i]);
                            }
                            LIGHTNODE_LOG(DEBUG)
                                << LOG_KV("blockParentNumber",
                                       block.blockHeader.data.parentInfo[0].blockNumber)
                                << LOG_KV("parentBlockNumber",
                                       parentBlock.blockHeader.data.blockNumber)
                                << LOG_KV("calculate parentHash", parentHashHex.str())
                                << LOG_KV("parentHash",
                                       toHexStringWithPrefix(
                                           block.blockHeader.data.parentInfo[0].blockHash));
                            LIGHTNODE_LOG(ERROR) << "No match parentHash!";
                            BOOST_THROW_EXCEPTION(std::runtime_error{"No match parentHash!"});
                        }
                    }
                }
                Json::Value resp;
                toJsonResp(self->m_hasher.clone(), block, resp, onlyHeader);
                respFunc(nullptr, resp);
            }
            catch (bcos::Error& error)
            {
                self->toErrorResp(error, respFunc);
            }
        }(this, onlyHeader, blockNumber, std::move(respFunc)));
    }

    void getBlockHashByNumber([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, int64_t blockNumber,
        RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block hash by number request: " << blockNumber;

        task::wait(
            [](decltype(this) self, int64_t blockNumber, RespFunc respFunc) -> task::Task<void> {
                try
                {
                    auto hash = bcos::h256();
                    co_await self->localLedger().getBlockHashByNumber(blockNumber, hash);
                    Json::Value resp = bcos::toHexStringWithPrefix(hash);
                    respFunc(nullptr, resp);
                }
                catch (bcos::Error& error)
                {
                    self->toErrorResp(error, respFunc);
                }
            }(this, blockNumber, std::move(respFunc)));
    }

    void getBlockNumber(
        std::string_view groupID, std::string_view nodeName, RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block number request";

        task::wait([](decltype(this) self, RespFunc respFunc) -> task::Task<void> {
            try
            {
                auto status = co_await self->localLedger().getStatus();
                LIGHTNODE_LOG(INFO) << "RPC get block number finished: " << status.blockNumber;

                Json::Value resp = status.blockNumber;
                respFunc(nullptr, resp);
            }
            catch (bcos::Error& error)
            {
                self->toErrorResp(error, respFunc);
            }
        }(this, std::move(respFunc)));
    }

    void getCode(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }

    void getABI([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, std::string_view _contractAddress,
        RespFunc _respFunc) override
    {
        bcos::task::wait([this](auto remoteLedger, std::string _contractAddress,
                             RespFunc _respFunc) -> task::Task<void> {
            try
            {
                LIGHTNODE_LOG(TRACE) << "RPC get contract " << _contractAddress << " ABI request";
                auto abiStr = co_await remoteLedger.getABI(_contractAddress);
                LIGHTNODE_LOG(TRACE) << " lightNode RPC get ABI is: " << abiStr;
                Json::Value resp = abiStr;
                _respFunc(nullptr, resp);
            }
            catch (std::exception& error)
            {
                toErrorResp(error, std::move(_respFunc));
            }
        }(remoteLedger(), std::string(_contractAddress), std::move(_respFunc)));
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

    void getNodeListByType(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _type, RespFunc _respFunc) override
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
    void newBlockFilter(std::string_view, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void newPendingTransactionFilter(std::string_view, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void newFilter(std::string_view, const Json::Value&, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void uninstallFilter(std::string_view, std::string_view, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getFilterChanges(std::string_view, std::string_view, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getFilterLogs(std::string_view, std::string_view, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getLogs(std::string_view, const Json::Value&, RespFunc _respFunc) override
    {
        Json::Value value;
        _respFunc(BCOS_ERROR_PTR(-1, "Unspported method!"), value);
    }
    void getGroupBlockNumber(RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get group block number request";

        task::wait([](decltype(this) self, RespFunc respFunc) -> task::Task<void> {
            try
            {
                auto status = co_await self->localLedger().getStatus();
                Json::Value resp = status.blockNumber;

                LIGHTNODE_LOG(INFO) << "RPC get block number finished: " << status.blockNumber;
                respFunc(nullptr, resp);
            }
            catch (bcos::Error& error)
            {
                self->toErrorResp(error, respFunc);
            }
        }(this, std::move(respFunc)));
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
    Hasher m_hasher;
};
}  // namespace bcos::rpc
