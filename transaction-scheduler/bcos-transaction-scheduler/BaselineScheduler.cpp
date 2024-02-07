#include "BaselineScheduler.h"

bcos::task::Task<std::vector<bcos::protocol::Transaction::ConstPtr>>
bcos::transaction_scheduler::getTransactions(
    txpool::TxPoolInterface& txpool, protocol::Block& block)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().GET_TRANSACTIONS);

    if (block.transactionsSize() > 0)
    {
        co_return RANGES::views::iota(0LU, block.transactionsSize()) |
            RANGES::views::transform(
                [&block](uint64_t index) { return block.transaction(index); }) |
            RANGES::to<std::vector>();
    }

    co_return co_await txpool.getTransactions(
        RANGES::iota_view<size_t, size_t>(0LU, block.transactionsMetaDataSize()) |
        RANGES::views::transform(
            [&block](uint64_t index) { return block.transactionHash(index); }));
}

bcos::h256 bcos::transaction_scheduler::calcauteTransactionRoot(
    protocol::Block const& block, crypto::Hash const& hashImpl)
{
    auto hasher = hashImpl.hasher();
    bcos::crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(hasher.clone());
    std::vector<bcos::h256> merkleTrie;
    if (block.transactionsSize() > 0)
    {
        auto hashes = RANGES::iota_view<size_t, size_t>(0LU, block.transactionsSize()) |
                      RANGES::views::transform(
                          [&block](uint64_t index) { return block.transaction(index)->hash(); });
        merkle.generateMerkle(hashes, merkleTrie);
    }
    else
    {
        auto hashes = RANGES::iota_view<size_t, size_t>(0LU, block.transactionsMetaDataSize()) |
                      RANGES::views::transform(
                          [&block](uint64_t index) { return block.transactionHash(index); });
        merkle.generateMerkle(hashes, merkleTrie);
    }

    return *RANGES::rbegin(merkleTrie);
}

std::chrono::milliseconds::rep bcos::transaction_scheduler::current()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
