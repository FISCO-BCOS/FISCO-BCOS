/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief interfaces for transaction
 * @file Transaction.h
 */
#pragma once
#include "TransactionSubmitResult.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <concepts>
#include <shared_mutex>
#include <span>
#include <type_traits>

namespace bcos::protocol
{
enum TransactionType
{
    NullTransaction = 0,
    ContractCreation,
    MessageCall,
};

using TxSubmitCallback =
    std::function<void(Error::Ptr, bcos::protocol::TransactionSubmitResult::Ptr)>;
class Transaction
{
public:
    enum Attribute : uint32_t
    {
        EVM_ABI_CODEC = 0x1,
        LIQUID_SCALE_CODEC = 0x2,
        DAG = 0x4,
        LIQUID_CREATE = 0x8,
    };

    using Ptr = std::shared_ptr<Transaction>;
    using ConstPtr = std::shared_ptr<const Transaction>;
    explicit Transaction(bcos::crypto::CryptoSuite::Ptr _cryptoSuite) : m_cryptoSuite(_cryptoSuite)
    {}

    virtual ~Transaction() {}

    virtual void decode(bytesConstRef _txData) = 0;
    virtual void encode(bcos::bytes& txData) const = 0;
    virtual bcos::crypto::HashType hash(bool _useCache = true) const = 0;

    virtual void verify() const
    {
        // The tx has already been verified
        if (!sender().empty())
        {
            return;
        }

        // check the hash when verify
        auto hashResult = hash(false);
        if (hashResult != this->hash())
        {
            throw bcos::Exception("Hash mismatch!");
        }

        // check the signatures
        auto signature = signatureData();
        auto publicKey = m_cryptoSuite->signatureImpl()->recover(this->hash(), signature);
        // recover the sender
        forceSender(m_cryptoSuite->calculateAddress(publicKey).asBytes());
    }

    virtual int32_t version() const = 0;
    virtual std::string_view chainId() const = 0;
    virtual std::string_view groupId() const = 0;
    virtual int64_t blockLimit() const = 0;
    virtual u256 nonce() const = 0;
    virtual std::string_view to() const = 0;
    virtual std::string_view abi() const = 0;
    virtual std::string_view source() const = 0;
    virtual void setSource(std::string const& _source) = 0;

    virtual std::string_view sender() const
    {
        return std::string_view((char*)m_sender.data(), m_sender.size());
    }

    virtual bytesConstRef input() const = 0;
    virtual int64_t importTime() const = 0;
    virtual void setImportTime(int64_t _importTime) = 0;
    virtual TransactionType type() const
    {
        if (to().size() > 0)
        {
            return TransactionType::MessageCall;
        }
        return TransactionType::ContractCreation;
    }
    virtual void forceSender(bytes _sender) const { m_sender = std::move(_sender); }
    virtual bytesConstRef signatureData() const = 0;

    virtual uint32_t attribute() const = 0;
    virtual void setAttribute(uint32_t attribute) = 0;

    TxSubmitCallback takeSubmitCallback() { return std::move(m_submitCallback); }
    TxSubmitCallback submitCallback() const { return m_submitCallback; }
    void setSubmitCallback(TxSubmitCallback _submitCallback) { m_submitCallback = _submitCallback; }
    bool synced() const { return m_synced; }
    void setSynced(bool _synced) const { m_synced = _synced; }

    bool sealed() const { return m_sealed; }
    void setSealed(bool _sealed) const { m_sealed = _sealed; }

    bool invalid() const { return m_invalid; }
    void setInvalid(bool _invalid) const { m_invalid = _invalid; }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() { return m_cryptoSuite; }

    void appendKnownNode(bcos::crypto::NodeIDPtr _node) const
    {
        std::unique_lock<std::shared_mutex> l(x_knownNodeList);
        m_knownNodeList.insert(_node);
    }

    bool isKnownBy(bcos::crypto::NodeIDPtr _node) const
    {
        std::shared_lock<std::shared_mutex> l(x_knownNodeList);
        return m_knownNodeList.count(_node);
    }

    void setSystemTx(bool _systemTx) const { m_systemTx = _systemTx; }
    bool systemTx() const { return m_systemTx; }

    void setBatchId(bcos::protocol::BlockNumber _batchId) const { m_batchId = _batchId; }
    bcos::protocol::BlockNumber batchId() const { return m_batchId; }

    void setBatchHash(bcos::crypto::HashType const& _hash) const { m_batchHash = _hash; }
    bcos::crypto::HashType const& batchHash() const { return m_batchHash; }

    bool storeToBackend() const { return m_storeToBackend; }
    void setStoreToBackend(bool _storeToBackend) const { m_storeToBackend = _storeToBackend; }

protected:
    mutable bcos::bytes m_sender;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;

    TxSubmitCallback m_submitCallback;
    // the tx has been synced or not

    // the hash of the proposal that the tx batched into
    mutable bcos::crypto::HashType m_batchHash;

    // Record the list of nodes containing the transaction and provide related query interfaces.
    mutable std::shared_mutex x_knownNodeList;
    // Record the node where the transaction exists
    mutable bcos::crypto::NodeIDSet m_knownNodeList;
    // the number of proposal that the tx batched into
    mutable bcos::protocol::BlockNumber m_batchId = {-1};

    mutable std::atomic_bool m_synced = {false};
    // the tx has been sealed by the leader of not
    mutable std::atomic_bool m_sealed = {false};
    // the tx is invalid for verify failed
    mutable std::atomic_bool m_invalid = {false};
    // the transaction is the system transaction or not
    mutable std::atomic_bool m_systemTx = {false};
    // the transaction has been stored to the storage or not
    mutable std::atomic_bool m_storeToBackend = {false};
};

using Transactions = std::vector<Transaction::Ptr>;
using TransactionsPtr = std::shared_ptr<Transactions>;
using TransactionsConstPtr = std::shared_ptr<const Transactions>;
using ConstTransactions = std::vector<Transaction::ConstPtr>;
using ConstTransactionsPtr = std::shared_ptr<ConstTransactions>;

}  // namespace bcos::protocol