#include "bcos-framework/protocol/Transaction.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

#if !ONLY_CPP_SDK
#include <bcos-utilities/ITTAPI.h>
#endif

namespace bcos::protocol
{
Web3AccessList Transaction::web3AccessList() const
{
    return {};
}

AuthorizationList Transaction::authorizationList() const
{
    return {};
}

VersionedHashes Transaction::blobVersionedHashes() const
{
    return {};
}

std::optional<u256> Transaction::maxFeePerBlobGas() const
{
    return std::nullopt;
}

Transaction::Transaction(const Transaction& other)
  : m_submitCallback(other.m_submitCallback),
    m_batchHash(other.m_batchHash),
    m_batchId(other.m_batchId),
    m_synced(other.m_synced),
    m_sealed(other.m_sealed),
    m_invalid(other.m_invalid),
    m_systemTx(other.m_systemTx.load(std::memory_order_acquire)),
    m_tainted(other.m_tainted),
    m_storeToBackend(other.m_storeToBackend)
{}

Transaction::Transaction(Transaction&& other) noexcept
  : m_submitCallback(std::move(other.m_submitCallback)),
    m_batchHash(other.m_batchHash),
    m_batchId(other.m_batchId),
    m_synced(other.m_synced),
    m_sealed(other.m_sealed),
    m_invalid(other.m_invalid),
    m_systemTx(other.m_systemTx.load(std::memory_order_acquire)),
    m_tainted(other.m_tainted),
    m_storeToBackend(other.m_storeToBackend)
{}

Transaction& Transaction::operator=(const Transaction& other)
{
    if (this != &other)
    {
        m_submitCallback = other.m_submitCallback;
        m_batchHash = other.m_batchHash;
        m_batchId = other.m_batchId;
        m_synced = other.m_synced;
        m_sealed = other.m_sealed;
        m_invalid = other.m_invalid;
        m_systemTx.store(
            other.m_systemTx.load(std::memory_order_acquire), std::memory_order_release);
        m_tainted = other.m_tainted;
        m_storeToBackend = other.m_storeToBackend;
    }
    return *this;
}

Transaction& Transaction::operator=(Transaction&& other) noexcept
{
    if (this != &other)
    {
        m_submitCallback = std::move(other.m_submitCallback);
        m_batchHash = other.m_batchHash;
        m_batchId = other.m_batchId;
        m_synced = other.m_synced;
        m_sealed = other.m_sealed;
        m_invalid = other.m_invalid;
        m_systemTx.store(
            other.m_systemTx.load(std::memory_order_acquire), std::memory_order_release);
        m_tainted = other.m_tainted;
        m_storeToBackend = other.m_storeToBackend;
    }
    return *this;
}

void Transaction::verify(crypto::Hash& hashImpl, crypto::SignatureCrypto& signatureImpl)
{
#if !ONLY_CPP_SDK
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().TRANSACTION,
        ittapi::ITT_DOMAINS::instance().VERIFY_TRANSACTION);
#endif
    if (!tainted())
    {
        return;
    }

    crypto::HashType hashResult;
    if (type() == static_cast<uint8_t>(TransactionType::BCOSTransaction))
    {
        calculateHash(hashImpl);
        hashResult = hash();
    }
    else if (type() == static_cast<uint8_t>(TransactionType::Web3Transaction))
    {
        // Recompute and store the canonical txHash from the signed payload. The computation lives
        // in the tars layer (calculateHash -> hash::calculate), so this framework core stays free
        // of RLP/codec. Callers clear extraTransactionHash before verify() (clearSenderAndHash),
        // so a wire-supplied value from an untrusted peer is never believed (FIB-New1).
        calculateHash(hashImpl);
        // Recover uses the EIP signing hash keccak256(preimage), which differs from the canonical
        // txHash stored above.
        hashResult = bcos::crypto::keccak256Hash(extraTransactionBytes());
    }

