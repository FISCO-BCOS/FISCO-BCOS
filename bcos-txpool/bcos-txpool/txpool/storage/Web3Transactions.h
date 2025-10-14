#pragma once

#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
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

    int64_t importTime() const;
    crypto::HashType hash() const;
    std::string_view sender() const;
    int64_t nonce() const;
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
    Transactions::nth_index<0>::type::const_iterator m_pendingEnd = m_transactions.begin();
    int64_t m_sealed = 0;
    std::mutex m_mutex;

    void updateLastPending();

public:
    bool add(protocol::Transaction::Ptr transaction);
    void remove(int64_t lastNonce);
    void seal(int64_t limit, std::vector<protocol::Transaction::Ptr>& out);
    void mark(int64_t lastNonce);
};


class Web3Transactions
{
private:
    struct StringHash
    {
        struct StringEqual
        {
            using is_transparent = void;
            template <std::convertible_to<std::string_view> T,
                std::convertible_to<std::string_view> U>
            bool operator()(const T& lhs, const U& rhs) const
            {
                return lhs == rhs;
            }
        };
        using transparent_key_equal = StringEqual;

        template <std::convertible_to<std::string_view> T>
        std::size_t operator()(const T& str) const
        {
            return std::hash<T>{}(str);
        }
    };

    tbb::concurrent_unordered_map<std::string, AccountTransactions, StringHash>
        m_accountTransactions;

public:
    bool add(protocol::Transaction::Ptr transaction);
    void remove(std::string_view sender, int64_t lastNonce);
    std::vector<protocol::Transaction::Ptr> seal(int64_t limit);
    void mark(std::string_view sender, int64_t lastNonce);
};

}  // namespace bcos::txpool