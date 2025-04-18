#include "TransactionFactoryImpl.h"
#include "../impl/TarsHashable.h"

bcostars::protocol::TransactionFactoryImpl::TransactionFactoryImpl(
    bcos::crypto::CryptoSuite::Ptr cryptoSuite)
  : m_cryptoSuite(std::move(cryptoSuite))
{}

bcos::protocol::Transaction::Ptr bcostars::protocol::TransactionFactoryImpl::createTransaction(
    bcos::bytesConstRef txData, bool checkSig, bool checkHash)
{
    auto transaction = std::make_shared<TransactionImpl>(
        [m_transaction = bcostars::Transaction()]() mutable { return &m_transaction; });

    transaction->decode(txData);
    // check value or gasPrice or maxFeePerGas or maxPriorityFeePerGas is hex string
    if (transaction->version() >= int32_t(bcos::protocol::TransactionVersion::V1_VERSION))
    {
        if (!bcos::isHexStringV2(transaction->mutableInner().data.value) ||
            !bcos::isHexStringV2(transaction->mutableInner().data.gasPrice) ||
            !bcos::isHexStringV2(transaction->mutableInner().data.maxFeePerGas) ||
            !bcos::isHexStringV2(transaction->mutableInner().data.maxPriorityFeePerGas))
        {
            BCOS_LOG(INFO) << LOG_DESC(
                                  "the transaction value or gasPrice or maxFeePerGas or "
                                  "maxPriorityFeePerGas is not hex string")
                           << LOG_KV("value", transaction->mutableInner().data.value)
                           << LOG_KV("gasPrice", transaction->mutableInner().data.gasPrice)
                           << LOG_KV("maxFeePerGas", transaction->mutableInner().data.maxFeePerGas)
                           << LOG_KV("maxPriorityFeePerGas",
                                  transaction->mutableInner().data.maxPriorityFeePerGas);
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("transaction value or gasPrice or maxFeePerGas or "
                                      "maxPriorityFeePerGas is not hex string"));
        }
    }

    // other transaction type, do not need to check hash, skip
    if (transaction->type() ==
        static_cast<uint8_t>(bcos::protocol::TransactionType::BCOSTransaction))
    {
        auto originDataHash = std::move(transaction->mutableInner().dataHash);
        transaction->mutableInner().dataHash.clear();
        transaction->calculateHash(*m_cryptoSuite->hashImpl());

        // check if hash matching
        if (checkHash && !originDataHash.empty() &&
            (originDataHash != transaction->mutableInner().dataHash)) [[unlikely]]
        {
            bcos::crypto::HashType originHashResult(
                (bcos::byte*)originDataHash.data(), originDataHash.size());
            bcos::crypto::HashType hashResult(
                (bcos::byte*)transaction->mutableInner().dataHash.data(),
                transaction->mutableInner().dataHash.size());

            BCOS_LOG(WARNING) << LOG_DESC("the transaction hash does not match")
                              << LOG_KV("originHash", originHashResult.hex())
                              << LOG_KV("realHash", hashResult.hex());
            BOOST_THROW_EXCEPTION(std::invalid_argument(
                "transaction hash mismatching, maybe transaction version not support."));
        }
    }

    if (checkSig)
    {
        transaction->mutableInner().sender.clear();  // Bugfix: User will fake a illegal sender,
        // must clear sender given by rpc
        transaction->verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
    }
    return transaction;
}

