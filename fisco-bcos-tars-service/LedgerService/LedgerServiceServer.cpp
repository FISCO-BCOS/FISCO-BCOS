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
 * @brief server for Ledger
 * @file LedgerServiceServer.cpp
 * @author: yujiechen
 * @date 2021-10-18
 */

#include "LedgerServiceServer.h"
#include <bcos-tars-protocol/Common.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-task/Wait.h>

using namespace bcostars;
using namespace bcos;

bcostars::Error LedgerServiceServer::asyncGetBatchTxsByHashList(
    const std::vector<std::vector<tars::Char>>& _txsHashList, tars::Bool _withProof,
    std::vector<bcostars::Transaction>&, std::map<std::string, std::vector<std::string>>&,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto hashList = std::make_shared<bcos::crypto::HashList>();
    for (auto const& hash : _txsHashList)
    {
        if (hash.size() < bcos::crypto::HashType::SIZE)
        {
            continue;
        }
        hashList->emplace_back(bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(hash.data()), bcos::crypto::HashType::SIZE));
    }
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger, bcos::crypto::HashListPtr txHashList,
                   bool withProof, tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::Error::Ptr error;
        bcos::ledger::BatchTransactionsResult result;
        try
        {
            result = co_await ledger->getBatchTxsByHashList(std::move(txHashList), withProof);
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }

        std::vector<bcostars::Transaction> tarsTxs;
        if (result.first)
        {
            for (auto const& tx : *result.first)
            {
                tarsTxs.emplace_back(
                    std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx)->inner());
            }
        }
        std::map<std::string, std::vector<std::string>> tarsMerkleProofs;
        if (result.second)
        {
            for (auto const& it : *result.second)
            {
                std::vector<std::string> tarsMerkleItem;
                auto const& proofs = it.second;
                for (auto const& proof : *proofs)
                {
                    tarsMerkleItem.emplace_back(proof.hex());
                }
                tarsMerkleProofs[it.first] = tarsMerkleItem;
            }
        }
        async_response_asyncGetBatchTxsByHashList(
            current, toTarsError(error), tarsTxs, tarsMerkleProofs);
    }(m_ledger, std::move(hashList), _withProof, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockDataByNumber(tars::Int64 _blockNumber,
    tars::Int64 _blockFlag, bcostars::Block&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger,
                   bcos::protocol::BlockNumber blockNumber, int32_t blockFlag,
                   tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::Error::Ptr error;
        bcos::protocol::Block::Ptr block;
        try
        {
            block = co_await ledger->getBlockDataByNumber(blockNumber, blockFlag);
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }

        bcostars::Block tarsBlock;
        if (block)
        {
            tarsBlock = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(block)->inner();
        }
        async_response_asyncGetBlockDataByNumber(current, toTarsError(error), tarsBlock);
    }(m_ledger, _blockNumber, _blockFlag, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockHashByNumber(
    tars::Int64 _blockNumber, std::vector<tars::Char>&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    task::wait(
        [](bcos::ledger::LedgerInterface::Ptr ledger, bcos::protocol::BlockNumber blockNumber,
            tars::TarsCurrentPtr current) -> task::Task<void> {
            bcos::Error::Ptr error;
            bcos::crypto::HashType blockHash;
            try
            {
                blockHash = co_await ledger->getBlockHashByNumber(blockNumber);
            }
            catch (bcos::Error& e)
            {
                error = std::make_shared<bcos::Error>(std::move(e));
            }

            if (error)
            {
                async_response_asyncGetBlockHashByNumber(
                    current, toTarsError(error), std::vector<tars::Char>());
                co_return;
            }
            std::vector<tars::Char> hashBytes(blockHash.begin(), blockHash.end());
            async_response_asyncGetBlockHashByNumber(current, toTarsError(error), hashBytes);
        }(m_ledger, _blockNumber, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockNumber(tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger,
                   tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::protocol::BlockNumber blockNumber = -1;
        bcos::Error::Ptr error;
        try
        {
            blockNumber = co_await ledger->getBlockNumber();
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }
        async_response_asyncGetBlockNumber(current, toTarsError(error), blockNumber);
    }(m_ledger, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockNumberByHash(
    const std::vector<tars::Char>& _blockHash, tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    bcos::crypto::HashType blockHash;
    if (_blockHash.size() >= bcos::crypto::HashType::SIZE)
    {
        blockHash = bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(_blockHash.data()), bcos::crypto::HashType::SIZE);
    }
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger, bcos::crypto::HashType blockHash,
                   tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::protocol::BlockNumber blockNumber = -1;
        bcos::Error::Ptr error;
        try
        {
            blockNumber = co_await ledger->getBlockNumberByHash(std::move(blockHash));
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }
        async_response_asyncGetBlockNumberByHash(current, toTarsError(error), blockNumber);
    }(m_ledger, blockHash, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetNodeListByType(
    const std::string& _type, std::vector<bcostars::ConsensusNode>&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger, std::string type,
                   tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::Error::Ptr error;
        bcos::consensus::ConsensusNodeList nodeList;
        try
        {
            nodeList = co_await ledger->getNodeListByType(type);
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }
        async_response_asyncGetNodeListByType(
            current, toTarsError(error), toTarsConsensusNodeList(nodeList));
    }(m_ledger, _type, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetSystemConfigByKey(
    const std::string& _key, std::string&, tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger, std::string key,
                   tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::ledger::SystemConfigEntry config{"", -1};
        bcos::Error::Ptr error;
        try
        {
            config = co_await ledger->getSystemConfigByKey(key);
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }
        async_response_asyncGetSystemConfigByKey(
            current, toTarsError(error), std::get<0>(config), std::get<1>(config));
    }(m_ledger, _key, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetTotalTransactionCount(
    tars::Int64&, tars::Int64&, tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger,
                   tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::ledger::TransactionCountResult count{-1, -1, -1};
        bcos::Error::Ptr error;
        try
        {
            count = co_await ledger->getTotalTransactionCount();
        }
        catch (bcos::Error& e)
        {
            error = std::make_shared<bcos::Error>(std::move(e));
        }
        async_response_asyncGetTotalTransactionCount(current, toTarsError(error),
            std::get<0>(count), std::get<1>(count), std::get<2>(count));
    }(m_ledger, current));
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetTransactionReceiptByHash(
    const std::vector<tars::Char>& _txHash, tars::Bool _withProof, bcostars::TransactionReceipt&,
    std::vector<std::string>&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    bcos::crypto::HashType txHash;
    if (_txHash.size() >= bcos::crypto::HashType::SIZE)
    {
        txHash = bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(_txHash.data()), bcos::crypto::HashType::SIZE);
    }

    task::wait([](bcos::ledger::LedgerInterface::Ptr ledger, bcos::crypto::HashType txHash,
                   bool withProof, tars::TarsCurrentPtr current) -> task::Task<void> {
        bcos::ledger::TransactionReceiptResult result;
        try
        {
            result = co_await ledger->getTransactionReceiptByHash(std::move(txHash), withProof);
        }
        catch (bcos::Error& e)
        {
            result.error = std::make_shared<bcos::Error>(std::move(e));
        }

        bcostars::TransactionReceipt tarsReceipt;
        if (result.receipt)
        {
            auto mutableReceipt =
                std::const_pointer_cast<bcos::protocol::TransactionReceipt>(result.receipt);
            tarsReceipt = std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(
                mutableReceipt)
                              ->inner();
        }
        std::vector<std::string> tarsMerkleItemList;
        if (result.proof)
        {
            for (auto const& merkle : *result.proof)
            {
                tarsMerkleItemList.emplace_back(merkle.hex());
            }
        }
        async_response_asyncGetTransactionReceiptByHash(
            current, toTarsError(result.error), tarsReceipt, tarsMerkleItemList);
    }(m_ledger, txHash, _withProof, current));
    return bcostars::Error();
}
