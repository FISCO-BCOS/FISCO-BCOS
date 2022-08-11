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
#include <iterator>

namespace bcos::initializer {

class AnyLedger {
public:
  using Keccak256Ledger = bcos::ledger::LedgerImpl<
      bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher,
      bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr>>;
  using SM3Ledger = bcos::ledger::LedgerImpl<
      bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher,
      bcos::storage::StorageImpl<bcos::storage::StorageInterface::Ptr>>;
  using LedgerType = std::variant<Keccak256Ledger, SM3Ledger>;

  AnyLedger(LedgerType ledger) : m_ledger(std::move(ledger)) {}

  template <bcos::concepts::ledger::DataFlag... Flags>
  void getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
                bcos::concepts::block::Block auto &block) {
    std::visit(
        [blockNumber, &block](auto &ledger) {
          ledger.template getBlock<Flags...>(blockNumber, block);
        },
        m_ledger);
  }

  template <bcos::concepts::ledger::DataFlag... Flags>
  void setBlock(bcos::concepts::block::Block auto block) {
    std::visit(
        [block = std::move(block)](auto &ledger) {
          ledger.template setBlock<Flags...>(std::move(block));
        },
        m_ledger);
  }

  void getTransactionsOrReceipts(RANGES::range auto const &hashes,
                                 RANGES::range auto &out) {
    std::visit(
        [&hashes, &out](auto &ledger) {
          ledger.getTransactionsOrReceipts(hashes, out);
        },
        m_ledger);
  }

  auto getStatus() {
    bcos::concepts::ledger::Status status;
    std::visit([&status](auto &ledger) { status = ledger.getStatus(); },
               m_ledger);
    return status;
  }

  template <bcos::crypto::hasher::Hasher Hasher>
  void setTransactionsOrReceipts(RANGES::range auto const &inputs) requires
      bcos::concepts::ledger::TransactionOrReceipt<
          RANGES::range_value_t<decltype(inputs)>> {
    std::visit([&inputs]<Hasher>(auto &ledger) {
      ledger.template setTransactionsOrReceipts<Hasher>(inputs);
    });
  }

  template <bool isTransaction>
  void setTransactionOrReceiptBuffers(RANGES::range auto const &hashes,
                                      RANGES::range auto buffers) {
    std::visit(
        [
          &hashes, buffers = std::move(buffers)
        ]<bool _isTransaction = isTransaction>(auto &ledger) {
          ledger.template setTransactionOrReceiptBuffers<_isTransaction>(
              hashes, std::move(buffers));
        },
        m_ledger);
  }

  template <class LedgerType, bcos::concepts::block::Block BlockType>
  requires std::derived_from<LedgerType,
                             bcos::concepts::ledger::LedgerBase<LedgerType>> ||
      std::derived_from<
          typename LedgerType::element_type,
          bcos::concepts::ledger::LedgerBase<typename LedgerType::element_type>>
  void sync(LedgerType &source, bool onlyHeader) {
    std::visit(
        [&source, onlyHeader]<LedgerType, BlockType>(auto &ledger) {
          ledger.template sync<LedgerType, BlockType>();
        },
        m_ledger);
  }

private:
  LedgerType m_ledger;
};

