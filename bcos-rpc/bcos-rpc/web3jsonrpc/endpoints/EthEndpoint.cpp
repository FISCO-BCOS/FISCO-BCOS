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

#include <bcos-rpc/Common.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthMethods.h>

using namespace bcos;
using namespace bcos::rpc;

EthEndpoint::EthEndpoint(std::string _groupId, bcos::rpc::GroupManager::Ptr groupManager)
  : m_groupId(std::move(_groupId)), m_groupManager(std::move(groupManager))
{
    initMethod();
}

void EthEndpoint::initMethod()
{
    // clang-format off
    m_methods[methodString(EthMethod::eth_protocolVersion)] = std::bind(&EthEndpoint::protocolVersionInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_syncing)] = std::bind(&EthEndpoint::syningInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_coinbase)] = std::bind(&EthEndpoint::coinbaseInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_chainId)] = std::bind(&EthEndpoint::chainIdInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_mining)] = std::bind(&EthEndpoint::miningInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_hashrate)] = std::bind(&EthEndpoint::hashrateInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_gasPrice)] = std::bind(&EthEndpoint::gasPriceInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_accounts)] = std::bind(&EthEndpoint::accountsInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_blockNumber)] = std::bind(&EthEndpoint::blockNumberInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getBalance)] = std::bind(&EthEndpoint::getBalanceInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getStorageAt)] = std::bind(&EthEndpoint::getStorageAtInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getTransactionCount)] = std::bind(&EthEndpoint::getTransactionCountInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getBlockTransactionCountByHash)] = std::bind(&EthEndpoint::getBlockTxCountByHashInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getBlockTransactionCountByNumber)] = std::bind(&EthEndpoint::getBlockTxCountByNumberInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getUncleCountByBlockHash)] = std::bind(&EthEndpoint::getUncleCountByBlockHashInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getUncleCountByBlockNumber)] = std::bind(&EthEndpoint::getUncleCountByBlockNumberInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getCode)] = std::bind(&EthEndpoint::getCodeInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_sign)] = std::bind(&EthEndpoint::signInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_sendTransaction)] = std::bind(&EthEndpoint::sendTransactionInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_signTransaction)] = std::bind(&EthEndpoint::signTransactionInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_sendRawTransaction)] = std::bind(&EthEndpoint::sendRawTransactionInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_call)] = std::bind(&EthEndpoint::callInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_estimateGas)] = std::bind(&EthEndpoint::estimateGasInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getBlockByHash)] = std::bind(&EthEndpoint::getBlockByHashInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getBlockByNumber)] = std::bind(&EthEndpoint::getBlockByNumberInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getTransactionByHash)] = std::bind(&EthEndpoint::getTransactionByHashInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getTransactionByBlockHashAndIndex)] = std::bind(&EthEndpoint::getTransactionByBlockHashAndIndexInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getTransactionByBlockNumberAndIndex)] = std::bind(&EthEndpoint::getTransactionByBlockNumberAndIndexInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getTransactionReceipt)] = std::bind(&EthEndpoint::getTransactionReceiptInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getUncleByBlockHashAndIndex)] = std::bind(&EthEndpoint::getUncleByBlockHashAndIndexInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getUncleByBlockNumberAndIndex)] = std::bind(&EthEndpoint::getUncleByBlockNumberAndIndexInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_newFilter)] = std::bind(&EthEndpoint::newFilterInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_newBlockFilter)] = std::bind(&EthEndpoint::newBlockFilterInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_newPendingTransactionFilter)] = std::bind(&EthEndpoint::newPendingTransactionFilterInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_uninstallFilter)] = std::bind(&EthEndpoint::uninstallFilterInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getFilterChanges)] = std::bind(&EthEndpoint::getFilterChangesInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getFilterLogs)] = std::bind(&EthEndpoint::getFilterLogsInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::eth_getLogs)] = std::bind(&EthEndpoint::getLogsInterface, this, std::placeholders::_1, std::placeholders::_2);
    // clang-format on
    for (auto& [method, _] : m_methods)
    {
        RPC_IMPL_LOG(INFO) << LOG_BADGE("initMethod") << LOG_KV("method", method);
    }
    RPC_IMPL_LOG(INFO) << LOG_BADGE("initMethod") << LOG_KV("size", m_methods.size());
}

// TODO: error code

void EthEndpoint::protocolVersion(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}

