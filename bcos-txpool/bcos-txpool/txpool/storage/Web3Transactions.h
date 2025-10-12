#pragma once

#include "bcos-framework/protocol/Block.h"
#include <bcos-framework/bcos-framework/protocol/Transaction.h>
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <string>

namespace bcos::txpool
{

struct TransactionData
{
    protocol::Transaction::Ptr m_transaction;
    int64_t m_nonce;

    int64_t importTime() const { return m_transaction->importTime(); }
    crypto::HashType hash() const { return m_transaction->hash(); }
    std::string_view sender() const { return m_transaction->sender(); }
    int64_t nonce() const { return m_nonce; }
};

class AccountTransactions
{
private:
    using Transactions = boost::multi_index_container<TransactionData,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<boost::multi_index::const_mem_fun<TransactionData,
                                                   int64_t, &TransactionData::nonce>,
                std::less<>>,
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<TransactionData,
                bcos::crypto::HashType, &TransactionData::hash>>,
            boost::multi_index::sequenced<>>>;

    Transactions m_transactions;
    Transactions::const_iterator m_pendingEnd = m_transactions.end();
    Transactions::const_iterator m_sealedEnd = m_transactions.end();
    std::mutex m_mutex;

    void updateLastPending()
    {
        auto it = m_pendingEnd == m_transactions.end() ? m_transactions.begin() : m_pendingEnd;
        while (it != m_transactions.end())
        {
            auto nextIterator = std::next(it);
            if (nextIterator == m_transactions.end() || nextIterator->nonce() != it->nonce() + 1)
            {
                it = nextIterator;
                break;
            }
            it = nextIterator;
        }
        m_pendingEnd = it;
    }

public:
    bool add(protocol::Transaction::Ptr transaction)
    {
        auto nonce = boost::lexical_cast<int64_t>(transaction->nonce());

        std::unique_lock lock(m_mutex);
        auto& index = m_transactions.get<0>();
        auto it = index.lower_bound(nonce);

        bool replaced = false;
        if (it != index.end() && it->nonce() == nonce)
        {
            replaced = true;
            index.replace(
                it, TransactionData{.m_transaction = std::move(transaction), .m_nonce = nonce});
        }
        else
        {
            index.emplace_hint(
                it, TransactionData{.m_transaction = std::move(transaction), .m_nonce = nonce});
        }

        updateLastPending();
        return replaced;
    }

    void remove(protocol::ViewResult<crypto::HashType> hashes)
    {
        std::unique_lock lock(m_mutex);
        auto& index = m_transactions.get<1>();
        for (auto const& hash : hashes)
        {
            index.erase(hash);
        }

        m_pendingEnd = m_transactions.end();
        m_sealedEnd = m_transactions.end();
        updateLastPending();
    }

    std::vector<protocol::Transaction::Ptr> seal(int64_t limit)
    {
        std::vector<protocol::Transaction::Ptr> sealedTransactions;
        std::unique_lock lock(m_mutex);
        auto& index = m_transactions.get<0>();

        auto it = m_sealedEnd == m_transactions.end() ? m_transactions.begin() : m_sealedEnd;
        auto start = it;
        for (; it != m_pendingEnd && std::distance(start, it) <= limit; ++it)
        {
            sealedTransactions.push_back(it->m_transaction);
        }
        m_sealedEnd = it;
        return sealedTransactions;
    }
};

class Web3Transactions
{
private:
    tbb::concurrent_unordered_map<std::string, AccountTransactions> m_accountTransactions;
};

}  // namespace bcos::txpool