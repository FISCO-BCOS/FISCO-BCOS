#pragma once

#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <algorithm>
#include <concepts>
#include <string_view>
#include <unordered_set>

namespace bcos::txpool
{

DERIVE_BCOS_EXCEPTION(InvalidNonce);
DERIVE_BCOS_EXCEPTION(InvalidTaintedTransaction);

struct TransactionData
{
    protocol::Transaction::Ptr m_transaction;
    int64_t m_nonce;

    int64_t importTime() const;
    crypto::HashType hash() const;
    std::string_view sender() const;
    int64_t nonce() const;

    TransactionData(protocol::Transaction::Ptr transaction);
};

template <class TransactionsType>
concept InputTransactions =
    ::ranges::input_range<TransactionsType> &&
    std::same_as<::ranges::range_value_t<TransactionsType>, protocol::Transaction::Ptr>;

template <class InputHashesType>
concept InputHashes =
    ::ranges::input_range<InputHashesType> &&
    std::same_as<::ranges::range_value_t<InputHashesType>, bcos::crypto::HashType>;

template <class SenderNonceTuple>
concept SenderNonce = requires(SenderNonceTuple senderNonce) {
    { std::get<0>(senderNonce) } -> std::convertible_to<std::string_view>;
    { std::get<1>(senderNonce) } -> std::convertible_to<int64_t>;
};


template <class SenderNoncesType>
concept SenderNonces = ::ranges::input_range<SenderNoncesType> &&
                       SenderNonce<::ranges::range_value_t<SenderNoncesType>>;


class MemPoolImpl
{
private:
    // Transactions: A boost::multi_index_container that maintains multiple indexes over
    // TransactionData for different query patterns and ordering policies.
    //
    // 该容器为交易缓存的多索引结构，针对不同访问/遍历需求建立多个索引，便于快速按哈希查找、
    // 按账户与 nonce 顺序扫描，以及按发送者聚合遍历，同时保留插入顺序。
    //
    // Index layout 索引布局（get<N>() 对应关系）：
    //   0 -> SenderNonceIndex（ordered_unique by (sender, nonce)）
    //        - 保证同一 sender 下 nonce 的唯一性与有序性；
    //        - 便于按照 (sender, currentNonce..) 连续扫描，用于 seal / remove等流程；
    //   1 -> HashIndex（hashed_unique by tx hash）
    //        - 按交易哈希 O(1) 近似查找/去重，用于交易按 hash 去重；
    //   2 -> SenderIndex（hashed_non_unique by sender）
    //        - 快速按发送者分组遍历（一个 sender 对应多笔交易），用于按 sender 遍历；
    //   3 -> SequenceIndex（sequenced）
    //        - 维护插入顺序（FIFO），便于基于时间/先来先服务的策略，用于超时淘汰；
    //
    // Notes:
    // - ordered_unique composite key uses (sender, nonce) to avoid duplicates and keep
    //   per-sender nonce strictly increasing when scanning.
    // - hashed_unique by hash prevents duplicate transactions with the same hash.
    // - hashed_non_unique by sender supports grouping operations across all txs of a sender.
    // - sequenced keeps push order; useful for strategies relying on arrival order.
    //
    // 注意：本结构仅添加索引与顺序语义，不改变交易对象本身；增删改查均通过对应索引视图完成。
    using Transactions = boost::multi_index_container<TransactionData,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::composite_key<TransactionData,
                    boost::multi_index::const_mem_fun<TransactionData, std::string_view,
                        &TransactionData::sender>,
                    boost::multi_index::const_mem_fun<TransactionData, int64_t,
                        &TransactionData::nonce>>,
                boost::multi_index::composite_key_compare<std::less<>, std::less<>>>,
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<TransactionData,
                bcos::crypto::HashType, &TransactionData::hash>>,
            boost::multi_index::hashed_non_unique<boost::multi_index::const_mem_fun<TransactionData,
                std::string_view, &TransactionData::sender>>,
            boost::multi_index::sequenced<>>>;

    Transactions m_transactions;
    std::mutex m_mutex;
    bool m_rawAddress{};

