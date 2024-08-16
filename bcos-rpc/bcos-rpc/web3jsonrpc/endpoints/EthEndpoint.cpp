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
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-executor/src/Common.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/util.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthMethods.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/CallRequest.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <variant>

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
    auto const sync = m_nodeService->sync();
    auto status = sync->getSyncStatus();
    Json::Value result;
    if (!status.has_value())
    {
        result = false;
    }
    else
    {
        result = Json::objectValue;
        auto [currentBlock, highestBlock] = status.value();
        result["startingBlock"] = "0x0";
        result["currentBlock"] = toQuantity(currentBlock);
        result["highestBlock"] = toQuantity(highestBlock);
    }
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
    auto const ledger = m_nodeService->ledger();
    auto config = co_await ledger::getSystemConfig(*ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID);
    Json::Value result;
    if (config.has_value())
    {
        try
        {
            auto [chainId, _] = config.value();
            result = toQuantity(std::stoull(chainId));
        }
        catch (...)
        {
            result = "0x0";  // 0x0 for default
        }
    }
    else
    {
        result = "0x0";  // 0 for default
    }
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
    auto const ledger = m_nodeService->ledger();
    auto config = co_await ledger::getSystemConfig(*ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE);
    Json::Value result;
    if (config.has_value())
    {
        auto [gasPrice, _] = config.value();
        auto const value = std::stoull(gasPrice, nullptr, 16);
        result = toQuantity(value < LowestGasPrice ? LowestGasPrice : value);
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
    auto address = toView(request[0u]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    // TODO)): blockNumber is ignored nowadays
    auto const blockTag = toView(request[1u]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getBalance" << LOG_KV("address", address)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }
    auto const ledger = m_nodeService->ledger();
    u256 balance = 0;
    if (auto const entry = co_await ledger::getStorageAt(
            *ledger, addressStr, bcos::executor::ACCOUNT_BALANCE, /*blockNumber*/ 0);
        entry.has_value())
    {
        auto const balanceStr = std::string(entry.value().get());
        balance = u256(balanceStr);
    }
    else
    {
        WEB3_LOG(TRACE) << LOG_DESC("getBalance failed, return 0 by defualt")
                        << LOG_KV("address", address);
    }
    Json::Value result = toQuantity(std::move(balance));
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getStorageAt(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), position(QTY), blockNumber(QTY|TAG)
    // result: value(DATA)
    auto address = toView(request[0u]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    auto position = toView(request[1u]);
    std::string positionStr =
        std::string(position.starts_with("0x") ? position.substr(2) : position);
    if (position.size() % 2 != 0)
    {
        positionStr.insert(0, "0");
    }
    const auto posistionBytes = FixedBytes<32>(positionStr, FixedBytes<32>::FromHex);
    // TODO)): blockNumber is ignored nowadays
    auto const blockTag = toView(request[2u]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getStorageAt" << LOG_KV("address", address)
                        << LOG_KV("pos", positionStr) << LOG_KV("blockTag", blockTag)
                        << LOG_KV("blockNumber", blockNumber);
    }
    auto const ledger = m_nodeService->ledger();
    Json::Value result;
    if (auto const entry = co_await ledger::getStorageAt(
            *ledger, addressStr, posistionBytes.toRawString(), /*blockNumber*/ 0);
        entry.has_value())
    {
        auto const value = entry.value().get();
        result = toHex(value, "0x");
    }
    else
    {
        // empty value
        result = "0x0000000000000000000000000000000000000000000000000000000000000000";
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionCount(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: nonce(QTY)
    auto address = toView(request[0u]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    // TODO)): blockNumber is ignored nowadays
    auto const blockTag = toView(request[1u]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getTransactionCount" << LOG_KV("address", address)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }
    auto const ledger = m_nodeService->ledger();
    uint64_t nonce = 0;
    if (auto const entry = co_await ledger::getStorageAt(
            *ledger, addressStr, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE, /*blockNumber*/ 0);
        entry.has_value())
    {
        nonce = std::stoull(std::string(entry.value().get()));
    }
    else
    {
        WEB3_LOG(TRACE) << LOG_DESC("get address nonce failed, return random value by defualt")
                        << LOG_KV("address", address);
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> dis(0, 255);
        std::array<bcos::byte, 8> randomFixedBytes;
        for (auto& element : randomFixedBytes)
        {
            element = dis(generator);
        }
        const auto* firstNum = (uint32_t*)randomFixedBytes.data();
        nonce = *firstNum;
    }
    Json::Value result = toQuantity(nonce);
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
    Json::Value result;
    try
    {
        auto number = co_await ledger::getBlockNumber(*ledger, std::move(hash));
        auto block =
            co_await ledger::getBlockData(*ledger, number, bcos::ledger::TRANSACTIONS_HASH);
        result = toQuantity(block->transactionsHashSize());
    }
    catch (...)
    {
        result = "0x0";
    }
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
    Json::Value result;
    try
    {
        auto const block =
            co_await ledger::getBlockData(*ledger, number, bcos::ledger::TRANSACTIONS_HASH);
        result = toQuantity(block->transactionsHashSize());
    }
    catch (...)
    {
        result = "0x0";
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockHash(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockNumber(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getCode(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: code(DATA)
    auto address = toView(request[0u]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    // TODO)): blockNumber is ignored nowadays
    auto const blockTag = toView(request[1u]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getCode" << LOG_KV("address", address)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }
    auto const scheduler = m_nodeService->scheduler();
    struct Awaitable
    {
        bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
        std::string& m_address;
        std::variant<Error::Ptr, bcos::bytes> m_result{};
        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            m_scheduler->getCode(m_address, [this, handle](auto&& error, auto&& code) {
                if (error)
                {
                    m_result.emplace<Error::Ptr>(std::forward<decltype(error)>(error));
                }
                else
                {
                    m_result.emplace<bcos::bytes>(std::forward<decltype(code)>(code));
                }
                handle.resume();
            });
        }
        bcos::bytes await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<bcos::bytes>(m_result);
        }
    };
    auto const code = co_await Awaitable{
        .m_scheduler = scheduler,
        .m_address = addressStr,
    };
    Json::Value result = toHexStringWithPrefix(code);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::sign(const Json::Value&, Json::Value& response)
{
    // params: address(DATA), message(DATA)
    // result: signature(DATA)
    Json::Value result = "0x00";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::signTransaction(const Json::Value&, Json::Value& response)
{
    // params: transaction(TX), address(DATA)
    // result: signedTransaction(DATA)
    Json::Value result = "0x00";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::sendTransaction(const Json::Value&, Json::Value& response)
{
    // params: transaction(TX)
    // result: transactionHash(DATA)
    Json::Value result = "0x0000000000000000000000000000000000000000000000000000000000000000";
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
    auto tarsTx = web3Tx.takeToTarsTransaction();

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_tx = std::move(tarsTx)]() mutable { return &m_tx; });
    // for web3.eth.sendRawTransaction, return the hash of raw transaction
    auto web3TxHash = bcos::crypto::keccak256Hash(bcos::ref(rawTxBytes));
    tx->mutableInner().extraTransactionHash.assign(web3TxHash.begin(), web3TxHash.end());

    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("sendRawTransaction") << web3Tx.toString();
    }
    txpool->broadcastTransaction(*tx);
    auto const txResult = co_await txpool->submitTransaction(std::move(tx));
    auto const hash = std::move(web3TxHash);
    if (txResult->status() == 0)
    {
        Json::Value result = hash.hexPrefixed();
        buildJsonContent(result, response);
    }
    else
    {
        protocol::TransactionStatus status =
            static_cast<protocol::TransactionStatus>(txResult->status());
        Json::Value errorData = Json::objectValue;
        errorData["txHash"] = hash.hexPrefixed();
        auto output = toHex(txResult->transactionReceipt()->output(), "0x");
        auto msg = fmt::format("VM Exception while processing transaction, reason: {}, msg: {}",
            protocol::toString(status), output);
        errorData["message"] = msg;
        errorData["data"] = std::move(output);
        buildJsonErrorWithData(errorData, InternalError, std::move(msg), response);
    }
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        WEB3_LOG(TRACE) << LOG_DESC("sendRawTransaction finished")
                        << LOG_KV("status", txResult->status())
                        << LOG_KV("hash", hash.hexPrefixed()) << LOG_KV("rsp", printJson(response));
    }
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
    auto [valid, call] = decodeCallRequest(request[0U]);
    if (!valid)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid call request!"));
    }
    auto const blockTag = toView(request[1U]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("eth_call") << LOG_KV("call", call)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }
    auto tx = call.takeToTransaction(m_nodeService->blockFactory()->transactionFactory());
    struct Awaitable
    {
        bcos::scheduler::SchedulerInterface& m_scheduler;
        bcos::protocol::Transaction::Ptr& m_tx;
        Error::Ptr m_error;
        Json::Value& m_response;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_scheduler.call(m_tx, [this, handle](Error::Ptr&& error, auto&& result) {
                if (error)
                {
                    m_error = std::move(error);
                }
                else
                {
                    auto output = toHexStringWithPrefix(result->output());
                    if (result->status() == static_cast<int32_t>(protocol::TransactionStatus::None))
                    {
                        m_response["jsonrpc"] = "2.0";
                        m_response["result"] = output;
                    }
                    else
                    {
                        // https://docs.infura.io/api/networks/ethereum/json-rpc-methods/eth_call#returns
                        Json::Value jsonResult = Json::objectValue;
                        jsonResult["code"] = result->status();
                        jsonResult["message"] = result->message();
                        jsonResult["data"] = output;
                        m_response["jsonrpc"] = "2.0";
                        m_response["error"] = std::move(jsonResult);
                    }
                }

                handle.resume();
            });
        }
        void await_resume()
        {
            if (m_error)
            {
                BOOST_THROW_EXCEPTION(*m_error);
            }
        }
    };
    Awaitable awaitable{
        .m_scheduler = *scheduler, .m_tx = tx, .m_error = {}, .m_response = response};
    co_await awaitable;
}
task::Task<void> EthEndpoint::estimateGas(const Json::Value& request, Json::Value& response)
{
    // params: transaction(TX), blockNumber(QTY|TAG)
    // result: gas(QTY)
    auto const tx = request[0u];
    auto const blockTag = toView(request[1u]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("eth_estimateGas") << LOG_KV("tx", printJson(tx))
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }
    // FIXME)): fake now
    Json::Value result = "0x1";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockByHash(const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA), fullTransaction(Boolean)
    // result: block(BLOCK)
    auto const blockHash = toView(request[0u]);
    auto const fullTransaction = request[1u].asBool();
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto const number = co_await ledger::getBlockNumber(
            *ledger, crypto::HashType(blockHash, crypto::HashType::FromHex));
        auto flag = bcos::ledger::HEADER;
        flag |= fullTransaction ? bcos::ledger::TRANSACTIONS : bcos::ledger::TRANSACTIONS_HASH;
        auto block = co_await ledger::getBlockData(*ledger, number, flag);
        combineBlockResponse(result, std::move(block), fullTransaction);
    }
    catch (...)
    {
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockByNumber(const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG), fullTransaction(Boolean)
    // result: block(BLOCK)
    auto const blockTag = toView(request[0u]);
    auto const fullTransaction = request[1u].asBool();
    Json::Value result = Json::objectValue;
    try
    {
        auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
        auto const ledger = m_nodeService->ledger();
        auto flag = bcos::ledger::HEADER;
        flag |= fullTransaction ? bcos::ledger::TRANSACTIONS : bcos::ledger::TRANSACTIONS_HASH;
        auto block = co_await ledger::getBlockData(*ledger, blockNumber, flag);
        combineBlockResponse(result, std::move(block), fullTransaction);
    }
    catch (...)
    {
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByHash(
    const Json::Value& request, Json::Value& response)
{
    // params: transactionHash(DATA)
    // result: transaction(TX)
    auto const txHash = toView(request[0u]);
    auto const hash = crypto::HashType(txHash, crypto::HashType::FromHex);
    auto hashList = std::make_shared<crypto::HashList>();
    hashList->push_back(hash);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto const txs = co_await ledger::getTransactions(*ledger, std::move(hashList));
        auto receipt = co_await ledger::getReceipt(*ledger, hash);
        if (!receipt || !txs || txs->empty())
        {
            result = Json::nullValue;
            buildJsonContent(result, response);
            co_return;
        }
        auto block = co_await ledger::getBlockData(*ledger, receipt->blockNumber(),
            bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS_HASH);
        combineTxResponse(result, txs->at(0), std::move(receipt), std::move(block));
    }
    catch (...)
    {
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByBlockHashAndIndex(
    const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA), transactionIndex(QTY)
    // result: transaction(TX)
    auto const blockHash = toView(request[0u]);
    auto const transactionIndex = fromQuantity(std::string(toView(request[1u])));
    auto const hash = crypto::HashType(blockHash, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    auto const number = co_await ledger::getBlockNumber(*ledger, hash);
    // will not throw exception in getBlockNumber if not found
    if (number <= 0) [[unlikely]]
    {
        result = Json::nullValue;
        buildJsonContent(result, response);
        co_return;
    }
    auto block = co_await ledger::getBlockData(
        *ledger, number, bcos::ledger::TRANSACTIONS | bcos::ledger::HEADER);
    if (!block || transactionIndex >= block->transactionsSize()) [[unlikely]]
    {
        result = Json::nullValue;
        buildJsonContent(result, response);
        co_return;
    }
    auto tx = block->transaction(transactionIndex);
    combineTxResponse(result, std::move(tx), nullptr, std::move(block));
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByBlockNumberAndIndex(
    const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG), transactionIndex(QTY)
    // result: transaction(TX)
    auto const blockTag = toView(request[0u]);
    auto const transactionIndex = fromQuantity(std::string(toView(request[1u])));
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto block = co_await ledger::getBlockData(
            *ledger, blockNumber, bcos::ledger::TRANSACTIONS | bcos::ledger::HEADER);
        if (!block || transactionIndex >= block->transactionsSize()) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction index!"));
        }
        auto tx = block->transaction(transactionIndex);
        combineTxResponse(result, std::move(tx), nullptr, std::move(block));
    }
    catch (...)
    {
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionReceipt(
    const Json::Value& request, Json::Value& response)
{
    // params: transactionHash(DATA)
    // result: transactionReceipt(RECEIPT)
    auto const hashStr = toView(request[0U]);
    auto const hash = crypto::HashType(hashStr, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto receipt = co_await ledger::getReceipt(*ledger, hash);
        auto hashList = std::make_shared<crypto::HashList>();
        hashList->push_back(hash);
        auto txs = co_await ledger::getTransactions(*ledger, std::move(hashList));
        if (!receipt || !txs || txs->empty())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Invalid transaction hash: " + hash.hexPrefixed()));
        }
        auto block = co_await ledger::getBlockData(*ledger, receipt->blockNumber(),
            bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS_HASH);
        combineReceiptResponse(result, std::move(receipt), txs->at(0), std::move(block));
    }
    catch (...)
    {
        result = Json::nullValue;
        buildJsonContent(result, response);
        co_return;
    }
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
task::Task<void> EthEndpoint::newFilter(const Json::Value& request, Json::Value& response)
{
    // params: filter(FILTER)
    // result: filterId(QTY)
    Json::Value jParams = request[0U];
    auto params = m_filterSystem->requestFactory()->create();
    params->fromJson(jParams);
    Json::Value result = co_await m_filterSystem->newFilter(params);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::newBlockFilter(const Json::Value&, Json::Value& response)
{
    // result: filterId(QTY)
    Json::Value result = co_await m_filterSystem->newBlockFilter();
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::newPendingTransactionFilter(const Json::Value&, Json::Value& response)
{
    // result: filterId(QTY)
    Json::Value result = co_await m_filterSystem->newPendingTxFilter();
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::uninstallFilter(const Json::Value& request, Json::Value& response)
{
    // params: filterId(QTY)
    // result: success(Boolean)
    auto const id = fromBigQuantity(toView(request[0U]));
    Json::Value result = co_await m_filterSystem->uninstallFilter(id);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getFilterChanges(const Json::Value& request, Json::Value& response)
{
    // params: filterId(QTY)
    // result: logs(ARRAY)
    auto const id = fromBigQuantity(toView(request[0U]));
    Json::Value result = co_await m_filterSystem->getFilterChanges(id);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getFilterLogs(const Json::Value& request, Json::Value& response)
{
    // params: filterId(QTY)
    // result: logs(ARRAY)
    auto const id = fromBigQuantity(toView(request[0U]));
    Json::Value result = co_await m_filterSystem->getFilterLogs(id);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getLogs(const Json::Value& request, Json::Value& response)
{
    // params: filter(FILTER)
    // result: logs(ARRAY)
    Json::Value jParams = request[0U];
    auto params = m_filterSystem->requestFactory()->create();
    params->fromJson(jParams);
    Json::Value result = co_await m_filterSystem->getLogs(params);
    buildJsonContent(result, response);
    co_return;
}
task::Task<std::tuple<protocol::BlockNumber, bool>> EthEndpoint::getBlockNumberByTag(
    std::string_view blockTag)
{
    auto ledger = m_nodeService->ledger();
    auto latest = co_await ledger::getCurrentBlockNumber(*ledger);
    auto [number, _] = bcos::rpc::getBlockNumberByTag(latest, blockTag);
    co_return std::make_tuple(number, std::cmp_equal(latest, number));
}