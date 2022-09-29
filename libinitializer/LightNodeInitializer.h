#pragma once

#include "bcos-concepts/Exception.h"
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
                auto init = self.lock();
                auto front = weakFront.lock();
                if (!front || !init)
                {
                    return;
                }

                bcostars::RequestBlock request;
                init->decodeRequest<bcostars::ResponseBlock>(
                    request, front, protocol::LIGHTNODE_GETBLOCK, nodeID, id, data);
                bcos::task::wait(init->getBlock(front, *anyLedger, nodeID, id, std::move(request)));
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
                            ~ledger.getTransactions(request.hashes, response.transactions);
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
                        {}
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
                            ~ledger.getTransactions(request.hashes, response.receipts);
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
                        {}
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
                            auto status = ~ledger.getStatus();
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
                        {}
                    });
            });
        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_SENDTRANSACTION,
            [transactionPool, self, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) {
                auto front = weakFront.lock();
                auto init = self.lock();
                if (!front || !init)
                {
                    return;
                }
                bcostars::RequestSendTransaction request;
                init->decodeRequest<bcostars::ResponseSendTransaction>(
                    request, front, protocol::LIGHTNODE_SENDTRANSACTION, nodeID, id, data);
                bcos::task::wait(init->submitTransaction(
                    front, transactionPool, nodeID, id, std::move(request)));
            });

        front->registerModuleMessageDispatcher(bcos::protocol::LIGHTNODE_CALL,
            [self, scheduler, weakFront](
                bcos::crypto::NodeIDPtr nodeID, const std::string& id, bytesConstRef data) mutable {
                auto front = weakFront.lock();
                auto init = self.lock();
                if (!front || !init)
                {
                    return;
                }

                bcostars::RequestSendTransaction request;
                init->decodeRequest<bcostars::ResponseSendTransaction>(
                    request, front, protocol::LIGHTNODE_CALL, nodeID, id, data);
                bcos::task::wait(init->call(front, scheduler, nodeID, id, std::move(request)));
            });
    }

private:
    template <class Response>
    bool decodeRequest(auto& request, std::shared_ptr<bcos::front::FrontService> front,
        bcos::protocol::ModuleID moduleID, bcos::crypto::NodeIDPtr nodeID, const std::string& id,
        bytesConstRef data)
    {
        bool success = true;
        try
        {
            bcos::concepts::serialize::decode(data, request);
            return success;
        }
        catch (std::exception const& e)
        {
            Response response;
            response.error.errorCode = -1;
            response.error.errorMessage = boost::diagnostic_information(e);
            success = false;

            bcos::bytes responseBuffer;
            bcos::concepts::serialize::encode(response, responseBuffer);
            front->asyncSendResponse(
                std::string(id), moduleID, nodeID, bcos::ref(responseBuffer), [](Error::Ptr) {});
        }

        return success;
    }

    task::Task<void> getBlock(std::shared_ptr<bcos::front::FrontService> front,
        AnyLedger& anyLedger, bcos::crypto::NodeIDPtr nodeID, const std::string& id,
        bcostars::RequestBlock request)
    {
        bcostars::ResponseBlock response;
        try
        {
            LIGHTNODE_LOG(INFO) << "Get block:" << request.blockNumber << " | "
                                << request.onlyHeader;

            co_await std::visit(
                [&request, &response](auto& ledger) -> task::Task<void> {
                    if (request.onlyHeader)
                    {
                        co_await ledger.template getBlock<bcos::concepts::ledger::HEADER>(
                            request.blockNumber, response.block);
                    }
                    else
                    {
                        co_await ledger.template getBlock<bcos::concepts::ledger::ALL>(
                            request.blockNumber, response.block);
                    }
                },
                anyLedger);
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
                {}
            });
    }

    task::Task<void> submitTransaction(std::shared_ptr<bcos::front::FrontService> front,
        std::shared_ptr<bcos::transaction_pool::TransactionPoolImpl<
            std::shared_ptr<bcos::txpool::TxPoolInterface>>>
            transactionPool,
        bcos::crypto::NodeIDPtr nodeID, const std::string& id,
        bcostars::RequestSendTransaction request)
    {
        bcostars::ResponseSendTransaction response;
        try
        {
            co_await transactionPool->submitTransaction(
                std::move(request.transaction), response.receipt);
        }
        catch (std::exception& e)
        {
            response.error.errorCode = -1;
            response.error.errorMessage = boost::diagnostic_information(e);
        }

        bcos::bytes responseBuffer;
        bcos::concepts::serialize::encode(response, responseBuffer);
        front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_SENDTRANSACTION, nodeID,
            bcos::ref(responseBuffer), [](Error::Ptr) {});
    }

    task::Task<void> call(std::shared_ptr<bcos::front::FrontService> front,
        std::shared_ptr<bcos::scheduler::SchedulerWrapperImpl<
            std::shared_ptr<bcos::scheduler::SchedulerInterface>>>
            scheduler,
        bcos::crypto::NodeIDPtr nodeID, std::string id, bcostars::RequestSendTransaction request)
    {
        bcostars::ResponseSendTransaction response;
        try
        {
            co_await scheduler->call(request.transaction, response.receipt);
        }
        catch (std::exception& e)
        {
            response.error.errorCode = -1;
            response.error.errorMessage = boost::diagnostic_information(e);
        }

        bcos::bytes responseBuffer;
        bcos::concepts::serialize::encode(response, responseBuffer);
        front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_CALL, nodeID,
            bcos::ref(responseBuffer), [](Error::Ptr) {});
    }
};
}  // namespace bcos::initializer