#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-front/FrontService.h>
#include <bcos-lightnode/Log.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>
#include <bcos-lightnode/scheduler/SchedulerWrapperImpl.h>
#include <bcos-lightnode/storage/StorageImpl.h>
#include <bcos-lightnode/transaction_pool/TransactionPoolImpl.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-scheduler/src/SchedulerImpl.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <iterator>

namespace bcos::initializer
{
using Keccak256Ledger =
    bcos::ledger::LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher,
        bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr>>;
using SM3Ledger = bcos::ledger::LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher,
    bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr>>;
using SHA3Ledger = bcos::ledger::LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher,
    bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr>>;
using SHA2Ledger = bcos::ledger::LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SHA2_256_Hasher,
    bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr>>;
using AnyLedger = std::variant<Keccak256Ledger, SM3Ledger, SHA3Ledger, SHA2Ledger>;

class LightNodeInitializer : public std::enable_shared_from_this<LightNodeInitializer>
{
public:
    LightNodeInitializer()
    {
        m_lightNodePool = std::make_shared<bcos::ThreadPool>("lightNodePool", 4);
    }
    // Note: FrontService is owned by Initializier for the entire lifetime
    void initLedgerServer(std::shared_ptr<bcos::front::FrontService> front,
        std::shared_ptr<AnyLedger> anyLedger,
        std::shared_ptr<bcos::transaction_pool::TransactionPoolImpl<
            std::shared_ptr<bcos::txpool::TxPoolInterface>>>
            transactionPool,
        std::shared_ptr<bcos::scheduler::SchedulerWrapperImpl<
            std::shared_ptr<bcos::scheduler::SchedulerInterface>>>
            scheduler)
    {
        auto weakFront = std::weak_ptr<bcos::front::FrontService>(front);
        auto self = std::weak_ptr<LightNodeInitializer>(shared_from_this());
        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_GETBLOCK,
            [self, anyLedger, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                auto lightNodeInit = self.lock();
                if (!lightNodeInit)
                {
                    return;
                }
                lightNodeInit->getBlock(weakFront, anyLedger, nodeID, id, data);
            });

        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_GETTRANSACTIONS,
            [anyLedger, front](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                bcostars::ResponseTransactions response;

                try
                {
                    bcostars::RequestTransactions request;
                    bcos::concepts::serialize::decode(data, request);

                    LIGHTNODE_LOG(INFO) << "Get transactions:" << request.hashes.size() << " | "
                                        << request.withProof;

                    std::visit(
                        [&request, &response](auto& ledger) {
                            ledger.getTransactions(request.hashes, response.transactions);
                        },
                        *anyLedger);
                }
                catch (std::exception& e)
                {
                    response.error.errorCode = -1;
                    response.error.errorMessage = boost::diagnostic_information(e);
                }

                bcos::bytes responseBuffer;
                bcos::concepts::serialize::encode(response, responseBuffer);
                front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETTRANSACTIONS, nodeID,
                    bcos::ref(responseBuffer), [](Error::Ptr _error) {
                        if (_error)
                        {
                        }
                    });
            });
        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_GETRECEIPTS,
            [anyLedger, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                auto sharedFront = weakFront.lock();
                if (!sharedFront)
                {
                    return;
                }
                bcostars::ResponseReceipts response;

                try
                {
                    bcostars::RequestReceipts request;
                    bcos::concepts::serialize::decode(data, request);

                    std::visit(
                        [&request, &response](auto& ledger) {
                            ledger.getTransactions(request.hashes, response.receipts);
                        },
                        *anyLedger);
                }
                catch (std::exception& e)
                {
                    response.error.errorCode = -1;
                    response.error.errorMessage = boost::diagnostic_information(e);
                }

                bcos::bytes responseBuffer;
                bcos::concepts::serialize::encode(response, responseBuffer);
                sharedFront->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETRECEIPTS, nodeID,
                    bcos::ref(responseBuffer), [](Error::Ptr _error) {
                        if (_error)
                        {
                        }
                    });
            });
        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_GETSTATUS,
            [anyLedger, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                auto sharedFront = weakFront.lock();
                if (!sharedFront)
                {
                    return;
                }
                bcostars::ResponseGetStatus response;

                try
                {
                    bcostars::RequestGetStatus request;
                    bcos::concepts::serialize::decode(data, request);

                    std::visit(
                        [&response](auto& ledger) {
                            auto status = ledger.getStatus();
                            response.total = status.total;
                            response.failed = status.failed;
                            response.blockNumber = status.blockNumber;
                        },
                        *anyLedger);
                }
                catch (std::exception& e)
                {
                    response.error.errorCode = -1;
                    response.error.errorMessage = boost::diagnostic_information(e);
                }

                bcos::bytes responseBuffer;
                bcos::concepts::serialize::encode(response, responseBuffer);
                sharedFront->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETSTATUS, nodeID,
                    bcos::ref(responseBuffer), [](Error::Ptr _error) {
                        if (_error)
                        {
                        }
                    });
            });
        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_SENDTRANSACTION,
            [transactionPool, self, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                auto init = self.lock();
                if (!init)
                {
                    return;
                }
                init->submitTransaction(weakFront, transactionPool, nodeID, id, data);
            });

        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_CALL,
            [self, scheduler, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                auto init = self.lock();
                if (!init)
                {
                    return;
                }
                init->call(weakFront, scheduler, nodeID, id, data);
            });
    }

