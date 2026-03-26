#pragma once

#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-utilities/Exceptions.h"
#include <tbb/concurrent_unordered_map.h>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <concepts>
#include <string_view>

namespace bcos::txpool
{

DERIVE_BCOS_EXCEPTION(InvalidNonce);

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


class Web3Transactions
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

    void add(protocol::Transaction::Ptr transaction);
    void remove(SenderNonces auto senderNonces)
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

    task::Task<void> seal(int64_t limit,
        storage2::ReadWriteStorage<executor_v1::StateKeyView, executor_v1::StateValue> auto& state,
        std::output_iterator<protocol::Transaction::Ptr> auto out)
    {
        // Phase 1: collect unique senders (owned strings) under lock so we can release the lock
        // before the async storage queries below (FIB-56: no mutex held across co_await)
        std::vector<std::string> senders;
        {
            std::unique_lock lock(m_mutex);
            auto& senderIndex = m_transactions.get<2>();
            std::string_view lastSender;
            for (const auto& data : senderIndex)
            {
                if (data.sender() != lastSender)
                {
                    lastSender = data.sender();
                    senders.emplace_back(data.sender());
                }
            }
        }  // lock released before any co_await

        // Phase 2: query current on-chain nonces for each sender (no lock held)
        std::unordered_map<std::string, int64_t> nonceMap;
        for (const auto& sender : senders)
        {
            ledger::account::EVMAccount account(state, sender, m_rawAddress);
            int64_t nonce = 0;
            if (auto nonceStr = co_await account.nonce())
            {
                if (auto result = std::from_chars(
                        nonceStr->data(), nonceStr->data() + nonceStr->size(), nonce);
                    result.ec != std::errc{})
                {
                    bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
                }
            }
            nonceMap.emplace(sender, nonce);
        }

        // Phase 3: under lock, collect transactions to seal and compute nonce updates
        std::vector<std::pair<std::string, int64_t>> nonceUpdates;
        {
            int64_t count = 0;
            std::unique_lock lock(m_mutex);
            auto& senderNonceIndex = m_transactions.get<0>();
            for (const auto& sender : senders)
            {
                auto currentNonce = nonceMap[sender];
                const auto startNonce = currentNonce;
                for (auto nonceIt =
                         senderNonceIndex.lower_bound(std::make_tuple(sender, currentNonce));
                    nonceIt != senderNonceIndex.end() && nonceIt->sender() == sender &&
                    nonceIt->nonce() == currentNonce && count < limit;
                    ++nonceIt, ++currentNonce, ++count)
                {
                    *out++ = nonceIt->m_transaction;
                }
                if (currentNonce > startNonce)
                {
                    nonceUpdates.emplace_back(sender, currentNonce);
                }
                if (count >= limit)
                {
                    break;
                }
            }
        }  // lock released before any co_await

        // Phase 4: write updated nonces back to storage (no lock held)
        for (const auto& [sender, nonce] : nonceUpdates)
        {
            ledger::account::EVMAccount account(state, sender, m_rawAddress);
            co_await account.setNonce(std::to_string(nonce));
        }
    }

    task::Task<void> remove(storage2::ReadableStorage<executor_v1::StateKeyView> auto& state)
    {
        // Phase 1: collect (sender, 0) pairs under lock using owned strings (FIB-56)
        std::vector<std::pair<std::string, int64_t>> senderNonces;
        {
            std::unique_lock lock(m_mutex);
            auto& senderIndex = m_transactions.get<2>();
            std::string_view lastSender;
            for (const auto& data : senderIndex)
            {
                if (data.sender() != lastSender)
                {
                    lastSender = data.sender();
                    senderNonces.emplace_back(std::string(data.sender()), 0L);
                }
            }
        }  // lock released before any co_await

        // Phase 2: query on-chain nonces (no lock held)
        for (auto& [sender, nonce] : senderNonces)
        {
            ledger::account::EVMAccount account(state, sender, m_rawAddress);
            if (auto nonceStr = co_await account.nonce())
            {
                if (auto result = std::from_chars(
                        nonceStr->data(), nonceStr->data() + nonceStr->size(), nonce);
                    result.ec != std::errc{})
                {
                    bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(*nonceStr));
                }
            }
        }

        // Phase 3: remove transactions under lock
        std::unique_lock lock(m_mutex);
        remove(::ranges::views::all(senderNonces));
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
        remove(::ranges::views::all(senderNonceMap));
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
};

}  // namespace bcos::txpool