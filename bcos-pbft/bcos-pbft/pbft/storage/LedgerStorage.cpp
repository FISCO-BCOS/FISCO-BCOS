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
 * @brief  Storage for the ledger
 * @file LedgerStorage.cpp
 * @author: yujiechen
 * @date 2021-04-26
 */
#include "LedgerStorage.h"
#include "../utilities/Common.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage/Table.h>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::storage;

PBFTProposalListPtr LedgerStorage::loadState(BlockNumber _stabledIndex)
{
    m_maxCommittedProposalIndexFetched = false;
    asyncGetLatestCommittedProposalIndex();
    auto startT = utcSteadyTime();
    while (utcSteadyTime() - startT < m_timeout)
    {
        if (m_maxCommittedProposalIndexFetched)
        {
            break;
        }
        boost::unique_lock<boost::mutex> l(x_signalled);
        m_signalled.wait_for(l, boost::chrono::milliseconds(10));
    }
    if (!m_maxCommittedProposalIndexFetched)
    {
        PBFT_STORAGE_LOG(WARNING) << LOG_DESC(
            "loadState failed for fetch maxCommittedProposalIndex failed");
        BOOST_THROW_EXCEPTION(InitPBFTException() << errinfo_comment(
                                  "loadState failed for fetch maxCommittedProposalIndex failed"));
    }
    // fetch the committed proposals
    if (m_maxCommittedProposalIndex <= _stabledIndex)
    {
        PBFT_STORAGE_LOG(INFO) << LOG_DESC("no need to fetch committed proposal")
                               << LOG_KV("maxCommittedProposal", m_maxCommittedProposalIndex)
                               << LOG_KV("stableCheckPoint", _stabledIndex);
        m_maxCommittedProposalIndex = _stabledIndex;
        return nullptr;
    }
    auto offset = (m_maxCommittedProposalIndex - _stabledIndex);
    PBFT_STORAGE_LOG(INFO) << LOG_DESC("recover committed proposal from the storage")
                           << LOG_KV("start", _stabledIndex + 1)
                           << LOG_KV("end", m_maxCommittedProposalIndex) << LOG_KV("size", offset);

    m_stateFetched = false;
    auto self = std::weak_ptr<LedgerStorage>(shared_from_this());
    asyncGetCommittedProposals(
        _stabledIndex + 1, offset, [self](PBFTProposalListPtr _proposalList) {
            try
            {
                auto storage = self.lock();
                if (!storage)
                {
                    return;
                }
                if (_proposalList)
                {
                    storage->m_stateProposals = _proposalList;
                }
                storage->m_stateFetched = true;
                storage->m_signalled.notify_all();
            }
            catch (std::exception const& e)
            {
                PBFT_STORAGE_LOG(WARNING)
                    << LOG_DESC(
                           "The committedProposals have been received, but the "
                           "callback is called exception")
                    << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
    startT = utcSteadyTime();
    while (utcSteadyTime() - startT < m_timeout)
    {
        if (m_stateFetched)
        {
            break;
        }
        boost::unique_lock<boost::mutex> l(x_signalled);
        m_signalled.wait_for(l, boost::chrono::milliseconds(10));
    }
    if (!m_stateFetched)
    {
        PBFT_STORAGE_LOG(WARNING) << LOG_DESC(
            "loadState failed for fetch committedProposal failed");
        BOOST_THROW_EXCEPTION(InitPBFTException() << errinfo_comment(
                                  "loadState failed for fetch committedProposal failed"));
    }
    if (!m_stateProposals || m_stateProposals->size() == 0)
    {
        m_maxCommittedProposalIndex = _stabledIndex;
    }
    return m_stateProposals;
}

void LedgerStorage::asyncGetCommittedProposals(
    BlockNumber _start, size_t _offset, std::function<void(PBFTProposalListPtr)> _onSuccess)
{
    // Note: The called program must effectively handle exceptions
    if (_start > m_maxCommittedProposalIndex)
    {
        PBFT_STORAGE_LOG(WARNING) << LOG_DESC("asyncGetCommittedProposals failed")
                                  << LOG_KV(
                                         "maxCommittedProposalIndex", m_maxCommittedProposalIndex)
                                  << LOG_KV("requestedMinIndex", _start);
        return;
    }
    auto keys = std::make_shared<std::vector<std::string>>();
    auto endIndex =
        std::min((int64_t)(_start + _offset - 1), (int64_t)m_maxCommittedProposalIndex.load());
    for (int64_t i = _start; i <= endIndex; i++)
    {
        keys->push_back(boost::lexical_cast<std::string>(i));
    }
    auto self = std::weak_ptr<LedgerStorage>(shared_from_this());
    m_storage->asyncGetBatch(m_pbftCommitDB, keys,
        [self, _onSuccess](
            Error::UniquePtr&& _error, std::shared_ptr<std::vector<std::string>>&& _values) {
            if (_error != nullptr)
            {
                PBFT_STORAGE_LOG(WARNING)
                    << LOG_DESC("asyncGetCommittedProposals: get proposals failed")
                    << LOG_KV("error", _error->errorCode())
                    << LOG_KV("errorMessage", _error->errorMessage());
                return;
            }
            try
            {
                auto storage = self.lock();
                if (!storage)
                {
                    return;
                }
                auto proposalList = std::make_shared<PBFTProposalList>();
                for (auto const& value : *_values)
                {
                    if (value.empty())
                    {
                        PBFT_STORAGE_LOG(INFO)
                            << LOG_DESC("asyncGetCommittedProposals: empty committed proposal")
                            << LOG_KV("valuesSize", _values->size());
                        _onSuccess(nullptr);
                        return;
                    }
                    auto proposalData = bytesConstRef((byte const*)value.data(), value.size());
                    proposalList->push_back(
                        storage->m_messageFactory->createPBFTProposal(proposalData));
                }
                _onSuccess(proposalList);
                PBFT_STORAGE_LOG(INFO) << LOG_DESC("asyncGetCommittedProposals success")
                                       << LOG_KV("proposals", proposalList->size());
            }
            catch (std::exception const& e)
            {
                PBFT_STORAGE_LOG(WARNING) << LOG_DESC("asyncGetCommittedProposals exception")
                                          << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void LedgerStorage::asyncGetLatestCommittedProposalIndex()
{
    auto self = std::weak_ptr<LedgerStorage>(shared_from_this());
    m_storage->asyncGet(m_pbftCommitDB, m_maxCommittedProposalKey,
        [self](Error::UniquePtr&& _error, std::string_view&& _value) {
            try
            {
                auto storage = self.lock();
                if (!storage)
                {
                    storage->m_signalled.notify_all();
                    return;
                }
                if (_value.size() == 0)
                {
                    storage->m_maxCommittedProposalIndexFetched = true;
                    storage->m_signalled.notify_all();
                    return;
                }
                if (_error != nullptr)
                {
                    PBFT_STORAGE_LOG(WARNING)
                        << LOG_DESC("asyncGetLatestCommittedProposalIndex failed")
                        << LOG_KV("errorCode", _error->errorCode())
                        << LOG_KV("errorMessage", _error->errorMessage());
                    storage->m_signalled.notify_all();
                    return;
                }
                auto latestCommittedProposalIndex = boost::lexical_cast<int64_t>(_value);
                if (storage->m_maxCommittedProposalIndex < latestCommittedProposalIndex)
                {
                    storage->m_maxCommittedProposalIndex = latestCommittedProposalIndex;
                }
                storage->m_maxCommittedProposalIndexFetched = true;
                storage->m_signalled.notify_all();
                PBFT_STORAGE_LOG(INFO)
                    << LOG_DESC("asyncGetLatestCommittedProposalIndex")
                    << LOG_KV("latestCommittedProposalIndex", storage->m_maxCommittedProposalIndex);
            }
            catch (std::exception const& e)
            {
                PBFT_STORAGE_LOG(WARNING)
                    << LOG_DESC("asyncGetLatestCommittedProposalIndex exception")
                    << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void LedgerStorage::asyncCommitProposal(PBFTProposalInterface::Ptr _committedProposal)
{
    if (m_maxCommittedProposalIndex.load() >= _committedProposal->index())
    {
        return;
    }
    m_maxCommittedProposalIndex.store(_committedProposal->index());
    PBFT_STORAGE_LOG(INFO) << LOG_DESC("asyncCommitProposal: write the committed proposal into db")
                           << LOG_KV("index", _committedProposal->index());
    // commit the max-index proposal information
    auto maxIndexStr = boost::lexical_cast<std::string>(m_maxCommittedProposalIndex);
    auto maxIndexBytes = std::make_shared<bytes>(maxIndexStr.begin(), maxIndexStr.end());
    asyncPutProposal(
        m_pbftCommitDB, m_maxCommittedProposalKey, maxIndexBytes, _committedProposal->index());

    // commit the data
    auto encodedData = _committedProposal->encode();
    asyncPutProposal(m_pbftCommitDB, boost::lexical_cast<std::string>(_committedProposal->index()),
        encodedData, _committedProposal->index());
}

void LedgerStorage::asyncPutProposal(std::string const& _dbName, std::string const& _key,
    bytesPointer _committedData, BlockNumber _proposalIndex, size_t _retryTime)
{
    auto startT = utcTime();
    auto self = std::weak_ptr<LedgerStorage>(shared_from_this());
    // TODO: optimize here to decrease copy overhead
    // Note: asyncPut now is a sync implementation, but no need to async here since this
    // timeout-head is only between 5-10ms
    m_storage->asyncPut(_dbName, _key,
        std::string((const char*)_committedData->data(), _committedData->size()),
        [startT, _dbName, _committedData, _key, _proposalIndex, _retryTime, self](
            Error::UniquePtr&& _error) {
            if (_error == nullptr)
            {
                PBFT_STORAGE_LOG(INFO)
                    << LOG_DESC("asyncPutProposal: commit success") << LOG_KV("dbName", _dbName)
                    << LOG_KV("key", _key) << LOG_KV("number", _proposalIndex)
                    << LOG_KV("timecost", (utcTime() - startT))
                    << LOG_KV("dataSize", _committedData->size());
                return;
            }
            PBFT_STORAGE_LOG(WARNING)
                << LOG_DESC("asyncPutProposal failed") << LOG_KV("proposalIndex", _proposalIndex)
                << LOG_KV("key", _key) << LOG_KV("dbName", _dbName)
                << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
            try
            {
                auto ledgerStorage = self.lock();
                if (!ledgerStorage)
                {
                    return;
                }
                if (_retryTime >= 3)
                {
                    return;
                }
                ledgerStorage->asyncPutProposal(
                    _dbName, _key, _committedData, _proposalIndex, (_retryTime + 1));
            }
            catch (std::exception const& e)
            {
                PBFT_STORAGE_LOG(WARNING) << LOG_DESC("asyncPutProposal exception")
                                          << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void LedgerStorage::asyncCommitStableCheckPoint(PBFTProposalInterface::Ptr _stableProposal)
{
    std::shared_ptr<std::vector<protocol::Signature>> signatureList =
        std::make_shared<std::vector<protocol::Signature>>();
    for (size_t i = 0; i < _stableProposal->signatureProofSize(); i++)
    {
        auto proof = _stableProposal->signatureProof(i);
        Signature signature;
        signature.index = proof.first;
        signature.signature = proof.second.toBytes();
        signatureList->push_back(signature);
    }
    auto blockHeader =
        m_blockFactory->blockHeaderFactory()->createBlockHeader(_stableProposal->data());
    blockHeader->setSignatureList(*signatureList);
    auto blockSignatureList = blockHeader->signatureList();
    PBFT_LOG(INFO) << LOG_DESC("asyncCommitStableCheckPoint: set signatureList")
                   << LOG_KV("index", blockHeader->number())
                   << LOG_KV("hash", blockHeader->hash().abridged())
                   << LOG_KV("proofSize", signatureList->size())
                   << LOG_KV("blockProofSize", blockSignatureList.size());
    // Note: enqueue here to increase the performance since commitBlock is a sync implementation
    auto self = std::weak_ptr<LedgerStorage>(shared_from_this());
    m_commitBlockWorker->enqueue([self, blockHeader, _stableProposal]() {
        auto storage = self.lock();
        if (!storage)
        {
            return;
        }
        // get the transactions list
        auto txsInfo = storage->m_blockFactory->createBlock(_stableProposal->extraData());
        storage->commitStableCheckPoint(_stableProposal, blockHeader, txsInfo);
    });
}
void LedgerStorage::onStableCheckPointCommitted(
    size_t _txsSize, BlockHeader::Ptr _blockHeader, LedgerConfig::Ptr _ledgerConfig)
{
    _ledgerConfig->setSealerId(_blockHeader->sealer());
    _ledgerConfig->setTxsSize(_txsSize);
    // reset the blockNumber
    _ledgerConfig->setBlockNumber(_blockHeader->number());
    _ledgerConfig->setHash(_blockHeader->hash());
    // finalize consensus
    if (m_finalizeHandler)
    {
        m_finalizeHandler(_ledgerConfig, false);
    }
    // remove the proposal committed into the ledger,
    // don't remove the latest stabled checkpoint for checkpoint msg response consideration
    if (_blockHeader->number() > c_reservedCheckPointSize)
    {
        asyncRemoveStabledCheckPoint(_blockHeader->number() - c_reservedCheckPointSize);
    }
}
void LedgerStorage::commitStableCheckPoint(PBFTProposalInterface::Ptr _stableProposal,
    BlockHeader::Ptr _blockHeader, Block::Ptr _blockInfo)
{
    auto self = std::weak_ptr<LedgerStorage>(shared_from_this());
    auto startT = utcTime();
    m_scheduler->commitBlock(_blockHeader, [_stableProposal, _blockHeader, _blockInfo, startT,
                                               self](Error::Ptr&& _error,
                                               LedgerConfig::Ptr _ledgerConfig) {
        try
        {
            auto ledgerStorage = self.lock();
            if (!ledgerStorage)
            {
                return;
            }
            if (_error != nullptr)
            {
                PBFT_STORAGE_LOG(ERROR) << LOG_DESC("commitStableCheckPoint failed")
                                        << LOG_KV("errorCode", _error->errorCode())
                                        << LOG_KV("errorInfo", _error->errorMessage())
                                        << LOG_KV("proposalIndex", _blockHeader->number())
                                        << LOG_KV("timecost", utcTime() - startT);
                ledgerStorage->m_onStableCheckPointCommitFailed(std::move(_error), _stableProposal);
                return;
            }
            auto commitPerTx =
                (double)(utcTime() - startT) / (double)(_blockInfo->transactionsHashSize());
            PBFT_STORAGE_LOG(INFO)
                << METRIC << LOG_DESC("commitStableCheckPoint success")
                << LOG_KV("index", _blockHeader->number())
                << LOG_KV("hash", _ledgerConfig->hash().abridged())
                << LOG_KV("txs", _blockInfo->transactionsHashSize())
                << LOG_KV("timeCost", utcTime() - startT) << LOG_KV("commitPerTx", commitPerTx);
            auto txsSize = _blockInfo->transactionsHashSize();
            // Note:Here the thread pool is used to asynchronize the operation of PBFT finalize to
            // prevent the commitBlock from calling the callback synchronously and affecting the
            // performance.
            ledgerStorage->m_commitBlockWorker->enqueue([txsSize, _blockHeader, ledgerStorage,
                                                            _ledgerConfig]() {
                ledgerStorage->onStableCheckPointCommitted(txsSize, _blockHeader, _ledgerConfig);
            });
        }
        catch (std::exception const& e)
        {
            PBFT_STORAGE_LOG(WARNING) << LOG_DESC("commitStableCheckPoint exception")
                                      << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void LedgerStorage::asyncRemoveStabledCheckPoint(size_t _stabledCheckPointIndex)
{
    PBFT_STORAGE_LOG(INFO) << LOG_DESC("asyncRemoveStabledCheckPoint")
                           << LOG_KV("index", _stabledCheckPointIndex);
    asyncRemove(m_pbftCommitDB, boost::lexical_cast<std::string>(_stabledCheckPointIndex));
}

void LedgerStorage::asyncRemove(std::string const& _dbName, std::string const& _key)
{
    m_storage->asyncRemove(_dbName, _key, [_dbName, _key](const Error::Ptr& _error) {
        if (_error == nullptr)
        {
            PBFT_STORAGE_LOG(INFO) << LOG_DESC("asyncRemove success") << LOG_KV("dbName", _dbName)
                                   << LOG_KV("key", _key);
            return;
        }
        // TODO: remove failed
        PBFT_STORAGE_LOG(WARNING) << LOG_DESC("asyncRemove failed") << LOG_KV("dbName", _dbName)
                                  << LOG_KV("key", _key);
    });
}

void LedgerStorage::createKVTable(std::string const& _dbName)
{
    auto ret = std::make_shared<std::promise<Error::Ptr>>();
    auto future = ret->get_future();
    std::string valueFields = "value";
    m_storage->storage()->asyncCreateTable(
        _dbName, valueFields, [_dbName, ret](Error::UniquePtr&& _error, std::optional<Table>&&) {
            if (_error && _error->errorCode() != bcos::storage::StorageError::TableExists)
            {
                PBFT_STORAGE_LOG(WARNING)
                    << LOG_DESC("createKVTable error") << LOG_KV("table", _dbName)
                    << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
                ret->set_value(std::move(_error));
                return;
            }
            ret->set_value(nullptr);
            PBFT_STORAGE_LOG(INFO) << LOG_DESC("createKVTable success") << LOG_KV("table", _dbName);
        });
    auto error = future.get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(
            InitPBFTException() << errinfo_comment(
                "Create PBFT backup DB failed, code: " + std::to_string(error->errorCode()) +
                ", message:" + error->errorMessage()));
    }
}
