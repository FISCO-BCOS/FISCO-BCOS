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
 * @file EthBlock.h
 * @brief Ethereum-standard block — inherits Block, RLP encoding, embedded header data
 * @date 2026/6/24
 */
#pragma once

#include "EthBlockHeader.h"
#include "EthWithdrawal.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <memory>
#include <vector>
#include <span>

namespace bcos::protocol
{

class EthBlock : public std::enable_shared_from_this<EthBlock>
{
public:
    using Ptr = std::shared_ptr<EthBlock>;

    EthBlock() = default;
    explicit EthBlock(EthBlockHeader _header): m_header(std::move(_header)) {}
    explicit EthBlock(bcos::bytesConstRef _data) 
    { 
        if (auto err = decode(_data); err != nullptr)
        {
            clear();
            return;
        }
    }

    EthBlock(const EthBlock&) = delete;
    EthBlock(EthBlock&&) noexcept = default;
    EthBlock& operator=(const EthBlock&) = delete;
    EthBlock& operator=(EthBlock&&) noexcept = default;
    ~EthBlock() noexcept = default;

    // ---- Block interfaces ----

    bcos::Error::UniquePtr decode(bcos::bytesConstRef _data);
    void encode(bcos::bytes& _encodeData) const;

    size_t size() const;

    void setBlockHeader(EthBlockHeader&& _blockHeader) { m_header = std::move(_blockHeader); }
    EthBlockHeader::Ptr blockHeaderPtr() { return std::shared_ptr<EthBlockHeader>(shared_from_this(), &m_header); }
    EthBlockHeader& blockHeader() { return m_header; }

    void setTransactionRlp(size_t _index, const bcos::bytes& _rlp); 
    void appendTransaction(const bcos::bytes& _rlp);
    void appendTransaction(bcos::bytes&& _rlp);
    std::span<const bcos::bytes> transactionRlps() const { return m_rlpTxs; }

    void setTransactionHash(size_t _index, const bcos::crypto::HashType& _hash);
    std::span<const bcos::crypto::HashType> transactionHashes() const { return m_txHashes; }

    void setWithdrawal(size_t _index, const EthWithdrawal& _wd);
    void appendWithdrawal(const EthWithdrawal& _wd) { m_withdrawals.push_back(_wd); }
    std::span<const EthWithdrawal> withdrawals() const { return m_withdrawals; }

    // Clear all
    void clear();

private:
    EthBlockHeader m_header;

    std::vector<bcos::crypto::HashType> m_txHashes;
    std::vector<bcos::bytes> m_rlpTxs;

    // Uncles (has been deprecated)
    // std::vector<EthBlockHeader> m_uncles;

    std::vector<EthWithdrawal> m_withdrawals;
};

}  // namespace bcos::protocol
