#pragma once

#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include <tbb/concurrent_unordered_map.h>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <string_view>

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

class Web3Transactions
{
private:
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
            boost::multi_index::sequenced<>>>;

    Transactions m_transactions;
    std::mutex m_mutex;

public:
    bool add(protocol::Transaction::Ptr transaction);
    void seal(std::string_view sender, int64_t startNonce, int64_t limit,
        std::vector<protocol::Transaction::Ptr>& out);
};

}  // namespace bcos::txpool