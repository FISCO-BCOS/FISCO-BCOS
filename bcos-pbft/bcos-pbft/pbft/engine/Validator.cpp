/**
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
 * @brief Validator for the consensus module
 * @file Validator.cpp
 * @author: yujiechen
 * @date 2021-04-21
 */
#include "Validator.h"
using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

void TxsValidator::verifyProposal(bcos::crypto::PublicPtr _fromNode,
    PBFTProposalInterface::Ptr _proposal,
    std::function<void(Error::Ptr, bool)> _verifyFinishedHandler)
{
    // TODO: check the sealerList here
    auto block = m_blockFactory->createBlock(_proposal->data());
    auto blockHeader = block->blockHeader();
    if (blockHeader->number() != _proposal->index())
    {
        if (_verifyFinishedHandler)
        {
            auto error = std::make_shared<Error>(-1, "Invalid proposal");
            _verifyFinishedHandler(error, false);
        }
        return;
    }
    m_txPool->asyncVerifyBlock(_fromNode, _proposal->data(), _verifyFinishedHandler);
}

void TxsValidator::asyncResetTxsFlag(bytesConstRef _data, bool _flag, bool _emptyTxBatchHash)
{
    auto block = m_blockFactory->createBlock(_data);
    auto blockHeader = block->blockHeader();
    if (_flag)
    {
        // already has the reset request
        if (!insertResettingProposal(blockHeader->hash()))
        {
            return;
        }
    }
    auto self = std::weak_ptr<TxsValidator>(shared_from_this());
    m_worker->enqueue([self, blockHeader, block, _flag, _emptyTxBatchHash]() {
        try
        {
            auto validator = self.lock();
            if (!validator)
            {
                return;
            }

            auto txsHash = std::make_shared<HashList>();
            for (size_t i = 0; i < block->transactionsHashSize(); i++)
            {
                txsHash->emplace_back(block->transactionHash(i));
            }
            if (txsHash->size() == 0)
            {
                return;
            }
            PBFT_LOG(INFO) << LOG_DESC("asyncResetTxsFlag")
                           << LOG_KV("index", blockHeader->number())
                           << LOG_KV("hash", blockHeader->hash().abridged())
                           << LOG_KV("flag", _flag);
            validator->asyncResetTxsFlag(block, txsHash, _flag, _emptyTxBatchHash);
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("asyncResetTxsFlag exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void TxsValidator::asyncResetTxsFlag(
    bcos::protocol::Block::Ptr _block, HashListPtr _txsHashList, bool _flag, bool _emptyTxBatchHash)
{
    auto blockHeader = _block->blockHeader();
    auto proposalNumber = blockHeader->number();
    auto proposalHash = blockHeader->hash();
    if (_emptyTxBatchHash)
    {
        proposalNumber = -1;
        proposalHash = bcos::crypto::HashType();
    }
    m_txPool->asyncMarkTxs(_txsHashList, _flag, proposalNumber, proposalHash,
        [this, _block, blockHeader, _txsHashList, _flag, _emptyTxBatchHash](Error::Ptr _error) {
            // must ensure asyncResetTxsFlag success before seal new next blocks
            if (_flag)
            {
                eraseResettingProposal(blockHeader->hash());
            }
            if (_error == nullptr)
            {
                PBFT_LOG(INFO) << LOG_DESC("asyncMarkTxs success")
                               << LOG_KV("index", blockHeader->number())
                               << LOG_KV("hash", blockHeader->hash().abridged())
                               << LOG_KV("flag", _flag)
                               << LOG_KV("emptyTxBatchHash", _emptyTxBatchHash);
                return;
            }
            PBFT_LOG(WARNING) << LOG_DESC("asyncMarkTxs failed")
                              << LOG_KV("code", _error->errorCode())
                              << LOG_KV("msg", _error->errorMessage());
            if (_flag)
            {
                insertResettingProposal(blockHeader->hash());
            }
        });
}