/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : According to the interface and parameters, generate transactions
 * @file: TxGenerator.h
 * @author: yujiechen
 * @date: 2020-06-03
 */
#pragma once
#include <libethcore/ABI.h>
#include <libethcore/Transaction.h>

#define TXGEN_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("TxGenerator")
namespace dev
{
namespace consensus
{
class TxGenerator
{
public:
    using Ptr = std::shared_ptr<TxGenerator>;
    TxGenerator(u256 const& _chainId, u256 const& _groupId, unsigned const& _validBlockLimit = 600)
      : m_chainId(_chainId), m_groupId(_groupId), m_validBlockLimit(_validBlockLimit)
    {}

    // Specify interface declarations, from and to, generate unsigned transactions
    template <class... T>
    dev::eth::Transaction::Ptr generateTransactionWithoutSig(std::string _interface,
        dev::eth::BlockNumber const& _currentNumber, Address const& _to,
        dev::eth::Transaction::Type const& _type, T const&... _params)
    {
        dev::eth::Transaction::Ptr generatedTx = std::make_shared<dev::eth::Transaction>();
        // get transaction data according to interface and params
        dev::eth::ContractABI abiObject;
        std::shared_ptr<dev::bytes> data = std::make_shared<dev::bytes>();
        *data = abiObject.abiIn(_interface, _params...);
        // set fields for the transaction
        generatedTx->setReceiveAddress(_to);
        generatedTx->setData(data);
        generatedTx->setChainId(m_chainId);
        generatedTx->setGroupId(m_groupId);
        generatedTx->setBlockLimit(u256(_currentNumber + m_validBlockLimit));
        generatedTx->setType(_type);
        generatedTx->setNonce(generateRandomValue());
        generatedTx->setImportTime(utcTime());
        return generatedTx;
    }

    // Specify interface declaration, from and to, generate signed transaction
    template <class... T>
    dev::eth::Transaction::Ptr generateTransactionWithSig(std::string _interface,
        dev::eth::BlockNumber const& _currentNumber, Address const& _to, KeyPair const& _keyPair,
        dev::eth::Transaction::Type const& _type, T const&... _params)
    {
        auto generatedTx =
            generateTransactionWithoutSig(_interface, _currentNumber, _to, _type, _params...);
        // sign with KeyPair for the generated transaction
        auto hashToSign = generatedTx->sha3(dev::eth::IncludeSignature::WithoutSignature);
        auto sign = dev::crypto::Sign(_keyPair, hashToSign);
        generatedTx->updateSignature(sign);
        TXGEN_LOG(DEBUG) << LOG_DESC("generateTransactionWithSig")
                         << LOG_KV("interface", _interface) << LOG_KV("blockNumber", _currentNumber)
                         << LOG_KV("to", _to.abridged())
                         << LOG_KV("from", _keyPair.pub().abridged())
                         << LOG_KV("nonce", generatedTx->nonce());
        return generatedTx;
    }

private:
    u256 generateRandomValue()
    {
        auto randomValue = h256::random();
        return u256(randomValue);
    }

private:
    u256 m_chainId;
    u256 m_groupId;
    unsigned m_validBlockLimit;
};
}  // namespace consensus
}  // namespace dev