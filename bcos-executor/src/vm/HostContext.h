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
 * @brief host context
 * @file HostContext.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../Common.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/storage/Table.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

namespace bcos
{
namespace executor
{
class TransactionExecutive;

class HostContext : public evmc_host_context
{
public:
    using UniquePtr = std::unique_ptr<HostContext>;
    using UniqueConstPtr = std::unique_ptr<const HostContext>;

    /// Full constructor.
    HostContext(CallParameters::UniquePtr callParameters,
        std::shared_ptr<TransactionExecutive> executive, std::string tableName);
    virtual ~HostContext() noexcept = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;
    HostContext(HostContext&&) = delete;
    HostContext& operator=(HostContext&&) = delete;

    std::string get(const std::string_view& _key);

    void set(const std::string_view& _key, std::string _value);

    /// Read storage location.
    evmc_bytes32 store(const evmc_bytes32* key);
    evmc_bytes32 getTransientStorage(const evmc_bytes32* key);

    /// Write a value in storage.
    // void setStore(const u256& _n, const u256& _v);
    void setStore(const evmc_bytes32* key, const evmc_bytes32* value);
    void setTransientStorage(const evmc_bytes32* key, const evmc_bytes32* value);

    /// Create a new contract.
    evmc_result externalRequest(const evmc_message* _msg);

    evmc_status_code toEVMStatus(
        std::unique_ptr<CallParameters> const& response, const BlockContext& blockContext);

    evmc_result callBuiltInPrecompiled(
        std::unique_ptr<CallParameters> const& _request, bool _isEvmPrecompiled);

    virtual bool setCode(bytes code);

    void setCodeAndAbi(bytes code, std::string abi);

    size_t codeSizeAt(const std::string_view& address);

    h256 codeHashAt(const std::string_view& address);

    /// Does the account exist?
    bool exists(const std::string_view&) { return true; }

    /// Return the EVM gas-price schedule for this execution context.
    VMSchedule const& vmSchedule() const;

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    h256 blockHash(int64_t _number) const;
    int64_t blockNumber() const;
    uint32_t blockVersion() const;
    uint64_t timestamp() const;
    int64_t blockGasLimit() const
    {
        if (m_executive->blockContext().blockVersion() >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // FISCO BCOS only has tx Gas limit. We use it as block gas limit
            return m_executive->blockContext().txGasLimit();
        }
        else
        {
            return 3000000000;
        }
    }

    evmc_uint256be chainId() const { return m_executive->blockContext().ledgerCache()->chainId(); }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data);

    /// ------ get interfaces related to HostContext------
    virtual std::string_view myAddress() const;
    virtual std::string_view caller() const { return m_callParameters->senderAddress; }
    std::string_view origin() const { return m_callParameters->origin; }
    std::string_view codeAddress() const { return m_callParameters->codeAddress; }
    std::string_view receiveAddress() const { return m_callParameters->receiveAddress; }

    bytes_view data() const
    {
        return bytes_view(m_callParameters->data.data(), m_callParameters->data.size());
    }
    virtual std::optional<storage::Entry> code();
    virtual h256 codeHash();
    u256 salt() const { return m_salt; }
    SubState& sub() { return m_sub; }
    bool isCreate() const { return m_callParameters->create; }
    bool staticCall() const { return m_callParameters->staticCall; }
    int64_t gas() const { return m_callParameters->gas; }
    u256 gasPrice() const { return m_callParameters->gasPrice; }
    u256 value() const { return m_callParameters->value; }
    void suicide()
    {
        m_executive->setContractTableChanged();
        if (m_executive->blockContext().blockVersion() >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            auto& blockContext = const_cast<BlockContext&>(m_executive->blockContext());
            blockContext.suicide(m_tableName);
        }
    }

    evmc_bytes32 getBalance(const evmc_address* _addr);
    bool selfdestruct(const evmc_address* _addr, const evmc_address* _beneficiary);

    CallParameters::UniquePtr&& takeCallParameters()
    {
        if (m_executive->blockContext().blockVersion() >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            for (const auto& response : m_responseStore)
            {
                m_callParameters->logEntries.insert(m_callParameters->logEntries.end(),
                    std::make_move_iterator(response->logEntries.begin()),
                    std::make_move_iterator(response->logEntries.end()));
            }
        }
        return std::move(m_callParameters);
    }

    static crypto::Hash::Ptr& hashImpl() { return GlobalHashImpl::g_hashImpl; }

    bool isWasm();
    const std::shared_ptr<TransactionExecutive>& getTransactionExecutive() const
    {
        return m_executive;
    }

    bcos::bytes codeAt(const std::string_view& address) { return externalCodeRequest(address); }
    const bcos::ledger::Features& features() const
    {
        return m_executive->blockContext().features();
    }

    std::string getContractTableName(const std::string_view& _address)
    {
        return m_executive->getContractTableName(_address, isWasm(), isCreate());
    }

protected:
    const CallParameters::UniquePtr& getCallParameters() const { return m_callParameters; }
    virtual bcos::bytes externalCodeRequest(const std::string_view& address);

private:
    void depositFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    void depositNotFungibleAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _assetID, const std::string& _uri);

    CallParameters::UniquePtr m_callParameters;
    std::shared_ptr<TransactionExecutive> m_executive;
    std::string m_tableName;

    u256 m_salt;     ///< Values used in new address construction by CREATE2
    SubState m_sub;  ///< Sub-band VM state (suicides, refund counter, logs).

    std::list<CallParameters::UniquePtr> m_responseStore;
};

}  // namespace executor
}  // namespace bcos
