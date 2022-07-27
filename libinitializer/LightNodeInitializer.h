#pragma once

#include "bcos-crypto/hasher/OpenSSLHasher.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-lightnode/storage/StorageSyncWrapper.h"
#include <bcos-front/FrontService.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>

namespace bcos::initializer {

class AnyLedger
    : public std::variant<
          bcos::ledger::LedgerImpl<
              bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher,
              bcos::storage::StorageSyncWrapper<
                  bcos::storage::StorageInterface::Ptr>>,
          bcos::ledger::LedgerImpl<
              bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher,
              bcos::storage::StorageSyncWrapper<
                  bcos::storage::StorageInterface::Ptr>>> {
public:
  using std::variant<
      bcos::ledger::LedgerImpl<
          bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher,
          bcos::storage::StorageSyncWrapper<
              bcos::storage::StorageInterface::Ptr>>,
      bcos::ledger::LedgerImpl<
          bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher,
          bcos::storage::StorageSyncWrapper<
              bcos::storage::StorageInterface::Ptr>>>::variant;

  template <bcos::concepts::ledger::DataFlag... flags>
  void getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
                bcos::concepts::block::Block auto &block) {
    std::visit([blockNumber,
                &block](auto &self) { self.getBlock(blockNumber, block); },
               *this);
  }

  template <bcos::concepts::ledger::DataFlag... flags>
  void setBlock(bcos::concepts::block::Block auto block) {
    std::visit([block = std::move(block)](
                   auto &self) { self.setBlock(std::move(block)); },
               *this);
  }

  template <bcos::concepts::ledger::DataFlag flag>
  void getTransactionsOrReceipts(RANGES::range auto const &hashes,
                                 RANGES::range auto &out) {
    std::visit([&hashes, &out](
                   auto &self) { self.getTransactionsOrReceipts(hashes, out); },
               *this);
  }

  auto getStatus() {
    bcos::concepts::ledger::Status status;

    // std::visit([&status](auto &self) { status = self.getStatus(); }, *this);

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
        *this);
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
               *this);
  }
};

class LightNodeInitializer {
public:
  // LightNodeInitializer()

private:
};
} // namespace bcos::initializer