class LightNodeInitializer {
public:
  void
  initLedgerServer(std::shared_ptr<bcos::front::FrontService> front,
                   std::shared_ptr<AnyLedger> anyLedger,
                   std::shared_ptr<bcos::transaction_pool::TransactionPoolImpl<
                       std::shared_ptr<bcos::txpool::TxPoolInterface>>>
                       transactionPool,
                   std::shared_ptr<bcos::scheduler::SchedulerWrapperImpl<
                       std::shared_ptr<bcos::scheduler::SchedulerInterface>>>
                       scheduler) {
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_GETBLOCK,
        [anyLedger, front](bcos::crypto::NodeIDPtr nodeID,
                           const std::string &id, bytesConstRef data) {
          bcostars::RequestBlock request;
          bcos::concepts::serialize::decode(data, request);

          LIGHTNODE_LOG(INFO) << "P2P get block:" << request.blockNumber;

          // bcostars::Block block;
          bcostars::ResponseBlock response;
          if (request.onlyHeader) {
            anyLedger->getBlock<bcos::concepts::ledger::HEADER>(
                request.blockNumber, response.block);
          } else {
            anyLedger->getBlock<bcos::concepts::ledger::ALL>(
                request.blockNumber, response.block);
          }

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(response, responseBuffer);
          front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETBLOCK,
                                   nodeID, bcos::ref(responseBuffer),
                                   [](Error::Ptr _error) {
                                     if (_error) {
                                     }
                                   });
        });
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_GETTRANSACTIONS,
        [anyLedger, front](bcos::crypto::NodeIDPtr nodeID,
                           const std::string &id, bytesConstRef data) {
          bcostars::RequestTransactions request;
          bcos::concepts::serialize::decode(data, request);

          bcostars::ResponseTransactions response;
          anyLedger->getTransactionsOrReceipts(request.hashes,
                                               response.transactions);

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(response, responseBuffer);
          front->asyncSendResponse(
              id, bcos::protocol::LIGHTNODE_GETTRANSACTIONS, nodeID,
              bcos::ref(responseBuffer), [](Error::Ptr _error) {
                if (_error) {
                }
              });
        });
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_GETRECEIPTS,
        [anyLedger, front](bcos::crypto::NodeIDPtr nodeID,
                           const std::string &id, bytesConstRef data) {
          bcostars::RequestReceipts request;
          bcos::concepts::serialize::decode(data, request);

          bcostars::ResponseReceipts response;
          anyLedger->getTransactionsOrReceipts(request.hashes,
                                               response.receipts);

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(response, responseBuffer);
          front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETRECEIPTS,
                                   nodeID, bcos::ref(responseBuffer),
                                   [](Error::Ptr _error) {
                                     if (_error) {
                                     }
                                   });
        });
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_GETSTATUS,
        [anyLedger, front](bcos::crypto::NodeIDPtr nodeID,
                           const std::string &id, bytesConstRef data) {
          bcostars::RequestGetStatus request;
          bcos::concepts::serialize::decode(data, request);

          bcostars::ResponseGetStatus response;
          auto status = anyLedger->getStatus();
          response.total = status.total;
          response.failed = status.failed;
          response.blockNumber = status.blockNumber;

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(response, responseBuffer);
          front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETSTATUS,
                                   nodeID, bcos::ref(responseBuffer),
                                   [](Error::Ptr _error) {
                                     if (_error) {
                                     }
                                   });
        });
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_SENDTRANSACTION,
        [transactionPool, front](bcos::crypto::NodeIDPtr nodeID,
                                 const std::string &id, bytesConstRef data) {
          bcostars::ResponseSendTransaction response;
          bcostars::RequestSendTransaction request;
          bcos::concepts::serialize::decode(data, request);

          std::string transactionHash;
          boost::algorithm::hex_lower(request.transaction.dataHash.begin(),
                                      request.transaction.dataHash.end(),
                                      std::back_inserter(transactionHash));

          LIGHTNODE_LOG(INFO) << "Send transaction: " << transactionHash;

          transactionPool->submitTransaction(std::move(request.transaction),
                                             response.receipt);

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(response, responseBuffer);
          front->asyncSendResponse(
              id, bcos::protocol::LIGHTNODE_SENDTRANSACTION, nodeID,
              bcos::ref(responseBuffer), [](Error::Ptr _error) {
                if (_error) {
                }
              });
        });
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_CALL,
        [scheduler, front](bcos::crypto::NodeIDPtr nodeID,
                           const std::string &id, bytesConstRef data) {
          bcostars::RequestSendTransaction request;
          bcos::concepts::serialize::decode(data, request);

          std::string transactionHash;
          boost::algorithm::hex_lower(request.transaction.dataHash.begin(),
                                      request.transaction.dataHash.end(),
                                      std::back_inserter(transactionHash));

          LIGHTNODE_LOG(INFO) << "Call: " << transactionHash;

          bcostars::ResponseSendTransaction response;
          scheduler->call(request.transaction, response.receipt);

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(response, responseBuffer);
          front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_CALL, nodeID,
                                   bcos::ref(responseBuffer),
                                   [](Error::Ptr _error) {
                                     if (_error) {
                                     }
                                   });
        });
  }
};
} // namespace bcos::initializer