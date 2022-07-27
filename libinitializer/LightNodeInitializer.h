#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "bcos-framework/protocol/Protocol.h"
#include "generated/bcos-tars-protocol/tars/LightNode.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-front/FrontService.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>
#include <bcos-lightnode/storage/StorageSyncWrapper.h>
#include <bcos-tars-protocol/tars/LightNode.h>

namespace bcos::initializer {

class AnyLedger {
public:
  using Keccak256Ledger = bcos::ledger::LedgerImpl<
      bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher,
      bcos::storage::StorageSyncWrapper<bcos::storage::StorageInterface::Ptr>>;
  using SM3Ledger = bcos::ledger::LedgerImpl<
      bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher,
      bcos::storage::StorageSyncWrapper<bcos::storage::StorageInterface::Ptr>>;
  using LedgerType = std::variant<Keccak256Ledger, SM3Ledger>;

  AnyLedger(LedgerType ledger) : m_ledger(std::move(ledger)) {}

  template <bcos::concepts::ledger::DataFlag... flags>
  void getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
                bcos::concepts::block::Block auto &block) {
    std::visit([blockNumber,
                &block](auto &self) { self.getBlock(blockNumber, block); },
               m_ledger);
  }

  template <bcos::concepts::ledger::DataFlag... flags>
  void setBlock(bcos::concepts::block::Block auto block) {
    std::visit([block = std::move(block)](
                   auto &self) { self.setBlock(std::move(block)); },
               m_ledger);
  }

  template <bcos::concepts::ledger::DataFlag flag>
  void getTransactionsOrReceipts(RANGES::range auto const &hashes,
                                 RANGES::range auto &out) {
    std::visit([&hashes, &out](
                   auto &self) { self.getTransactionsOrReceipts(hashes, out); },
               m_ledger);
  }

  auto getStatus() {
    bcos::concepts::ledger::Status status;
    std::visit([&status](auto &self) { status = self.getStatus(); }, m_ledger);
    return status;
  }

  template <bcos::crypto::hasher::Hasher Hasher>
  void setTransactionsOrReceipts(RANGES::range auto const &inputs) requires
      bcos::concepts::ledger::TransactionOrReceipt<
          RANGES::range_value_t<decltype(inputs)>> {
    std::visit([&inputs]<Hasher>(auto &self) {
      self.template setTransactionsOrReceipts<Hasher>(inputs);
    });
  }

  template <bool isTransaction>
  void setTransactionOrReceiptBuffers(RANGES::range auto const &hashes,
                                      RANGES::range auto buffers) {
    std::visit(
        [
          &hashes, buffers = std::move(buffers)
        ]<bool _isTransaction = isTransaction>(auto &self) {
          self.template setTransactionOrReceiptBuffers<_isTransaction>(
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
    std::visit([&source, onlyHeader]<LedgerType, BlockType>(
                   auto &self) { self.template sync<LedgerType, BlockType>(); },
               m_ledger);
  }

private:
  LedgerType m_ledger;
};

class LightNodeInitializer {
public:
  void init(std::shared_ptr<bcos::front::FrontService> front,
            std::shared_ptr<AnyLedger> anyLedger) {
    front->registerModuleMessageDispatcher(
        bcos::protocol::LIGHTNODE_GETBLOCK,
        [anyLedger, front](bcos::crypto::NodeIDPtr nodeID,
                           const std::string &id, bytesConstRef data) {
          bcostars::RequestBlock request;
          bcos::concepts::serialize::decode(data, request);

          bcostars::Block block;
          if (request.onlyHeader) {
            anyLedger->getBlock<bcos::concepts::ledger::HEADER>(
                request.blockNumber, block);
          } else {
            anyLedger->getBlock<bcos::concepts::ledger::ALL>(
                request.blockNumber, block);
          }

          bcos::bytes responseBuffer;
          bcos::concepts::serialize::encode(block, responseBuffer);
          front->asyncSendResponse(id, bcos::protocol::LIGHTNODE_GETBLOCK,
                                   nodeID, bcos::ref(responseBuffer),
                                   [](Error::Ptr _error) {
                                     if (_error) {
                                     }
                                   });
        });
  }

private:
};
} // namespace bcos::initializer