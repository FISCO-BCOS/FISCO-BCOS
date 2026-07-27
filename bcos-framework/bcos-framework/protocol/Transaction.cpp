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
Web3AccessList const& Transaction::emptyWeb3AccessList()
{
    static Web3AccessList const empty;
    return empty;
}

Web3AccessList const& Transaction::web3AccessList() const
{
    return emptyWeb3AccessList();
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