#include "Web3Transactions.h"
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
bcos::txpool::TransactionData::TransactionData(protocol::Transaction::Ptr transaction)
  : m_transaction(std::move(transaction)), m_nonce([&]() {
        int64_t nonce;  // NOLINT(cppcoreguidelines-init-variables) - initialized by
                        // std::from_chars
        auto view = m_transaction->nonce();
        if (auto result = std::from_chars(view.begin(), view.end(), nonce);
            result.ec != std::errc{})
        {
            bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(std::string{view}));
        }
        return nonce;
    }())
{}
void bcos::txpool::Web3Transactions::add(protocol::Transaction::Ptr transaction)
{
    auto& nonceIndex = m_transactions.get<0>();
    auto& hashIndex = m_transactions.get<1>();

    auto hash = transaction->hash();
    if (auto it = hashIndex.find(hash); it != hashIndex.end())
    {
        // Duplicate transaction
        return;
    }

    TransactionData transactionData{std::move(transaction)};
    if (auto it = nonceIndex.lower_bound(
            std::make_tuple(transactionData.sender(), transactionData.nonce()));
        it != nonceIndex.end() && it->sender() == transactionData.sender() &&
        it->nonce() == transactionData.nonce())
    {
        nonceIndex.replace(it, std::move(transactionData));
    }
    else
    {
        nonceIndex.emplace_hint(it, std::move(transactionData));
    }
}