    /// The mempool stores the sender as raw address bytes (TransactionImpl::sender());
    /// convert them to an evmc_address so EVMAccount resolves the same lower-case hex
    /// account path (/apps/<hex>) the executor writes and reads.
    static evmc_address senderToAddress(std::string_view sender)
    {
        evmc_address addr{};
        if (sender.size() >= sizeof(addr.bytes))
        {
            std::copy_n(sender.begin(), sizeof(addr.bytes), addr.bytes);
        }
        return addr;
    }

    void add(protocol::Transaction::Ptr transaction);
    void removeBySenderNonces(SenderNonces auto senderNonces)
    {
        auto& senderNonceIndex = m_transactions.get<0>();

        for (auto&& [sender, nonce] : senderNonces)
        {
            auto start = senderNonceIndex.lower_bound(std::make_tuple(sender, 0));
            auto end = senderNonceIndex.upper_bound(std::make_tuple(sender, nonce));
            for (auto it = start; it != end;)
            {
                it = senderNonceIndex.erase(it);
            }
        }
    }

public:
    void add(InputTransactions auto transactions)
    {
        std::unique_lock lock(m_mutex);
        for (auto&& transaction : transactions)
        {
            add(std::forward<decltype(transaction)>(transaction));
        }
    }

    void seal(int64_t limit,
        storage2::ReadWriteStorage<executor_v1::StateKeyView, executor_v1::StateValue> auto& state,
        std::output_iterator<protocol::Transaction::Ptr> auto out)
    {
        int64_t count = 0;
        std::unique_lock lock(m_mutex);
        auto& senderNonceIndex = m_transactions.get<0>();
        auto& senderIndex = m_transactions.get<2>();
        // senderIndex is hashed_non_unique: a sender appears once per transaction, so the same
        // sender is visited multiple times while iterating. Track the senders whose gapless
        // prefix has already been sealed to avoid re-sealing the same transactions on the
        // subsequent entries of that sender. (The legacy implementation achieved this by
        // writing the advanced nonce back into `state`; keeping seal() read-only with respect
        // to `state` requires the dedup to live here instead.)
        std::unordered_set<std::string_view> sealedSenders;
        for (const auto& data : senderIndex)
        {
            auto sender = data.sender();
            if (!sealedSenders.emplace(sender).second)
            {
                continue;
            }
            // The mempool stores the sender as raw address bytes (forceSender), while the
            // executor persists accounts under the lower-case hex path (/apps/<hex>) via the
            // evmc_address EVMAccount overload. Passing the raw bytes through the string_view
            // overload would treat them as a hex string and compute a wrong table path, so the
            // nonce read below would miss the account entirely. Build an evmc_address instead
            // so the same hex path is used as the executor.
            ledger::account::EVMAccount account(state, senderToAddress(sender), m_rawAddress);

            int64_t currentNonce = 0;
            if (auto nonceStr = task::syncWait(account.nonce()))
            {
                if (auto result = std::from_chars(
                        nonceStr->data(), nonceStr->data() + nonceStr->size(), currentNonce);
                    result.ec != std::errc{})
                {
                    bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
                }
            }
            // seal() is read-only with respect to `state`: it only reads the sender's current
            // nonce to pick the executable (gapless) prefix in nonce order, and never writes the
            // advanced nonce back. The authoritative nonce advance happens during execution
            // itself, so writing it here would cause the executor (evmone) to reject every
            // just-sealed transaction with NONCE_TOO_LOW. This matches how geth's legacypool
            // (in-memory noncer) and reth's best_transactions() select block transactions
            // without touching state.
            for (auto nonceIt = senderNonceIndex.lower_bound(std::make_tuple(sender, currentNonce));
                nonceIt != senderNonceIndex.end() && nonceIt->sender() == sender &&
                nonceIt->nonce() == currentNonce;
                ++nonceIt)
            {
                ++currentNonce;
                ++count;
                *out++ = nonceIt->m_transaction;

                if (count >= limit)
                {
                    break;
                }
            }
            if (count >= limit)
            {
                break;
            }
        }
    }