    auto const signature = signatureData();
    // Defense-in-depth BEFORE recoverAddress: an explicitly-sized check gives a precise error
    // (recoverAddress's checkSigLen would throw a generic InvalidSignature for the same input).
    // All legitimate Web3 signatures are exactly 65 bytes (r||s||v, assembled by
    // takeToTarsTransaction); a different length means malformed tars deserialization or a
    // hostile peer. Deposits never legitimately reach verify() — admission rejects 0x7e and
    // the engine path decodes via decodeDepositEnvelope — so an empty/short signature here is
    // treated as malformed (fail-closed), never as an unsigned deposit.
    if (type() == static_cast<uint8_t>(TransactionType::Web3Transaction) && signature.size() != 65)
        [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("EIP-2: Web3 signature must be exactly 65 bytes (v, r, s)"));
    }
    // The EIP-2 r/s range and v-domain checks are alloc-free and need only the signature bytes,
    // so they run BEFORE the EC recovery: hostile peers pay nothing per rejected signature
    // (same ordering as the RPC decode funnel).
    if (type() == static_cast<uint8_t>(TransactionType::Web3Transaction))
    {
        auto const r = bcos::fromBigEndian<u256>(signature.getCroppedData(0, 32));
        auto const s = bcos::fromBigEndian<u256>(signature.getCroppedData(32, 32));
        if (r == 0 || r >= bcos::crypto::c_secp256k1n || s == 0 ||
            s > bcos::crypto::c_secp256k1nOver2) [[unlikely]]
        {
            // n is odd, so n/2 = (n-1)/2; s > (n-1)/2 is equivalent to s >= ceil(n/2),
            // matching op-geth's secp256k1.IsCanonical check.
            BOOST_THROW_EXCEPTION(std::invalid_argument(
                "EIP-2: invalid signature (r out of [1,n-1] or s exceeds secp256k1n/2)"));
        }
        // The tars signature byte is the recovery id / yParity — always 0/1 by construction:
        // LegacyTxHandler::decode normalizes legacy v (27/28 -> v-27, EIP-155 -> (v-35)%2) and
        // the typed handlers store y_parity; secp256k1Sign stores the raw recid (0/1; 2/3 only
        // in the rare r < p - n corner where x0 = r + n is the recovered x — such r passes the
        // r < n check above, so the value needs its own gate). A signature byte > 1 (recid 2/3,
        // the v=29/30 envelope form) is never produced by the RPC funnel; op-geth rejects the
        // same in decode and Sender(). Applies to typed AND legacy envelopes — the
        // envelope-vs-preimage distinction lives in extraTransactionBytes, not in this byte.
        auto const& env = extraTransactionBytes();
        if (!env.empty() && signature[64] > 1) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument(
                "EIP-2718/EIP-155: recovery id exceeds 1 in transaction signature"));
        }
    }
    auto [recovered, sender] = signatureImpl.recoverAddress(hashImpl, hashResult, signature);
    if (!recovered) [[unlikely]]
    {
        BCOS_LOG(INFO) << LOG_DESC("recover sender address failed")
                       << LOG_KV("hash", hashResult.abridged());
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("recover sender address from signature failed"));
    }

    forceSender(sender);
    setTainted(false);
}

std::ostream& operator<<(std::ostream& stream, const Transaction& transaction)
{
    stream << "Transaction{"
           << "hash=" << transaction.hash() << ", "
           << "version=" << transaction.version() << ", "
           << "chainId=" << transaction.chainId() << ", "
           << "groupId=" << transaction.groupId() << ", "
           << "blockLimit=" << transaction.blockLimit() << ", "
           << "nonce=" << transaction.nonce() << ", "
           << "to=" << transaction.to() << ", "
           << "abi=" << transaction.abi() << ", "
           << "value=" << transaction.value() << ", "
           << "gasPrice=" << transaction.gasPrice().value_or(0) << ", "
           << "gasLimit=" << transaction.gasLimit() << ", "
           << "maxFeePerGas=" << transaction.maxFeePerGas().value_or(0) << ", "
           << "maxPriorityFeePerGas=" << transaction.maxPriorityFeePerGas().value_or(0) << ", "
           << "extension=" << toHex(transaction.extension()) << ", "
           << "extraData=" << transaction.extraData() << ", "
           << "sender=" <<
        [&]() {
            auto view = transaction.sender();
            return bcos::bytesConstRef{
                reinterpret_cast<const bcos::byte*>(view.data()), view.size()};
        }() << ", "
           << "input=" << toHex(transaction.input()) << ", "
           << "importTime=" << transaction.importTime() << ", "
           << "type=" << static_cast<int>(transaction.type()) << ", "
           << "attribute=" << transaction.attribute() << ", "
           << "size=" << transaction.size() << "}";
    return stream;
}

u256 effectiveGasPrice(Transaction const& tx)
{
    if (auto price = tx.gasPrice(); price.has_value() && *price > 0)
    {
        return *price;
    }
    return tx.maxFeePerGas().value_or(0);
}
}  // namespace bcos::protocol