private:
    void getBlock(std::weak_ptr<bcos::front::FrontService> weakFront,
        std::shared_ptr<AnyLedger> anyLedger, bcos::crypto::NodeIDPtr nodeID, const std::string& id,
        bytesConstRef data)
    {
        auto front = weakFront.lock();
        if (!front)
        {
            return;
        }
        bcostars::ResponseBlock response;
        try
        {
            bcostars::RequestBlock request;
            bcos::concepts::serialize::decode(data, request);

            LIGHTNODE_LOG(INFO) << "Get block:" << request.blockNumber << " | "
                                << request.onlyHeader;

            std::visit(
                [&request, &response](auto& ledger) {
                    if (request.onlyHeader)
                    {
                        ledger.template getBlock<bcos::concepts::ledger::HEADER>(
                            request.blockNumber, response.block);
                    }
                    else
                    {
                        ledger.template getBlock<bcos::concepts::ledger::ALL>(
                            request.blockNumber, response.block);
                    }
                },
                *anyLedger);
        }
        catch (std::exception& e)
        {
            response.error.errorCode = -1;
            response.error.errorMessage = boost::diagnostic_information(e);
        }

        bcos::bytes responseBuffer;
        bcos::concepts::serialize::encode(response, responseBuffer);
        front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETBLOCK, nodeID,
            bcos::ref(responseBuffer), [](Error::Ptr _error) {
                if (_error)
                {
                }
            });
    }

    template <typename T, typename S>
    bool decodeRequest(T& request, S& response, std::shared_ptr<bcos::front::FrontService> front,
        bcos::protocol::ModuleID moduleID, bcos::crypto::NodeIDPtr nodeID, const std::string& id,
        bytesConstRef data)
    {
        bool success = true;
        try
        {
            bcos::concepts::serialize::decode(data, request);
        }
        catch (std::exception const& e)
        {
            response.error.errorCode = -1;
            response.error.errorMessage = boost::diagnostic_information(e);
            success = false;
        }
        bcos::bytes responseBuffer;
        bcos::concepts::serialize::encode(response, responseBuffer);
        front->asyncSendResponse(
            id, moduleID, nodeID, bcos::ref(responseBuffer), [](Error::Ptr _error) {
                if (_error)
                {
                }
            });
        return success;
    }

    void submitTransaction(std::weak_ptr<bcos::front::FrontService> weakFront,
        std::shared_ptr<bcos::transaction_pool::TransactionPoolImpl<
            std::shared_ptr<bcos::txpool::TxPoolInterface>>>
            transactionPool,
        bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data)
    {
        auto front = weakFront.lock();
        if (!front)
        {
            return;
        }
        bcostars::ResponseSendTransaction sendTxsResponse;
        bcostars::RequestSendTransaction sendTxsRequest;
        auto moduleID = bcos::protocol::LIGHTNODE_SENDTRANSACTION;
        if (!decodeRequest(sendTxsRequest, sendTxsResponse, front, moduleID, nodeID, id, data))
        {
            return;
        }
        m_lightNodePool->enqueue(
            [response = std::move(sendTxsResponse), request = std::move(sendTxsRequest), nodeID, id,
                transactionPool, weakFront, moduleID]() mutable {
                auto front = weakFront.lock();
                if (!front)
                {
                    return;
                }
                try
                {
                    std::string transactionHash;
                    transactionHash.reserve(request.transaction.dataHash.size() * 2);
                    boost::algorithm::hex_lower(request.transaction.dataHash.begin(),
                        request.transaction.dataHash.end(), std::back_inserter(transactionHash));
                    LIGHTNODE_LOG(INFO) << "Send transaction: " << transactionHash;

                    transactionPool->submitTransaction(
                        std::move(request.transaction), response.receipt);
                }
                catch (std::exception& e)
                {
                    response.error.errorCode = -1;
                    response.error.errorMessage = boost::diagnostic_information(e);
                }

                bcos::bytes responseBuffer;
                bcos::concepts::serialize::encode(response, responseBuffer);
                front->asyncSendResponse(
                    id, moduleID, nodeID, bcos::ref(responseBuffer), [](Error::Ptr _error) {
                        if (_error)
                        {
                        }
                    });
            });
    }

    void call(std::weak_ptr<bcos::front::FrontService> weakFront,
        std::shared_ptr<bcos::scheduler::SchedulerWrapperImpl<
            std::shared_ptr<bcos::scheduler::SchedulerInterface>>>
            scheduler,
        bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data)
    {
        auto front = weakFront.lock();
        if (!front)
        {
            return;
        }
        bcostars::ResponseSendTransaction sendTxResponse;
        bcostars::RequestSendTransaction sendTxsRequest;
        auto moduleID = bcos::protocol::LIGHTNODE_CALL;
        if (!decodeRequest(sendTxsRequest, sendTxResponse, front, moduleID, nodeID, id, data))
        {
            return;
        }
        m_lightNodePool->enqueue(
            [request = std::move(sendTxsRequest), response = std::move(sendTxResponse), weakFront,
                scheduler, nodeID, id, moduleID]() mutable {
                auto front = weakFront.lock();
                if (!front)
                {
                    return;
                }
                try
                {
                    std::string to;
                    to.reserve(request.transaction.data.to.size() * 2);
                    boost::algorithm::hex_lower(request.transaction.data.to.begin(),
                        request.transaction.data.to.end(), std::back_inserter(to));

                    LIGHTNODE_LOG(INFO) << "Call to: " << to;

                    scheduler->call(request.transaction, response.receipt);
                }
                catch (std::exception& e)
                {
                    response.error.errorCode = -1;
                    response.error.errorMessage = boost::diagnostic_information(e);
                }

                bcos::bytes responseBuffer;
                bcos::concepts::serialize::encode(response, responseBuffer);
                front->asyncSendResponse(
                    id, moduleID, nodeID, bcos::ref(responseBuffer), [](Error::Ptr _error) {
                        if (_error)
                        {
                        }
                    });
            });
    }

private:
    std::shared_ptr<bcos::ThreadPool> m_lightNodePool;
};
}  // namespace bcos::initializer