    void remove(storage2::ReadableStorage<executor_v1::StateKeyView> auto& state)
    {
        std::unique_lock lock(m_mutex);
        auto& senderIndex = m_transactions.get<2>();
        auto& senderNonceIndex = m_transactions.get<0>();

        for (auto it = senderIndex.begin(); it != senderIndex.end();)
        {
            auto sender = it->sender();
            auto nextIt = senderIndex.equal_range(sender).second;
            // Same hex-path note as in seal(): the raw sender bytes must go through the
            // evmc_address overload so the account nonce read finds the executor's account.
            ledger::account::EVMAccount account(state, senderToAddress(sender), m_rawAddress);
            if (auto nonceStr = task::syncWait(account.nonce()))
            {
                int64_t nonce = 0;
                if (auto result = std::from_chars(
                        nonceStr->data(), nonceStr->data() + nonceStr->size(), nonce);
                    result.ec != std::errc{})
                {
                    bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
                }

                if (nonce > 0)
                {
                    auto start = senderNonceIndex.lower_bound(std::make_tuple(sender, 0));
                    auto end = senderNonceIndex.upper_bound(std::make_tuple(sender, nonce - 1));
                    senderNonceIndex.erase(start, end);
                }
            }

            it = nextIt;
        }
    }

    void remove(InputHashes auto hashes)
    {
        std::unordered_map<std::string_view, int64_t> senderNonceMap;
        std::unique_lock lock(m_mutex);
        auto& hashIndex = m_transactions.get<1>();
        for (const auto& hash : hashes)
        {
            if (auto it = hashIndex.find(hash); it != hashIndex.end())
            {
                if (auto nonceIt = senderNonceMap.find(it->sender());
                    nonceIt != senderNonceMap.end())
                {
                    nonceIt->second = std::max(it->nonce(), nonceIt->second);
                }
                else
                {
                    senderNonceMap.emplace(it->sender(), it->nonce());
                }
            }
        }
        removeBySenderNonces(::ranges::views::all(senderNonceMap));
    }

    template <InputHashes TransactionHashes>
    std::vector<protocol::Transaction::Ptr> get(TransactionHashes hashes)
    {
        std::vector<protocol::Transaction::Ptr> transactions;
        if constexpr (::ranges::sized_range<TransactionHashes>)
        {
            transactions.reserve(hashes.size());
        }
        std::unique_lock lock(m_mutex);
        auto& hashIndex = m_transactions.get<1>();
        for (const auto& hash : hashes)
        {
            if (auto it = hashIndex.find(hash); it != hashIndex.end())
            {
                transactions.emplace_back(it->m_transaction);
            }
            else
            {
                transactions.emplace_back();
            }
        }
        return transactions;
    }

    /// Drain up to @p limit pending transactions from the pool (in (sender, nonce) order) and
    /// clear them. A negative @p limit means unlimited.
    ///
    /// Used by the single-node consensus driver to assemble the next block proposal. Unlike
    /// seal(), no nonce-vs-state check is performed: the driver hands the txs directly to the
    /// scheduler, so there is no state view to validate against here — and the driver accepts
    /// future-nonce (gapped) transactions on purpose, letting execution fail them instead of
    /// holding them back. @p limit restores the block-size bound seal() enforces, so an
    /// unbounded submission rate cannot grow a single proposal without limit.
    std::vector<protocol::Transaction::Ptr> takeAll(int64_t limit = -1)
    {
        std::vector<protocol::Transaction::Ptr> transactions;
        std::unique_lock lock(m_mutex);
        auto& senderNonceIndex = m_transactions.get<0>();
        transactions.reserve(limit < 0 ? m_transactions.size() :
                                         std::min<size_t>(m_transactions.size(), limit));
        int64_t count = 0;
        for (auto it = senderNonceIndex.begin(); it != senderNonceIndex.end();)
        {
            transactions.push_back(it->m_transaction);
            it = senderNonceIndex.erase(it);
            ++count;
            if (limit >= 0 && count >= limit)
            {
                break;
            }
        }
        return transactions;
    }
};

}  // namespace bcos::txpool