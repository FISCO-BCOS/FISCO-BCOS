#include "bcos-framework/protocol/Transaction.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

#if !ONLY_CPP_SDK
#include <bcos-utilities/ITTAPI.h>
#endif

namespace bcos::protocol
{
namespace
{
// EIP-2 canonical-s guard (same constants as the RPC-side check in Web3Transaction.cpp):
// a signature with s > n/2 is malleable — flipping s to n-s recovers the same sender and
// executes identically, so op-geth/op-reth reject it at BOTH admission and block processing.
// The RPC path (Web3Transaction::decode) already enforces this; this check closes the P2P
// import path (TxValidator::verify -> Transaction::verify), which previously only did
// signature recovery. r(32)||s(32)||yParity(1) wire format, s is the middle 32 bytes.
const u256 c_secp256k1n("0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
const u256 c_secp256k1nOver2 = c_secp256k1n / 2;
}  // namespace

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
    auto [recovered, sender] = signatureImpl.recoverAddress(hashImpl, hashResult, signature);
    if (!recovered) [[unlikely]]
    {
        BCOS_LOG(INFO) << LOG_DESC("recover sender address failed")
                       << LOG_KV("hash", hashResult.abridged());
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("recover sender address from signature failed"));
    }
    // EIP-2: reject malleable (high-s) signatures on the P2P import path too. The RPC path
    // (Web3Transaction::decode -> checkEip2Signature) already rejects s > n/2; without this
    // symmetric gate a P2P-imported malleated tx would pass admission and diverge from
    // op-geth/op-reth, which enforce low-s at both txpool admission and block execution.
    if (type() == static_cast<uint8_t>(TransactionType::Web3Transaction) && signature.size() == 65)
    {
        auto const s = bcos::fromBigEndian<u256>(signature.getCroppedData(32, 32));
        if (s == 0 || s > c_secp256k1nOver2) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("EIP-2: malleable signature (s exceeds secp256k1n/2)"));
        }
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