void EthEndpoint::syning(RespFunc func)
{
    // TODO: add get sync object in sync interface
    Json::Value result = false;
    func(nullptr, result);
}
void EthEndpoint::coinbase(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::chainId(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::mining(RespFunc func)
{
    Json::Value result = false;
    func(nullptr, result);
}
void EthEndpoint::hashrate(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::gasPrice(RespFunc func)
{
    // TODO: add get gas price interface
    Json::Value result = "0x1";
    func(nullptr, result);
}
void EthEndpoint::accounts(RespFunc func)
{
    // return empty array by default
    Json::Value result = Json::arrayValue;
    func(nullptr, result);
}
void EthEndpoint::blockNumber(RespFunc func)
{
    auto ledger = getNodeService()->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBlockNumber(
        [m_respFunc = std::move(func)](Error::Ptr _error, protocol::BlockNumber _blockNumber) {
            if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
            {
                RPC_IMPL_LOG(INFO) << LOG_BADGE("getBlockNumber failed")
                                   << LOG_KV("code", _error ? _error->errorCode() : 0)
                                   << LOG_KV("message", _error ? _error->errorMessage() : "success")
                                   << LOG_KV("blockNumber", _blockNumber);
            }

            Json::Value jResp = toQuantity(_blockNumber);
            m_respFunc(_error, jResp);
        });
}
void EthEndpoint::getBalance(std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getStorageAt(std::string_view, std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getTransactionCount(std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getBlockTxCountByHash(std::string_view hash, RespFunc func)
{
    auto ledger = getNodeService()->ledger();
    checkService(ledger, "ledger");
    auto weakLedger = std::weak_ptr<ledger::LedgerInterface>(ledger);
    ledger->asyncGetBlockNumberByHash(bcos::crypto::HashType(hash, crypto::HashType::FromHex),
        [m_hash = std::string(hash), m_func = std::move(func), weakLedger](
            auto&& _error, protocol::BlockNumber blockNumber) {
            if (!_error || _error->errorCode() == bcos::protocol::CommonError::SUCCESS)
            {
                auto ledger = weakLedger.lock();
                if (ledger)
                {
                    ledger->asyncGetBlockDataByNumber(blockNumber, bcos::ledger::TRANSACTIONS_HASH,
                        [m_func = std::move(m_func), blockNumber](
                            auto&& error, protocol::Block::Ptr block) {
                            Json::Value jResp;
                            if (error && error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                            {
                                RPC_IMPL_LOG(INFO)
                                    << LOG_BADGE("getBlockByNumber failed")
                                    << LOG_KV("blockNumber", blockNumber)
                                    << LOG_KV("code", error ? error->errorCode() : 0)
                                    << LOG_KV("message", error ? error->errorMessage() : "success");
                            }
                            else
                            {
                                auto size = block->transactionsSize();
                                jResp = toQuantity(size);
                            }
                            m_func(error, jResp);
                        });
                }
            }
            else
            {
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getBlockTxCountByHash failed") << LOG_KV("blockHash", m_hash)
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
                Json::Value jResp;
                m_func(_error, jResp);
            }
        });
}
void EthEndpoint::getBlockTxCountByNumber(std::string_view number, RespFunc func)
{
    // auto ledger = getNodeService()->ledger();
    // checkService(ledger, "ledger");
    // ledger->asyncGetBlockDataByNumber(blockNumber, bcos::ledger::TRANSACTIONS_HASH,
    //     [m_func = std::move(func), blockNumber](auto&& error, protocol::Block::Ptr block) {
    //         Json::Value jResp;
    //         if (error && error->errorCode() != bcos::protocol::CommonError::SUCCESS)
    //         {
    //             RPC_IMPL_LOG(INFO)
    //                 << LOG_BADGE("getBlockByNumber failed") << LOG_KV("blockNumber", blockNumber)
    //                 << LOG_KV("code", error ? error->errorCode() : 0)
    //                 << LOG_KV("message", error ? error->errorMessage() : "success");
    //         }
    //         else
    //         {
    //             auto size = block->transactionsSize();
    //             jResp = toQuantity(size);
    //         }
    //         m_func(error, jResp);
    //     });
}
void EthEndpoint::getUncleCountByBlockHash(std::string_view, RespFunc func)
{
    Json::Value result = "0x0";
    func(nullptr, result);
}
void EthEndpoint::getUncleCountByBlockNumber(std::string_view, RespFunc func)
{
    Json::Value result = "0x0";
    func(nullptr, result);
}
void EthEndpoint::getCode(std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::sign(std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::signTransaction(Json::Value const&, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::sendTransaction(Json::Value const&, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::sendRawTransaction(std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::call(Json::Value const&, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::estimateGas(Json::Value const&, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getBlockByHash(std::string_view, bool, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getBlockByNumber(std::string_view, bool, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getTransactionByHash(std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getTransactionByBlockHashAndIndex(
    std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getTransactionByBlockNumberAndIndex(
    std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getTransactionReceipt(std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getUncleByBlockHashAndIndex(std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getUncleByBlockNumberAndIndex(std::string_view, std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::newFilter(Json::Value const&, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::newBlockFilter(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::newPendingTransactionFilter(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::uninstallFilter(std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getFilterChanges(std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getFilterLogs(std::string_view, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void EthEndpoint::getLogs(Json::Value const&, RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
