#pragma once

#include <bcos-tars-protocol/impl/TarsHashable.h>

#include "../Log.h"
#include "Converter.h"
#include "bcos-concepts/Basic.h"
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Exception.h"
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
#include <bcos-task/Wait.h>
#include <json/value.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>

namespace bcos::rpc
{
// clang-format off
struct NotFoundTransactionHash: public bcos::error::Exception {};
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
        std::string groupID)
      : m_localLedger(std::move(localLedger)),
        m_remoteLedger(std::move(remoteLedger)),
        m_remoteTransactionPool(std::move(remoteTransactionPool)),
        m_scheduler(std::move(scheduler)),
        m_chainID(std::move(chainID)),
        m_groupID(std::move(groupID))
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
            toErrorResp(std::make_shared<bcos::Error>(std::move(bcosError)), std::move(respFunc));
        }
        catch ([[maybe_unused]] std::bad_cast&)
        {
            // no bcos error
            auto bcosError = bcos::Error(-1, boost::diagnostic_information(error));
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
        // call data is json
        auto transaction = std::make_unique<bcostars::Transaction>();
        decodeData(hexTransaction, transaction->data.input);
        transaction->data.to = to;
        transaction->data.nonce = "0";
        transaction->data.blockLimit = 0;
        transaction->data.chainID = "";
        transaction->data.groupID = "";
        transaction->importTime = 0;

        LIGHTNODE_LOG(INFO) << "RPC call request, to: " << to;

        auto receipt = std::make_unique<bcostars::TransactionReceipt>();

        auto& transactionRef = *transaction;
        auto& receiptRef = *receipt;
        bcos::task::wait(scheduler().call(transactionRef, receiptRef),
            [this, m_transaction = std::move(transaction), m_receipt = std::move(receipt),
                m_respFunc = std::move(respFunc)](std::exception_ptr error = {}) mutable {
                if (error)
                {
                    toErrorResp(error, m_respFunc);
                    return;
                }

                Json::Value resp;
                toJsonResp<Hasher>(*m_receipt, {}, resp);

                LIGHTNODE_LOG(INFO) << "RPC call transaction finished";
                m_respFunc(nullptr, resp);
            });
    }

    void sendTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, std::string_view hexTransaction,
        [[maybe_unused]] bool requireProof, RespFunc respFunc) override
    {
        bcos::task::wait([this](std::string_view hexTransaction,
                             RespFunc respFunc) -> task::Task<void> {
            try
            {
                bcos::bytes binData;
                decodeData(hexTransaction, binData);
                bcostars::Transaction transaction;
                bcos::concepts::serialize::decode(binData, transaction);

                bcos::bytes txHash;
                bcos::concepts::hash::calculate<Hasher>(transaction, txHash);
                std::string txHashStr;
                txHashStr.reserve(txHash.size() * 2);
                boost::algorithm::hex_lower(
                    txHash.begin(), txHash.end(), std::back_inserter(txHashStr));

                LIGHTNODE_LOG(INFO) << "RPC send transaction request: "
                                    << "0x" << txHashStr;

                bcostars::TransactionReceipt receipt;
                co_await remoteTransactionPool().submitTransaction(std::move(transaction), receipt);

                Json::Value resp;
                toJsonResp<Hasher>(receipt, transaction, txHashStr, resp);

                LIGHTNODE_LOG(INFO) << "RPC send transaction finished";
                respFunc(nullptr, resp);
            }
            catch (std::exception& error)
            {
                toErrorResp(error, std::move(respFunc));
            }
        }(hexTransaction, std::move(respFunc)));
    }

    void getTransaction([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view txHash,
        [[maybe_unused]] bool _requireProof, RespFunc _respFunc) override
    {
        bcos::task::wait([this](std::string txHash, RespFunc respFunc) -> task::Task<void> {
            try
            {
                LIGHTNODE_LOG(INFO) << "RPC get transaction request: " << txHash;

                std::array<bcos::h256, 1> hashes{bcos::h256{txHash, bcos::h256::FromHex}};
                std::vector<bcostars::Transaction> transactions;

                co_await remoteLedger().getTransactions(hashes, transactions);

                Json::Value resp;
                toJsonResp<Hasher>(transactions[0], resp);

                respFunc(nullptr, resp);
            }
            catch (std::exception& error)
            {
                toErrorResp(error, std::move(respFunc));
            }
        }(std::string(txHash), std::move(_respFunc)));
    }

    void getTransactionReceipt([[maybe_unused]] std::string_view groupID,
        [[maybe_unused]] std::string_view nodeName, [[maybe_unused]] std::string_view txHash,
        [[maybe_unused]] bool requireProof, RespFunc respFunc) override
    {
        bcos::task::wait(
            [this](auto remoteLedger, std::string txHash, RespFunc respFunc) -> task::Task<void> {
                try
                {
                    LIGHTNODE_LOG(INFO) << "RPC get receipt request: " << txHash;

                    std::array<bcos::h256, 1> hashes{bcos::h256{txHash, bcos::h256::FromHex}};
                    std::vector<bcostars::TransactionReceipt> receipts(1);
                    std::vector<bcostars::Transaction> transactions(1);

                    co_await remoteLedger.getTransactions(hashes, receipts);
                    co_await remoteLedger.getTransactions(hashes, transactions);


                    Json::Value resp;
                    toJsonResp<Hasher>(receipts[0], transactions[0], txHash, resp);

                    respFunc(nullptr, resp);
                }
                catch (std::exception& error)
                {
                    toErrorResp(error, std::move(respFunc));
                }
            }(remoteLedger(), std::string(txHash), std::move(respFunc)));
    }

    void getBlockByHash([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, [[maybe_unused]] std::string_view blockHash,
        bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) override
    {
        auto blockNumber = std::make_unique<int64_t>(-1);
        auto hash = std::make_unique<std::array<std::byte, Hasher::HASH_SIZE>>();
        hex2Bin(blockHash, *hash);

        auto& hashRef = *hash;
        auto& blockNumberRef = *blockNumber;
        bcos::task::wait(localLedger().getBlockNumberByHash(hashRef, blockNumberRef),
            [this, m_hash = std::move(hash), m_blockNumber = std::move(blockNumber),
                m_onlyHeader = _onlyHeader, m_respFunc = std::move(_respFunc),
                m_groupID = std::string(_groupID), m_nodeName = std::string(_nodeName),
                m_onlyTxHash = _onlyTxHash](std::exception_ptr error = {}) mutable {
                if (error)
                {
                    toErrorResp(error, m_respFunc);
                    return;
                }

                LIGHTNODE_LOG(INFO)
                    << "RPC get block by hash request: 0x"
                    // << bcos::toHex<decltype(*m_hash), std::string>(*m_hash) << " "
                    << *m_blockNumber << " " << m_onlyHeader;
                if (*m_blockNumber < 0)
                    BOOST_THROW_EXCEPTION(std::runtime_error{"Unable to find block hash!"});

                getBlockByNumber(m_groupID, m_nodeName, *m_blockNumber, m_onlyHeader, m_onlyTxHash,
                    std::move(m_respFunc));
            });
        ;
    }

    void getBlockByNumber([[maybe_unused]] std::string_view groupID,
        [[maybe_unused]] std::string_view nodeName, int64_t blockNumber, bool onlyHeader,
        bool onlyTxHash, RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block by number request: " << blockNumber << " "
                            << onlyHeader;

        auto getBlockCoroutine = [this](bool onlyHeader,
                                     int64_t blockNumber) -> task::Task<bcostars::Block> {
            bcostars::Block block;
            if (onlyHeader)
            {
                co_await localLedger().template getBlock<bcos::concepts::ledger::HEADER>(
                    blockNumber, block);
            }
            else
            {
                co_await remoteLedger().template getBlock<bcos::concepts::ledger::ALL>(
                    blockNumber, block);

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
                    {
                        BOOST_THROW_EXCEPTION(
                            std::runtime_error{"Unable to generate transaction merkle root!"});
                    }

                    if (!bcos::concepts::bytebuffer::equalTo(
                            block.blockHeader.data.txsRoot, *RANGES::rbegin(merkles)))
                    {
                        BOOST_THROW_EXCEPTION(std::runtime_error{"No match transaction root!"});
                    }
                }

                // Check parentBlock
                if (blockNumber > 0)
                {
                    decltype(block) parentBlock;
                    co_await localLedger().template getBlock<bcos::concepts::ledger::HEADER>(
                        blockNumber - 1, parentBlock);

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

            co_return block;
        };

        bcos::task::wait(getBlockCoroutine(onlyHeader, blockNumber),
            [this, m_respFunc = std::move(respFunc), m_onlyHeader = onlyHeader](
                auto&& result) mutable {
                using ResultType = std::remove_cvref_t<decltype(result)>;
                if constexpr (std::is_same_v<ResultType, std::exception_ptr>)
                {
                    this->toErrorResp(result, m_respFunc);
                    return;
                }
                else
                {
                    Json::Value resp;
                    toJsonResp<Hasher>(result, resp, m_onlyHeader);

                    m_respFunc(nullptr, resp);
                }
            });
    }

    void getBlockHashByNumber([[maybe_unused]] std::string_view _groupID,
        [[maybe_unused]] std::string_view _nodeName, int64_t blockNumber,
        RespFunc respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block hash by number request: " << blockNumber;

        auto hash = std::make_unique<bcos::h256>();
        auto& hashRef = *hash;
        bcos::task::wait(localLedger().getBlockHashByNumber(blockNumber, hashRef),
            [this, m_hash = std::move(hash), m_respFunc = std::move(respFunc)](
                std::exception_ptr error = {}) mutable {
                if (error)
                {
                    this->toErrorResp(error, m_respFunc);
                    return;
                }
                Json::Value resp = bcos::toHexStringWithPrefix(*m_hash);

                m_respFunc(nullptr, resp);
            });
    }

    void getBlockNumber(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override
    {
        LIGHTNODE_LOG(INFO) << "RPC get block number request";

        bcos::task::wait(localLedger().getStatus(),
            [this, m_respFunc = std::move(_respFunc)](auto&& result) mutable {
                using ResultType = std::remove_cvref_t<decltype(result)>;
                if constexpr (std::is_same_v<ResultType, std::exception_ptr>)
                {
                    this->toErrorResp(result, m_respFunc);
                    return;
                }
                else
                {
                    Json::Value resp = result.blockNumber;

                    LIGHTNODE_LOG(INFO) << "RPC get block number finished: " << result.blockNumber;
                    m_respFunc(nullptr, resp);
                }
            });
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

        bcos::task::wait(localLedger().getStatus(),
            [this, m_respFunc = std::move(_respFunc)](auto&& result) mutable {
                using ResultType = std::remove_cvref_t<decltype(result)>;
                if constexpr (std::is_same_v<ResultType, std::exception_ptr>)
                {
                    this->toErrorResp(result, m_respFunc);
                    return;
                }
                else
                {
                    Json::Value resp = result.blockNumber;

                    LIGHTNODE_LOG(INFO) << "RPC get block number finished: " << result.blockNumber;
                    m_respFunc(nullptr, resp);
                }
            });
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