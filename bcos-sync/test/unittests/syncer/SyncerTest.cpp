#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "../../../bcos-syncer/BlockSyncerClient.h"
#include <bcos-framework/concepts/ledger/Ledger.h>
#include <bcos-framework/concepts/storage/Storage.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::sync;

// struct MockLedger
// {
//     // bcostars::Block getBlock(bcos::concepts::block::BlockNumber auto blockNumber) {}
//     bcostars::Block getBlock(int64_t blockNumber) {}

//     void setBlock(
//         bcos::concepts::storage::Storage auto& storage, bcos::concepts::block::Block auto block)
//     {}

//     bcostars::Transaction getTransactionsOrReceipts(std::ranges::range auto hashes) {}

//     void getTotalTransactionCount() {}

//     void setTransactionsOrReceipts(std::ranges::range auto inputs) {}

//     void setTransactionOrReceiptBuffers(
//         std::ranges::range auto hashes, std::ranges::range auto buffers)
//     {}
// };

// static_assert(bcos::concepts::ledger::Ledger<MockLedger>, "");