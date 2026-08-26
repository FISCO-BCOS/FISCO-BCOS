#include "BaselineScheduler.h"
#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-utilities/ITTAPI.h"
#include <boost/exception/diagnostic_information.hpp>
#include <range/v3/iterator/operations.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/transform.hpp>
#include <chrono>
#include <type_traits>

bcos::task::Task<std::vector<bcos::protocol::Transaction::ConstPtr>>
bcos::scheduler_v1::getTransactions(txpool::TxPoolInterface& txpool, protocol::Block& block)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().GET_TRANSACTIONS);

    if (block.transactionsSize() > 0)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return bcos::protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    co_return co_await txpool.getTransactions(block.transactionHashes());
}

bcos::h256 bcos::scheduler_v1::calculateTransactionRoot(
    protocol::Block const& block, crypto::Hash const& hashImpl)
{
    auto hasher = hashImpl.hasher();
    bcos::crypto::merkle::Merkle<std::remove_reference_t<decltype(hasher)>> merkle(hasher.clone());

    if (block.transactionsSize() == 0 && block.transactionsMetaDataSize() == 0)
    {
        return {};
    }

    std::vector<bcos::h256> merkleTrie;
    try
    {
        if (block.transactionsSize() > 0)
        {
            auto hashes = ::ranges::views::transform(
                block.transactions(), [](auto tx) { return tx->hash(); });
            merkle.generateMerkle(hashes, merkleTrie);
        }
        else
        {
            auto hashes = block.transactionHashes();
            merkle.generateMerkle(::ranges::views::all(hashes), merkleTrie);
        }
    }
    catch (std::exception const& e)
    {
        BASELINE_SCHEDULER_LOG(WARNING)
            << "calculateTransactionRoot failed: " << boost::diagnostic_information(e);
        return {};
    }

    if (merkleTrie.empty())
    {
        return {};
    }

    return *::ranges::rbegin(merkleTrie);
}

bcos::h256 bcos::scheduler_v1::calculateEthereumTransactionRoot(protocol::Block const& block)
{
    // Commit to the tx trie over each transaction's full EIP-2718 wire bytes (the canonical
    // tx trie value). reassembleWeb3RawTransaction throws std::invalid_argument for a
    // non-web3 payload or a non-65-byte signature — on a v2 (Ethereum-executor) chain those
    // are malformed by construction, so failing loudly beats a silently wrong txsRoot.
    std::vector<bcos::bytes> txRlps;
    txRlps.reserve(block.transactionsSize());
    for (auto const& tx : block.transactions())
    {
        txRlps.push_back(bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData()));
    }
    std::vector<bcos::bytesConstRef> refs;
    refs.reserve(txRlps.size());
    for (auto const& rlp : txRlps)
    {
        refs.emplace_back(bcos::ref(rlp));
    }
    return ledger::mpt::calculateTransactionsRoot(refs);
}

std::chrono::milliseconds::rep bcos::scheduler_v1::current()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
