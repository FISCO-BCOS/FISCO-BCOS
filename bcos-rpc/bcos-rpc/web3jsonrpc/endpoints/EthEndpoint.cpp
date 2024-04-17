/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EthEndpoint.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "EthEndpoint.h"

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthMethods.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/CallRequest.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>

using namespace bcos;
using namespace bcos::rpc;

task::Task<void> EthEndpoint::protocolVersion(const Json::Value&, Json::Value&)
{
    // TODO: impl this
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));
    co_return;
}
task::Task<void> EthEndpoint::syncing(const Json::Value&, Json::Value& response)
{
    // TODO: impl this
    Json::Value result = false;
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::coinbase(const Json::Value&, Json::Value&)
{
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));
    co_return;
}
task::Task<void> EthEndpoint::chainId(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x4ee8";  // 20200
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::mining(const Json::Value&, Json::Value& response)
{
    Json::Value result = false;
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::hashrate(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::gasPrice(const Json::Value&, Json::Value& response)
{
    // result: gasPrice(QTY)
    auto ledger = m_nodeService->ledger();
    // TODO: to check gas price is hex value or not
    auto config = co_await ledger::getSystemConfig(*ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE);
    Json::Value result;
    if (config.has_value())
    {
        auto [gasPrice, _] = config.value();
        auto const value = std::stoull(gasPrice);
        result = toQuantity(value);
    }
    else
    {
        result = "0x5208";  // 21000
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::accounts(const Json::Value&, Json::Value& response)
{
    Json::Value result = Json::arrayValue;
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::blockNumber(const Json::Value&, Json::Value& response)
{
    auto ledger = m_nodeService->ledger();
    auto number = co_await ledger::getCurrentBlockNumber(*ledger);
    Json::Value result = toQuantity(number);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBalance(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: balance(QTY)
    // TODO: get balance from ledger
    Json::Value result = "0xfffffffffffffff";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getStorageAt(const Json::Value&, Json::Value&)
{
    // params: address(DATA), position(QTY), blockNumber(QTY|TAG)
    // result: value(DATA)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));
    co_return;
}
task::Task<void> EthEndpoint::getTransactionCount(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: nonce(QTY)
    // TODO: impliment getTransactionCount
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> dis(0, 255);
    std::array<bcos::byte, 16> randomFixedBytes;
    for (auto& element : randomFixedBytes)
    {
        element = dis(generator);
    }

    uint16_t* firstNum = (uint16_t*)randomFixedBytes.data();
    // uint64_t* lastNum = (uint64_t*)(randomFixedBytes.data() + sizeof(uint64_t));
    Json::Value result = toQuantity(*firstNum);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockTxCountByHash(
    const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA)
    // result: transactionCount(QTY)
    auto const hashStr = toView(request[0U]);
    auto hash = crypto::HashType(hashStr, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    auto number = co_await ledger::getBlockNumber(*ledger, std::move(hash));
    auto block = co_await ledger::getBlockData(*ledger, number, bcos::ledger::TRANSACTIONS_HASH);
    Json::Value result = toQuantity(block->transactionsHashSize());
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockTxCountByNumber(
    const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG)
    // result: transactionCount(QTY)
    auto const number = fromQuantity(std::string(toView(request[0U])));
    auto const ledger = m_nodeService->ledger();
    auto block = co_await ledger::getBlockData(*ledger, number, bcos::ledger::TRANSACTIONS_HASH);
    Json::Value result = toQuantity(block->transactionsHashSize());
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockHash(
    const Json::Value& request, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockNumber(
    const Json::Value& request, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getCode(const Json::Value&, Json::Value&)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: code(DATA)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));
    co_return;
}
task::Task<void> EthEndpoint::sign(const Json::Value&, Json::Value& response)
{
    // params: address(DATA), message(DATA)
    // result: signature(DATA)
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::signTransaction(const Json::Value&, Json::Value& response)
{
    // params: transaction(TX), address(DATA)
    // result: signedTransaction(DATA)
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::sendTransaction(const Json::Value&, Json::Value& response)
{
    // params: transaction(TX)
    // result: transactionHash(DATA)
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::sendRawTransaction(const Json::Value& request, Json::Value& response)
{
    // params: signedTransaction(DATA)
    // result: transactionHash(DATA)
    auto txpool = m_nodeService->txpool();
    if (!txpool) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::InternalError, "TXPool not available!"));
    }
    auto rawTx = toView(request[0U]);
    auto rawTxBytes = fromHexWithPrefix(rawTx);
    auto bytesRef = bcos::ref(rawTxBytes);
    Web3Transaction web3Tx;
    if (auto const error = codec::rlp::decode(bytesRef, web3Tx); error != nullptr) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, error->errorMessage()));
    }
    auto tarsTx = web3Tx.toTarsTransaction();

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_tx = std::move(tarsTx)]() mutable { return &m_tx; });
    // for web3.eth.sendRawTransaction, return the hash of raw transaction
    auto web3TxHash = bcos::crypto::keccak256Hash(bcos::ref(rawTxBytes));
    tx->mutableInner().dataHash.assign(web3TxHash.begin(), web3TxHash.end());

    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("sendRawTransaction") << web3Tx.toString();
    }
    txpool->broadcastTransaction(*tx);
    auto const txResult = co_await txpool->submitTransactionWithoutReceipt(std::move(tx));
    crypto::HashType hash{};
    if (txResult->status() == 0)
    {
        hash = std::move(web3TxHash);
    }
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("sendRawTransaction finished")
                        << LOG_KV("status", txResult->status())
                        << LOG_KV("hash", hash.hexPrefixed());
    }
    Json::Value result = hash.hexPrefixed();
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::call(const Json::Value& request, Json::Value& response)
{
    // params: transaction(TX), blockNumber(QTY|TAG)
    // result: data(DATA)
    auto scheduler = m_nodeService->scheduler();
    if (!scheduler)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::InternalError, "Scheduler not available!"));
    }
    auto [valid, call] = decodeCallRequest(request[0u]);
    if (!valid)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid call request!"));
    }
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("eth_call") << call;
    }
    auto&& tx = call.takeToTransaction(m_nodeService->blockFactory()->transactionFactory());
    // TODO: ignore params blockNumber here, use it after historical data is available

    // MOVE it into a new file
    struct Awaitable
    {
        bcos::scheduler::SchedulerInterface& m_scheduler;
        bcos::protocol::Transaction::Ptr m_tx;
        std::variant<Error::Ptr, protocol::TransactionReceipt::Ptr> m_result{};
        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle) noexcept
        {
            m_scheduler.call(m_tx, [this, handle](Error::Ptr&& error, auto&& result) {
                if (error)
                {
                    m_result.emplace<Error::Ptr>(std::move(error));
                }
                else
                {
                    m_result.emplace<protocol::TransactionReceipt::Ptr>(std::move(result));
                }
                handle.resume();
            });
        }
        protocol::TransactionReceipt::Ptr await_resume() noexcept
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<protocol::TransactionReceipt::Ptr>(m_result));
        }
    };
    auto const result = co_await Awaitable{.m_scheduler = *scheduler, .m_tx = std::move(tx)};

    auto output = toHexStringWithPrefix(result->output());
    if (result->status() == static_cast<int32_t>(protocol::TransactionStatus::None))
    {
        response["jsonrpc"] = "2.0";
        response["result"] = std::move(output);
    }
    else
    {
        // https://docs.infura.io/api/networks/ethereum/json-rpc-methods/eth_call#returns
        Json::Value jsonResult = Json::objectValue;
        jsonResult["code"] = result->status();
        jsonResult["message"] = result->message();
        jsonResult["data"] = std::move(output);
        response["jsonrpc"] = "2.0";
        response["error"] = std::move(jsonResult);
    }
    co_return;
}
task::Task<void> EthEndpoint::estimateGas(const Json::Value& request, Json::Value& response)
{
    // params: transaction(TX), blockNumber(QTY|TAG)
    // result: gas(QTY)
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockByHash(const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA), fullTransaction(Boolean)
    // result: block(BLOCK)
    auto blockHash = toView(request[0u]);
    auto fullTransaction = request[1u].asBool();
    auto const ledger = m_nodeService->ledger();
    auto number = co_await ledger::getBlockNumber(
        *ledger, crypto::HashType(blockHash, crypto::HashType::FromHex));
    auto block = co_await ledger::getBlockData(*ledger, number, bcos::ledger::FULL_BLOCK);
    Json::Value result = Json::objectValue;
    combineBlockResponse(result, std::move(block), fullTransaction);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockByNumber(const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG), fullTransaction(Boolean)
    // result: block(BLOCK)
    auto blockTag = toView(request[0u]);
    auto fullTransaction = request[1u].asBool();
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    auto const ledger = m_nodeService->ledger();
    auto block = co_await ledger::getBlockData(*ledger, blockNumber, bcos::ledger::FULL_BLOCK);
    Json::Value result = Json::objectValue;
    combineBlockResponse(result, std::move(block), fullTransaction);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByHash(
    const Json::Value& request, Json::Value& response)
{
    // params: transactionHash(DATA)
    // result: transaction(TX)
    auto txHash = toView(request[0u]);
    auto hash = crypto::HashType(txHash, crypto::HashType::FromHex);
    auto hashList = std::make_shared<crypto::HashList>();
    hashList->push_back(hash);
    auto const ledger = m_nodeService->ledger();
    auto txs = co_await ledger::getTransactions(*ledger, std::move(hashList));
    auto receipt = co_await ledger::getReceipt(*ledger, hash);
    if (!receipt || !txs || txs->empty())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "Invalid transaction hash: " + hash.hexPrefixed()));
    }
    auto block = co_await ledger::getBlockData(
        *ledger, receipt->blockNumber(), bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS_HASH);
    Json::Value result = Json::objectValue;
    combineTxResponse(result, txs->at(0), std::move(receipt), std::move(block));
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByBlockHashAndIndex(
    const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA), transactionIndex(QTY)
    // result: transaction(TX)
    // auto blockHash = toView(request[0u]);
    // auto transactionIndex = fromQuantity(std::string(toView(request[1u])));
    // auto hash = crypto::HashType(blockHash, crypto::HashType::FromHex);
    // auto const ledger = m_nodeService->ledger();
    // auto number = co_await ledger::getBlockNumber(*ledger, hash);
    // if (number <= 0) [[unlikely]]
    // {
    //     BOOST_THROW_EXCEPTION(
    //         JsonRpcException(InvalidParams, "Invalid block hash: " + hash.hexPrefixed()));
    // }
    // auto block = co_await ledger::getBlockData(
    //     *ledger, number, bcos::ledger::TRANSACTIONS | bcos::ledger::HEADER);
    // if (!block || transactionIndex >= block->transactionsSize()) [[unlikely]]
    // {
    //     BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction index!"));
    // }
    // auto tx = block->transaction(transactionIndex);
    // Json::Value result = Json::objectValue;
    // combineTxResponse(result, std::move(tx), nullptr, std::move(block));
    // buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByBlockNumberAndIndex(
    const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG), transactionIndex(QTY)
    // result: transaction(TX)
    // auto blockTag = toView(request[0u]);
    // auto transactionIndex = fromQuantity(std::string(toView(request[1u])));
    // auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    // auto const ledger = m_nodeService->ledger();
    // auto block = co_await ledger::getBlockData(
    //     *ledger, blockNumber, bcos::ledger::TRANSACTIONS | bcos::ledger::HEADER);
    // if (!block || transactionIndex >= block->transactionsSize()) [[unlikely]]
    // {
    //     BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction index!"));
    // }
    // auto tx = block->transaction(transactionIndex);
    // Json::Value result = Json::objectValue;
    // combineTxResponse(result, std::move(tx), nullptr, std::move(block));
    // buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionReceipt(
    const Json::Value& request, Json::Value& response)
{
    // params: transactionHash(DATA)
    // result: transactionReceipt(RECEIPT)
    auto const hashStr = toView(request[0U]);
    auto hash = crypto::HashType(hashStr, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    auto receipt = co_await ledger::getReceipt(*ledger, hash);
    auto hashList = std::make_shared<crypto::HashList>();
    hashList->push_back(hash);
    auto txs = co_await ledger::getTransactions(*ledger, std::move(hashList));
    if (!receipt || !txs || txs->empty())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "Invalid transaction hash: " + hash.hexPrefixed()));
    }
    auto block = co_await ledger::getBlockData(
        *ledger, receipt->blockNumber(), bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS_HASH);
    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, std::move(receipt), txs->at(0), std::move(block));
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleByBlockHashAndIndex(const Json::Value&, Json::Value& response)
{
    Json::Value result = "null";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleByBlockNumberAndIndex(
    const Json::Value&, Json::Value& response)
{
    Json::Value result = "null";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::newFilter(const Json::Value&, Json::Value&)
{
    // params: filter(FILTER)
    // result: filterId(QTY)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}
task::Task<void> EthEndpoint::newBlockFilter(const Json::Value&, Json::Value&)
{
    // result: filterId(QTY)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}
task::Task<void> EthEndpoint::newPendingTransactionFilter(const Json::Value&, Json::Value&)
{
    // result: filterId(QTY)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}
task::Task<void> EthEndpoint::uninstallFilter(const Json::Value&, Json::Value&)
{
    // params: filterId(QTY)
    // result: success(Boolean)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}
task::Task<void> EthEndpoint::getFilterChanges(const Json::Value&, Json::Value&)
{
    // params: filterId(QTY)
    // result: logs(ARRAY)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}
task::Task<void> EthEndpoint::getFilterLogs(const Json::Value&, Json::Value&)
{
    // params: filterId(QTY)
    // result: logs(ARRAY)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}
task::Task<void> EthEndpoint::getLogs(const Json::Value&, Json::Value&)
{
    // params: filter(FILTER)
    // result: logs(ARRAY)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));

    co_return;
}

// return (actual block number, isLatest block)
task::Task<std::tuple<protocol::BlockNumber, bool>> EthEndpoint::getBlockNumberByTag(
    std::string_view blockTag)
{
    if (blockTag == EarliestBlock)
    {
        co_return std::make_tuple(0, false);
    }
    else if (blockTag == LatestBlock || blockTag == SafeBlock || blockTag == FinalizedBlock ||
             blockTag == PendingBlock)
    {
        auto ledger = m_nodeService->ledger();
        auto number = co_await ledger::getCurrentBlockNumber(*ledger);
        co_return std::make_tuple(number, true);
    }
    else
    {
        static const std::regex hexRegex("^0x[0-9a-fA-F]+$");
        if (std::regex_match(blockTag.data(), hexRegex))
        {
            auto ledger = m_nodeService->ledger();
            auto number = co_await ledger::getCurrentBlockNumber(*ledger);
            auto blockNumber = fromQuantity(std::string(blockTag));
            co_return std::make_tuple(blockNumber, std::cmp_equal(number, blockNumber));
        }
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "Invalid Block Number: " + std::string(blockTag)));
    }
}
