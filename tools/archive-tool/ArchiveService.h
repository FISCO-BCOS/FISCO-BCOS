/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief service for archive
 * @file ArchiveService.h
 * @author: xingqiangbai
 * @date 2022-11-09
 */
#pragma once
#include "bcos-boostssl/httpserver/HttpServer.h"
#include "bcos-boostssl/httpserver/HttpStream.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-ledger/src/libledger/Ledger.h"
#include "bcos-rpc/jsonrpc/Common.h"
#include "bcos-rpc/jsonrpc/JsonRpcInterface.h"
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-utilities/Error.h>
#include <json/json.h>
#include <functional>
#include <future>
#include <utility>

#define ARCHIVE_SERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[ARCHIVE]"

namespace bcos::archive
{
const std::string ARCHIVE_MODULE_NAME = "ArchiveService";
class ArchiveService : public std::enable_shared_from_this<ArchiveService>
{
public:
    virtual ~ArchiveService() = default;
    ArchiveService(bcos::storage::StorageInterface::Ptr _storage,
        std::shared_ptr<bcos::ledger::Ledger> _ledger,
        bcos::storage::StorageInterface::Ptr _blockStorage, std::string _listenIP,
        uint16_t _listenPort)
      : m_storage(std::move(_storage)),
        m_blockStorage(std::move(_blockStorage)),
        m_ledger(std::move(_ledger)),
        m_listenIP(std::move(_listenIP)),
        m_listenPort(_listenPort)
    {
        m_ioServicePool = std::make_shared<IOServicePool>();
        m_httpServer = std::make_shared<bcos::boostssl::http::HttpServer>(
            m_listenIP, m_listenPort, ARCHIVE_MODULE_NAME);
        auto acceptor =
            std::make_shared<boost::asio::ip::tcp::acceptor>((*m_ioServicePool->getIOService()));
        auto httpStreamFactory = std::make_shared<bcos::boostssl::http::HttpStreamFactory>();
        m_httpServer->setDisableSsl(true);
        m_httpServer->setAcceptor(acceptor);
        m_httpServer->setHttpStreamFactory(httpStreamFactory);
        m_httpServer->setIOServicePool(m_ioServicePool);
        // m_httpServer->setThreadPool(std::make_shared<ThreadPool>("archiveThread", 1));
        m_methodToFunc["deleteArchivedData"] = [this](const Json::Value& request,
                                                   const std::function<void(
                                                       bcos::Error::Ptr, Json::Value&)>& callback) {
            auto startBlock = request[0].asInt64();
            auto endBlock = request[1].asInt64();
            std::promise<std::pair<Error::Ptr, bcos::protocol::BlockNumber>> promise;
            m_ledger->asyncGetBlockNumber(
                [&promise](const Error::Ptr& err, bcos::protocol::BlockNumber number) {
                    promise.set_value(std::make_pair(err, number));
                });
            auto ret = promise.get_future().get();
            Json::Value result;
            if (ret.first)
            {
                result["status"] = "error, get block number failed, " + ret.first->errorMessage();
                ARCHIVE_SERVICE_LOG(WARNING)
                    << LOG_BADGE("deleteArchivedData GetBlockNumber failed")
                    << LOG_KV("message", ret.first->errorMessage());
                callback(nullptr, result);
                return;
            }
            auto currentBlock = ret.second;
            if (startBlock > currentBlock || endBlock > currentBlock || startBlock > endBlock ||
                startBlock <= 0)
            {
                result["status"] = "error, invalid block range";
                ARCHIVE_SERVICE_LOG(WARNING)
                    << LOG_BADGE("deleteArchivedData invalid range") << LOG_KV("start", startBlock)
                    << LOG_KV("end", endBlock) << LOG_KV("current", currentBlock);
                callback(nullptr, result);
                return;
            }
            // read archived block number to check the request range
            std::promise<std::pair<Error::Ptr, std::optional<bcos::storage::Entry>>> statePromise;
            m_ledger->asyncGetCurrentStateByKey(ledger::SYS_KEY_ARCHIVED_NUMBER,
                [&statePromise](Error::Ptr&& err, std::optional<bcos::storage::Entry>&& entry) {
                    statePromise.set_value(std::make_pair(err, entry));
                });
            auto archiveRet = statePromise.get_future().get();
            if (archiveRet.first)
            {
                result["status"] =
                    "error, get current state failed, " + archiveRet.first->errorMessage();
                ARCHIVE_SERVICE_LOG(WARNING)
                    << LOG_BADGE("deleteArchivedData get current state failed")
                    << LOG_KV("message", archiveRet.first->errorMessage());
                callback(nullptr, result);
                return;
            }
            protocol::BlockNumber archivedBlockNumber = 0;
            if (archiveRet.second)
            {
                try
                {
                    archivedBlockNumber = boost::lexical_cast<int64_t>(archiveRet.second->get());
                }
                catch (boost::bad_lexical_cast& e)
                {
                    ARCHIVE_SERVICE_LOG(DEBUG)
                        << "Lexical cast transaction count failed, entry value: "
                        << archiveRet.second->get();
                }
            }
            if (archivedBlockNumber > 0 && startBlock != archivedBlockNumber)
            {
                result["status"] = "error, start block is not equal to archived block, " +
                                   std::to_string(startBlock) +
                                   " != " + std::to_string(archivedBlockNumber);
                ARCHIVE_SERVICE_LOG(WARNING)
                    << LOG_BADGE("deleteArchivedData start block is not equal to archived block")
                    << LOG_KV("startBlock", startBlock)
                    << LOG_KV("archivedNumber", archivedBlockNumber);
                callback(nullptr, result);
                return;
            }
            auto err = deleteArchivedData(startBlock, endBlock);
            if (err)
            {
                ARCHIVE_SERVICE_LOG(WARNING)
                    << LOG_BADGE("deleteArchivedData failed") << LOG_KV("start", startBlock)
                    << LOG_KV("end", endBlock) << LOG_KV("message", err->errorMessage());
                result["status"] = "deleteArchivedData failed, " + err->errorMessage();
                callback(nullptr, result);
            }
            else
            {
                ARCHIVE_SERVICE_LOG(INFO) << LOG_BADGE("deleteArchivedData success")
                                          << LOG_KV("start", startBlock) << LOG_KV("end", endBlock);
                result["status"] = "success";
                // update SYS_CURRENT_STATE SYS_KEY_ARCHIVED_NUMBER
                storage::Entry archivedNumber;
                archivedNumber.importFields({std::to_string(endBlock)});
                m_storage->asyncSetRow(ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_ARCHIVED_NUMBER,
                    archivedNumber, [](Error::UniquePtr err) {
                        if (err)
                        {
                            ARCHIVE_SERVICE_LOG(WARNING)
                                << LOG_BADGE("deleteArchivedData set archived number failed")
                                << LOG_KV("message", err->errorMessage());
                        }
                    });
                callback(nullptr, result);
            }
            // TODO: should call compaction
            // delete nonces of historical blocks
            auto lastBlockToDeleteNonces =
                currentBlock > 5000 ? currentBlock - 5000 : 1;  // MAX_BLOCK_LIMIT is 5000
            if (lastBlockToDeleteNonces != 1)
            {
                auto numberRange =
                    RANGES::iota_view<uint64_t, uint64_t>(1, lastBlockToDeleteNonces);
                auto numberList = numberRange |
                                  RANGES::views::transform([](protocol::BlockNumber blockNumber) {
                                      return boost::lexical_cast<std::string>(blockNumber);
                                  }) |
                                  RANGES::to<std::vector<std::string>>();
                auto err = m_storage->deleteRows(ledger::SYS_BLOCK_NUMBER_2_NONCES, numberList);
                if (err)
                {
                    ARCHIVE_SERVICE_LOG(WARNING)
                        << LOG_BADGE("delete historical blocks nonces")
                        << LOG_KV("lastBlockToDeleteNonces", lastBlockToDeleteNonces)
                        << LOG_KV("message", err->errorMessage());
                }
            }
        };
        m_httpServer->setHttpReqHandler([this](auto&& PH1, auto&& PH2) {
            handleHttpRequest(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
        });
    }

    // virtual ~ArchiveService() = default;
    virtual void start()
    {
        m_ioServicePool->start();
        m_httpServer->start();
        ARCHIVE_SERVICE_LOG(INFO) << LOG_BADGE("start") << LOG_KV("listenIP", m_listenIP)
                                  << LOG_KV("listenPort", m_listenPort);
    }

    virtual void stop()
    {
        ARCHIVE_SERVICE_LOG(INFO) << LOG_BADGE("stop");
        m_ioServicePool->stop();
        m_httpServer->stop();
    }

    Error::Ptr deleteArchivedData(int64_t startBlock, int64_t endBlock)
    {  // delete blocks in [startBlock, endBlock)
        auto blockFlag = bcos::ledger::HEADER;
        for (int64_t blockNumber = startBlock; blockNumber < endBlock; blockNumber++)
        {
            std::promise<Error::Ptr> promise;
            m_ledger->asyncGetBlockTransactionHashes(
                blockNumber, [&](Error::Ptr&& error, std::vector<std::string>&& txHashes) {
                    if (error)
                    {
                        std::cerr << "get block failed: " << error->errorMessage();
                        promise.set_value(error);
                        return;
                    }
                    ARCHIVE_SERVICE_LOG(INFO)
                        << LOG_BADGE("deleteArchivedData") << LOG_KV("number", blockNumber)
                        << LOG_KV("size", txHashes.size());
                    auto blockStorage = m_blockStorage ? m_blockStorage : m_storage;
                    // delete block data: txs, receipts
                    auto err = blockStorage->deleteRows(ledger::SYS_HASH_2_TX, txHashes);
                    if (err)
                    {
                        ARCHIVE_SERVICE_LOG(WARNING)
                            << LOG_BADGE("delete transactions") << LOG_KV("number", blockNumber)
                            << LOG_KV("message", err->errorMessage());
                        promise.set_value(error);
                        return;
                    }
                    err = blockStorage->deleteRows(ledger::SYS_HASH_2_RECEIPT, txHashes);
                    if (err)
                    {
                        ARCHIVE_SERVICE_LOG(WARNING)
                            << LOG_BADGE("delete receipts") << LOG_KV("number", blockNumber)
                            << LOG_KV("message", err->errorMessage());
                        promise.set_value(error);
                        return;
                    }
                    promise.set_value(nullptr);
                });
            auto error = promise.get_future().get();
            if (error)
            {
                ARCHIVE_SERVICE_LOG(WARNING)
                    << LOG_BADGE("deleteArchivedData failed") << LOG_KV("number", blockNumber)
                    << LOG_KV("message", error->errorMessage());
                return error;
            }
        }
        return nullptr;
    }

    void handleHttpRequest(std::string_view _requestBody, std::function<void(bcos::bytes)> _sender)
    {
        bcos::rpc::JsonRequest request;
        bcos::rpc::JsonResponse response;
        try
        {
            bcos::rpc::parseRpcRequestJson(_requestBody, request);

            response.jsonrpc = request.jsonrpc;
            response.id = request.id;

            const auto& method = request.method;
            auto it = m_methodToFunc.find(method);
            if (it == m_methodToFunc.end())
            {
                ARCHIVE_SERVICE_LOG(DEBUG) << LOG_BADGE("handleHttpRequest method not found")
                                           << LOG_KV("request", _requestBody);
                BOOST_THROW_EXCEPTION(
                    bcos::rpc::JsonRpcException(bcos::rpc::JsonRpcError::MethodNotFound,
                        "The method does not exist/is not available."));
            }
            it->second(request.params, [_requestBody, response, _sender](
                                           const Error::Ptr& _error, Json::Value& _result) mutable {
                if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
                {
                    // error
                    response.error.code = _error->errorCode();
                    response.error.message = _error->errorMessage();
                }
                else
                {
                    response.result.swap(_result);
                }
                auto strResp = bcos::rpc::toStringResponse(std::move(response));
                ARCHIVE_SERVICE_LOG(TRACE)
                    << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody)
                    << LOG_KV("response",
                           std::string_view((const char*)strResp.data(), strResp.size()));
                _sender(std::move(strResp));
            });

            // success response
            return;
        }
        catch (const bcos::rpc::JsonRpcException& e)
        {
            response.error.code = e.code();
            response.error.message = std::string(e.what());
        }
        catch (const std::exception& e)
        {
            // server internal error or unexpected error
            response.error.code = bcos::rpc::JsonRpcError::InvalidRequest;
            response.error.message = std::string(e.what());
        }

        auto strResp = bcos::rpc::toStringResponse(response);

        ARCHIVE_SERVICE_LOG(DEBUG)
            << LOG_BADGE("handleHttpRequest") << LOG_KV("request", _requestBody)
            << LOG_KV("response", std::string_view((const char*)strResp.data(), strResp.size()));
        _sender(strResp);
    }

private:
    bcos::storage::StorageInterface::Ptr m_storage;
    bcos::storage::StorageInterface::Ptr m_blockStorage = nullptr;
    std::shared_ptr<bcos::ledger::Ledger> m_ledger;
    std::string m_listenIP;
    uint16_t m_listenPort;
    IOServicePool::Ptr m_ioServicePool;
    bcos::boostssl::http::HttpServer::Ptr m_httpServer;
    std::unordered_map<std::string, std::function<void(const Json::Value&,
                                        std::function<void(bcos::Error::Ptr, Json::Value&)>)>>
        m_methodToFunc;
};
}  // namespace bcos::archive
