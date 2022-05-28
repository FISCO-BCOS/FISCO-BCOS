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
 * @file LedgerServiceServer.h
 * @author: yujiechen
 * @date 2021-10-18
 */
#pragma once
#include <bcos-framework//ledger/LedgerInterface.h>
#include <bcos-tars-protocol/tars/LedgerService.h>

namespace bcostars
{
struct LedgerServiceParam
{
    bcos::ledger::LedgerInterface::Ptr ledger;
};
class LedgerServiceServer : public LedgerService
{
public:
    LedgerServiceServer(LedgerServiceParam const& _param) : m_ledger(_param.ledger) {}
    ~LedgerServiceServer() override {}

    void initialize() override {}
    void destroy() override {}

    bcostars::Error asyncGetBatchTxsByHashList(const vector<vector<tars::Char> >& _txsHashList,
        tars::Bool _withProof, vector<bcostars::Transaction>& _transactions,
        map<std::string, vector<bcostars::MerkleProofItem> >& _merkleProofList,
        tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetBlockDataByNumber(tars::Int64 _blockNumber, tars::Int64 _blockFlag,
        bcostars::Block& _block, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetBlockHashByNumber(tars::Int64 _blockNumber,
        vector<tars::Char>& _blockHash, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetBlockNumber(
        tars::Int64& _blockNumber, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetBlockNumberByHash(const vector<tars::Char>& _blockHash,
        tars::Int64& _blockNumber, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetNodeListByType(const std::string& _type,
        vector<bcostars::ConsensusNode>& _nodeList, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetSystemConfigByKey(const std::string& _key, std::string& _value,
        tars::Int64& _blockNumber, tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetTotalTransactionCount(tars::Int64& _totalTxCount,
        tars::Int64& _failedTxCount, tars::Int64& _latestBlockNumber,
        tars::TarsCurrentPtr current) override;

    bcostars::Error asyncGetTransactionReceiptByHash(const vector<tars::Char>& _txHash,
        tars::Bool _withProof, bcostars::TransactionReceipt& _receipt,
        vector<bcostars::MerkleProofItem>& _proof, tars::TarsCurrentPtr current) override;

private:
    bcos::ledger::LedgerInterface::Ptr m_ledger;
};
}  // namespace bcostars