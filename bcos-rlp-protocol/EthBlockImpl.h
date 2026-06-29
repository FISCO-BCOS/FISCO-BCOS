/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EthBlockImpl.h
 * @brief Ethereum-standard block — inherits Block, RLP encoding, embedded header data
 * @date 2026/6/24
 */
#pragma once

#include "EthBlockHeaderImpl.h"
#include "EthWithdrawal.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionMetaData.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <memory>
#include <vector>

namespace bcos::protocol
{

class EthBlockImpl : public std::enable_shared_from_this<EthBlockImpl>
{
public:
    EthBlockImpl() = default;
    EthBlockImpl(const EthBlockImpl&) = delete;
    EthBlockImpl(EthBlockImpl&&) noexcept = default;
    EthBlockImpl& operator=(const EthBlockImpl&) = delete;
    EthBlockImpl& operator=(EthBlockImpl&&) noexcept = default;
    ~EthBlockImpl() noexcept = default;

    // ---- Block interfaces ----

    bcos::Error::UniquePtr decode(bcos::bytesConstRef _data);
    void encode(bcos::bytes& _encodeData) const;

    size_t size() const;

    EthBlockHeader& header() { return m_header; }
    const EthBlockHeader& header() const { return m_header; }
    void setHeader(const EthBlockHeader& data) { m_header = data; }

    void appendTransactionRlp(const bcos::bytes& _rlp);
    void appendTransactionRlp(bcos::bytes&& _rlp);
    void setTransactionRlp(size_t _index, const bcos::bytes& _rlp);
    std::span<const bcos::bytes> transactionRlps() const { return m_rlpTxs; }

    void appendTransactionHash(const bcos::crypto::HashType& _hash) { m_txHashes.push_back(_hash); }
    void setTransactionHash(size_t _index, const bcos::crypto::HashType& _hash);
    std::span<const bcos::crypto::HashType> transactionHashes() const { return m_txHashes; }

    void appendWithdrawal(const EthWithdrawal& _wd) { m_withdrawals.push_back(_wd); }
    void setWithdrawal(size_t _index, const EthWithdrawal& _wd);
    std::span<const EthWithdrawal> withdrawals() const { return m_withdrawals; }

    // Clear all
    void clear();

private:
    void appendTxRlp(const bcos::bytes& rlp);
    void appendTxRlp(bcos::bytes&& rlp);

    EthBlockHeader m_header;

    std::vector<bcos::crypto::HashType> m_txHashes;
    std::vector<bcos::bytes> m_rlpTxs;

    // Uncles (has been deprecated)
    // std::vector<EthBlockHeaderImpl> m_uncles;

    std::vector<EthWithdrawal> m_withdrawals;
};

}  // namespace bcos::protocol
