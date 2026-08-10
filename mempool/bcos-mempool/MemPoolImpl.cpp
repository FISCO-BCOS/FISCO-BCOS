#include "MemPoolImpl.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/exception/diagnostic_information.hpp>
#include <charconv>

#define MEMPOOL_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("MEMPOOL")

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
        auto view = m_transaction->nonce();
        // Web3 transactions store the nonce as a "0x"-prefixed hex string
        // (Web3Transaction::takeToTarsTransaction -> toQuantity, e.g. "0x1"), while
        // BCOSTransaction uses a plain decimal string. Parsing "0x1" with a decimal
        // from_chars stops at the 'x' and yields 0, so every web3 tx with nonce > 0
        // would be mis-sorted (nonce 0) and dropped by remove()/never sealed. Strip an
        // optional 0x prefix and parse the remainder as hex when prefixed, else decimal.
        bool isHex = (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'));
        std::string_view digits = isHex ? view.substr(2) : view;
        int64_t nonce = 0;
        auto [ptr, ec] = std::from_chars(digits.begin(), digits.end(), nonce, isHex ? 16 : 10);
        if (ec != std::errc{} || ptr != digits.end())
        {
            bcos::throwTrace(InvalidNonce{} << bcos::errinfo_comment(std::string{view}));
        }
        return nonce;
    }())
{}
void bcos::txpool::MemPoolImpl::add(protocol::Transaction::Ptr transaction)
{
    if (!transaction) [[unlikely]]
    {
        return;
    }

    if (transaction->tainted()) [[unlikely]]
    {
        bcos::throwTrace(InvalidTaintedTransaction{});
    }

    auto& nonceIndex = m_transactions.get<0>();
    auto& hashIndex = m_transactions.get<1>();

    bcos::crypto::HashType hash;
    try
    {
        hash = transaction->hash();
    }
    catch (std::exception const& e)
    {
        MEMPOOL_LOG(WARNING) << LOG_DESC("MemPoolImpl::add: get hash failed, skip")
                             << LOG_KV("reason", boost::diagnostic_information(e));
        return;
    }

    if (auto it = hashIndex.find(hash); it != hashIndex.end())
    {
        return;
    }

    try
    {
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
    catch (InvalidNonce const& e)
    {
        MEMPOOL_LOG(WARNING) << LOG_DESC("MemPoolImpl::add: invalid nonce, skip")
                             << LOG_KV("reason", boost::diagnostic_information(e));
    }
}