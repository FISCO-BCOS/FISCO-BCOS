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
        auto const bytesRef = extraTransactionBytes();
        hashResult = bcos::crypto::keccak256Hash(bytesRef);
    }

    auto const signature = signatureData();
    auto [recovered, sender] = signatureImpl.recoverAddress(hashImpl, hashResult, signature);
    if (!recovered) [[unlikely]]
    {
        BCOS_LOG(INFO) << LOG_DESC("recover sender address failed")
                       << LOG_KV("hash", hashResult.abridged());
        BOOST_THROW_EXCEPTION(std::invalid_argument("recover sender address from signature failed"));
    }

    forceSender(sender);
    setTainted(false);
}

std::ostream& operator<<(std::ostream& stream, const Transaction& transaction)
{
    stream << "Transaction{" << "hash=" << transaction.hash() << ", "
           << "version=" << transaction.version() << ", " << "chainId=" << transaction.chainId()
           << ", " << "groupId=" << transaction.groupId() << ", "
           << "blockLimit=" << transaction.blockLimit() << ", " << "nonce=" << transaction.nonce()
           << ", " << "to=" << transaction.to() << ", " << "abi=" << transaction.abi() << ", "
           << "value=" << transaction.value() << ", " << "gasPrice=" << transaction.gasPrice()
           << ", " << "gasLimit=" << transaction.gasLimit() << ", "
           << "maxFeePerGas=" << transaction.maxFeePerGas() << ", "
           << "maxPriorityFeePerGas=" << transaction.maxPriorityFeePerGas() << ", "
           << "extension=" << toHex(transaction.extension()) << ", "
           << "extraData=" << transaction.extraData() << ", "
           << "sender="
           << [&]() {
                  auto view = transaction.sender();
                  return bcos::bytesConstRef{
                      reinterpret_cast<const bcos::byte*>(view.data()), view.size()};
              }()
           << ", " << "input=" << toHex(transaction.input()) << ", "
           << "importTime=" << transaction.importTime() << ", "
           << "type=" << static_cast<int>(transaction.type()) << ", "
           << "attribute=" << transaction.attribute() << ", "
           << "size=" << transaction.size() << "}";
    return stream;
}
}  // namespace bcos::protocol