std::shared_ptr<bcos::protocol::Transaction>
bcostars::protocol::TransactionFactoryImpl::createTransaction(int32_t _version, std::string _to,
    bcos::bytes const& _input, std::string const& _nonce, int64_t _blockLimit, std::string _chainId,
    std::string _groupId, int64_t _importTime, std::string _abi, std::string _value,
    std::string _gasPrice, int64_t _gasLimit, std::string _maxFeePerGas,
    std::string _maxPriorityFeePerGas)
{
    auto transaction = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_transaction = bcostars::Transaction()]() mutable { return &m_transaction; });
    auto& inner = transaction->mutableInner();
    inner.data.version = _version;
    inner.data.to = std::move(_to);
    inner.data.input.assign(_input.begin(), _input.end());
    inner.data.blockLimit = _blockLimit;
    inner.data.chainID = std::move(_chainId);
    inner.data.groupID = std::move(_groupId);
    inner.data.nonce = boost::lexical_cast<std::string>(_nonce);
    inner.data.abi = std::move(_abi);
    if (_version == int32_t(bcos::protocol::TransactionVersion::V0_VERSION))
    {
        inner.data.value = "0x0";
        inner.data.gasPrice = "0x0";
        inner.data.gasLimit = 0;
        inner.data.maxFeePerGas = "0x0";
        inner.data.maxPriorityFeePerGas = "0x0";
    }

    if (_version >= int32_t(bcos::protocol::TransactionVersion::V1_VERSION))
    {
        if (bcos::isHexStringV2(_value) && bcos::isHexStringV2(_gasPrice) &&
            bcos::isHexStringV2(_maxFeePerGas) && bcos::isHexStringV2(_maxPriorityFeePerGas))
        {
            inner.data.value = std::move(_value);
            inner.data.gasPrice = std::move(_gasPrice);
            inner.data.gasLimit = _gasLimit;
            inner.data.maxFeePerGas = std::move(_maxFeePerGas);
            inner.data.maxPriorityFeePerGas = std::move(_maxPriorityFeePerGas);
        }
        else
        {
            BCOS_LOG(WARNING) << LOG_DESC(
                                     "the transaction value or gasPrice or maxFeePerGas or "
                                     "maxPriorityFeePerGas is not hex string")
                              << LOG_KV("value", _value) << LOG_KV("gasPrice", _gasPrice)
                              << LOG_KV("maxFeePerGas", _maxFeePerGas)
                              << LOG_KV("maxPriorityFeePerGas", _maxPriorityFeePerGas);
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("transaction value or gasPrice or maxFeePerGas or "
                                      "maxPriorityFeePerGas is not hex string"));
        }
    }
    inner.importTime = _importTime;

    // Update the hash field
    bcos::concepts::hash::calculate(inner, m_cryptoSuite->hashImpl()->hasher(), inner.dataHash);

    return transaction;
}

bcos::protocol::Transaction::Ptr bcostars::protocol::TransactionFactoryImpl::createTransaction(
    int32_t _version, std::string _to, bcos::bytes const& _input, std::string const& _nonce,
    int64_t _blockLimit, std::string _chainId, std::string _groupId, int64_t _importTime,
    const bcos::crypto::KeyPairInterface& keyPair, std::string _abi, std::string _value,
    std::string _gasPrice, int64_t _gasLimit, std::string _maxFeePerGas,
    std::string _maxPriorityFeePerGas)
{
    bcos::protocol::Transaction::Ptr tx;
    if (_version == int32_t(bcos::protocol::TransactionVersion::V0_VERSION))
    {
        tx = createTransaction(_version, std::move(_to), _input, _nonce, _blockLimit,
            std::move(_chainId), std::move(_groupId), _importTime, std::move(_abi));
    }
    else
    {
        tx = createTransaction(_version, std::move(_to), _input, _nonce, _blockLimit,
            std::move(_chainId), std::move(_groupId), _importTime, std::move(_abi),
            std::move(_value), std::move(_gasPrice), _gasLimit, std::move(_maxFeePerGas),
            std::move(_maxPriorityFeePerGas));
    }
    auto sign = m_cryptoSuite->signatureImpl()->sign(keyPair, tx->hash(), true);

    auto tarsTx = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    auto& inner = tarsTx->mutableInner();
    inner.signature.assign(sign->begin(), sign->end());

    return tx;
}

void bcostars::protocol::TransactionFactoryImpl::setCryptoSuite(
    bcos::crypto::CryptoSuite::Ptr cryptoSuite)
{
    m_cryptoSuite = std::move(cryptoSuite);
}

bcos::crypto::CryptoSuite::Ptr bcostars::protocol::TransactionFactoryImpl::cryptoSuite()
{
    return m_cryptoSuite;
}
