#include "MemPoolImpl.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-crypto/kzg/Kzg4844.h"
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
/// The shared body of add() and tryAdd(). The caller holds m_mutex; the lookup and the insert
/// are one step under it, which is what makes tryAdd's reservation atomic.
///
/// @param transaction non-null, checked by both entries -- they answer a null differently.
bcos::protocol::TransactionStatus bcos::txpool::MemPoolImpl::insertLocked(
    protocol::Transaction::Ptr transaction, OnTakenNonce onTakenNonce,
    std::optional<engine::BlobTxSidecar> sidecar)
{
    using bcos::protocol::TransactionStatus;

    if (transaction->tainted()) [[unlikely]]
    {
        bcos::throwTrace(InvalidTaintedTransaction{});
    }

    // L2 never admits blob (type-3) transactions (OP Stack, Ecotone onwards). The RPC entry
    // rejects them before decoding; this is the second gate for in-process callers. For Web3
    // transactions the signing payload (extraTransactionBytes) starts with the same EIP-2718
    // type byte as the raw envelope, so the shared dispatch table applies. On L1 blob
    // transactions are the point of the fee market, so the gate becomes a shape check
    // (TxValidator does the full validation upstream; this gate keeps in-process callers
    // honest).
    if (transaction->type() ==
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction) &&
        bcos::engine::dispatchRawTransaction(transaction->extraTransactionBytes()) ==
            bcos::engine::RawTransactionKind::Blob) [[unlikely]]
    {
        if (m_config.chainKind == ChainKind::L2)
        {
            bcos::throwTrace(InvalidBlobTransaction{});
        }
        auto const blobCount = transaction->blobVersionedHashes().size();
        if (blobCount == 0 || blobCount > m_config.maxBlobsPerTransaction)
        {
            MEMPOOL_LOG(WARNING) << LOG_DESC("MemPoolImpl: invalid blob transaction, skip")
                                 << LOG_KV("blobCount", blobCount)
                                 << LOG_KV("maxBlobsPerTransaction",
                                        m_config.maxBlobsPerTransaction);
            return TransactionStatus::Malformed;
        }
    }

    evictExpiredLocked();

    auto& nonceIndex = m_transactions.get<0>();
    auto& hashIndex = m_transactions.get<1>();

    bcos::crypto::HashType hash;
    try
    {
        hash = transaction->hash();
    }
    catch (std::exception const& e)
    {
        // A transaction whose hash cannot be computed cannot be stored under one, and a caller
        // has nothing to poll for.
        MEMPOOL_LOG(WARNING) << LOG_DESC("MemPoolImpl: get hash failed, skip")
                             << LOG_KV("reason", boost::diagnostic_information(e));
        return TransactionStatus::Malformed;
    }

    if (hashIndex.find(hash) != hashIndex.end())
    {
        return TransactionStatus::AlreadyInTxPool;
    }

    // Stamp the admission time so the lifetime eviction has something to compare; a
    // caller-set importTime (the other pool stamps at admission too) is kept.
    if (transaction->importTime() <= 0)
    {
        transaction->setImportTime(static_cast<int64_t>(utcTime()));
    }

    try
    {
        TransactionData transactionData{std::move(transaction)};
        auto const position = nonceIndex.lower_bound(
            std::make_tuple(transactionData.sender(), transactionData.nonce()));
        if (position != nonceIndex.end() && position->sender() == transactionData.sender() &&
            position->nonce() == transactionData.nonce())
        {
            if (onTakenNonce == OnTakenNonce::Refuse)
            {
                return TransactionStatus::NonceCheckFail;
            }
            // On L1 a taken (sender, nonce) is a fee-market decision: the incoming
            // transaction must outbid the stored one by the EIP-1559 price bump, or the
            // slot stays with the stored transaction (geth's ErrUnderpriced; the pool
            // has no underpriced-specific status, so it reports AlreadyInTxPool).
            if (m_config.chainKind == ChainKind::L1 &&
                !bumpsFee(*position->m_transaction, *transactionData.m_transaction))
            {
                return TransactionStatus::AlreadyInTxPool;
            }
            // The replaced transaction's sidecar goes with it; the incoming transaction's
            // (when it carries one) is registered below.
            m_blobSidecars.erase(position->hash());
            nonceIndex.replace(position, std::move(transactionData));
            if (sidecar.has_value())
            {
                m_blobSidecars.emplace(hash, std::move(*sidecar));
            }
            return TransactionStatus::None;
        }
        nonceIndex.emplace_hint(position, std::move(transactionData));
        if (sidecar.has_value())
        {
            m_blobSidecars.emplace(hash, std::move(*sidecar));
        }
        evictOverCapacityLocked();
    }
    catch (InvalidNonce const& e)
    {
        // TransactionData's constructor parses the nonce; one it cannot read is not a nonce this
        // pool can order by.
        MEMPOOL_LOG(WARNING) << LOG_DESC("MemPoolImpl: invalid nonce, skip")
                             << LOG_KV("reason", boost::diagnostic_information(e));
        return TransactionStatus::NonceCheckFail;
    }
    return TransactionStatus::None;
}

bcos::protocol::TransactionStatus bcos::txpool::MemPoolImpl::tryAdd(
    protocol::Transaction::Ptr transaction, std::optional<engine::BlobTxSidecar> sidecar)
{
    if (!transaction) [[unlikely]]
    {
        bcos::throwTrace(NullTransaction{});
    }
    std::unique_lock lock(m_mutex);
    return insertLocked(std::move(transaction), OnTakenNonce::Refuse, std::move(sidecar));
}

std::vector<std::optional<bcos::engine::BlobItem>>
bcos::txpool::MemPoolImpl::blobsByVersionedHashes(
    std::span<const crypto::HashType> versionedHashes) const
{
    std::vector<std::optional<engine::BlobItem>> out(versionedHashes.size());
    std::unique_lock lock(m_mutex);
    std::size_t missing = versionedHashes.size();
    for (auto const& [txHash, sidecar] : m_blobSidecars)
    {
        (void)txHash;
        for (std::size_t i = 0; i < sidecar.commitments.size(); ++i)
        {
            auto const versionedHash = crypto::kzg::versionedHashFromCommitment(
                bcos::ref(sidecar.commitments[i]));
            for (std::size_t j = 0; j < versionedHashes.size(); ++j)
            {
                if (!out[j].has_value() && versionedHashes[j] == versionedHash)
                {
                    out[j] = engine::BlobItem{.commitment = sidecar.commitments[i],
                        .proof = sidecar.proofs[i],
                        .blob = sidecar.blobs[i]};
                    if (--missing == 0)
                    {
                        return out;
                    }
                }
            }
        }
    }
    return out;
}

void bcos::txpool::MemPoolImpl::add(protocol::Transaction::Ptr transaction)
{
    if (!transaction) [[unlikely]]
    {
        return;
    }
    // The status is what tryAdd() exists to report; this entry drops it, which is the whole of
    // the difference between them once the taken-nonce policy is a parameter.
    (void)insertLocked(std::move(transaction), OnTakenNonce::Replace, std::nullopt);
}
