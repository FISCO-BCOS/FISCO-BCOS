#include "Web3Transactions.h"
#include "bcos-framework/txpool/TxPoolTypeDef.h"
#include <charconv>

int64_t bcos::txpool::TransactionData::importTime() const
{
    return m_transaction->importTime();
}
bcos::crypto::HashType bcos::txpool::TransactionData::hash() const
{
    return m_transaction->hash();
}
std::string_view bcos::txpool::TransactionData::sender() const
{
    return m_transaction->sender();
}
int64_t bcos::txpool::TransactionData::nonce() const
{
    return m_nonce;
}

void bcos::txpool::AccountTransactions::updateLongestSequence(OrderedIndexConstIterator updatedIt)
{
    auto& index = m_transactions.get<0>();
    if (index.size() <= 1)
    {
        m_pendingEnd = index.end();
        return;
    }

    auto it = (m_pendingEnd == index.end() || m_pendingEnd == index.begin() ||
                  updatedIt == index.end() || m_pendingEnd->nonce() > updatedIt->nonce()) ?
                  index.begin() :
                  std::prev(m_pendingEnd);
    auto nextIt = std::next(it);
    while (nextIt != index.end() && nextIt->nonce() == it->nonce() + 1)
    {
        ++it;
        ++nextIt;
    }
    m_pendingEnd = nextIt;
}

bool bcos::txpool::AccountTransactions::add(protocol::Transaction::Ptr transaction)
{
    int64_t nonce;  // NOLINT
    auto view = transaction->nonce();
    if (auto result = std::from_chars(view.begin(), view.end(), nonce); result.ec != std::errc{})
    {
        TXPOOL_LOG(WARNING) << "Transaction " << transaction->hash()
                            << " invalid web3 nonce: " << view;
        return false;
    }

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
        it = index.emplace_hint(
            it, TransactionData{.m_transaction = std::move(transaction), .m_nonce = nonce});
        updateLongestSequence(it);
    }

    return replaced;
}

void bcos::txpool::AccountTransactions::remove(int64_t lastNonce)
{
    std::unique_lock lock(m_mutex);
    auto& index = m_transactions.get<0>();
    size_t erased = 0;
    auto it = index.begin();
    for (; it != index.end() && it->nonce() <= lastNonce; it = index.erase(it))
    {
        ++erased;
    }

    if (erased != 0)
    {
        updateLongestSequence(index.end());
    }
}

void bcos::txpool::AccountTransactions::seal(
    int64_t limit, std::vector<protocol::Transaction::Ptr>& out)
{
    std::unique_lock lock(m_mutex);
    auto& index = m_transactions.get<0>();
    /*-----------------------------------------
       | 1 | 2 | 3 | 4 | 8 | 9 | 10 | 11 |
             ^           ^
          m_sealed  m_pendingEnd
       |<-   valid   ->|<-   invalid   ->|
      -----------------------------------------
      只有在pending范围内的交易才能seal，m_sealed < m_pendingEnd->nonce
      Only transactions within the pending range can be sealed, i.e., m_sealed <
      m_pendingEnd->nonce
    */
    if (limit <= 0 || m_transactions.empty() || m_pendingEnd == m_transactions.begin() ||
        (m_pendingEnd != m_transactions.end() && m_sealed >= m_pendingEnd->m_nonce))
    {
        return;
    }

    auto start = m_sealed;
    for (auto it = index.lower_bound(m_sealed); it != m_pendingEnd && m_sealed - start < limit;
        ++it)
    {
        out.emplace_back(it->m_transaction);
        ++m_sealed;
    }
}

void bcos::txpool::AccountTransactions::mark(int64_t lastNonce)
{
    std::unique_lock lock(m_mutex);
    m_sealed = lastNonce;
}

bool bcos::txpool::Web3Transactions::add(protocol::Transaction::Ptr transaction)
{
    if (auto it = m_accountTransactions.find(transaction->sender());
        it != m_accountTransactions.end())
    {
        return it->second.add(std::move(transaction));
    }
    return false;
}

void bcos::txpool::Web3Transactions::remove(std::string_view sender, int64_t lastNonce)
{
    if (auto it = m_accountTransactions.find(sender); it != m_accountTransactions.end())
    {
        it->second.remove(lastNonce);
    }
}

std::vector<bcos::protocol::Transaction::Ptr> bcos::txpool::Web3Transactions::seal(int64_t limit)
{
    std::vector<protocol::Transaction::Ptr> sealedTransactions;
    for (auto& it : m_accountTransactions)
    {
        it.second.seal(limit, sealedTransactions);
    }
    return sealedTransactions;
}

void bcos::txpool::Web3Transactions::mark(std::string_view sender, int64_t lastNonce)
{
    if (auto it = m_accountTransactions.find(sender); it != m_accountTransactions.end())
    {
        it->second.mark(lastNonce);
    }
}