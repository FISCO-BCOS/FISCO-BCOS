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
#include "../Executive.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage2/Storage2.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

namespace bcos::transaction_executor
{

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

template <storage2::Storage Storage>
class HostContext : public evmc_host_context
{
public:
    /// Full constructor.
    HostContext(const protocol::BlockHeader& blockHeader, const evmc_message* message,
        Storage& storage, std::string tableName)
      : m_message(message), m_storage(storage), m_tableName(std::move(tableName))
    {
        // Set the interfaces

        hash_fn = evm_hash_fn;
        // version = m_executive->blockContext().lock()->blockVersion();
        isSMCrypto = false;
        if (GlobalHashImpl::g_hashImpl &&
            GlobalHashImpl::g_hashImpl->getHashImplType() == crypto::HashImplType::Sm3Hash)
        {
            isSMCrypto = true;
        }

        constexpr static evmc_gas_metrics ethMetrics{32000, 20000, 5000, 200, 9000, 2300, 25000};
        metrics = &ethMetrics;
    }
    virtual ~HostContext() noexcept = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;
    HostContext(HostContext&&) = delete;
    HostContext& operator=(HostContext&&) = delete;

    // Read storage location.
    evmc_bytes32 store(const evmc_bytes32* key)
    {
        auto keyView = std::string_view((char*)key->bytes, sizeof(key->bytes));

        auto entry = task::syncWait(
            m_storage.getRow(m_tableName, keyView));  // Use syncWait because evm doesn't know task

        evmc_bytes32 result;
        if (entry)
        {
            auto field = entry->getField(0);
            std::uninitialized_copy_n(field.data(), sizeof(result), result.bytes);
        }
        else
        {
            std::uninitialized_fill_n(result.bytes, sizeof(result), 0);
        }
        return result;
    }

    // Write a value in storage.
    void setStore(const evmc_bytes32* key, const evmc_bytes32* value)
    {
        auto keyView = std::string_view((char*)key->bytes, sizeof(key->bytes));
        bytes valueBytes(value->bytes, value->bytes + sizeof(value->bytes));

        storage::Entry entry;
        entry.importFields({std::move(valueBytes)});
        task::syncWait(m_storage.setRow(
            m_tableName, keyView, std::move(entry)));  // Use syncWait because evm doesn't know task
    }

    // call
    evmc_result call(const evmc_message* message)
    {
        auto result = m_externalCall(message);

        // TODO: Store the log
        return result;
    }

    evmc_result callBuiltInPrecompiled(const evmc_message* message, bool _isEvmPrecompiled)
    {
        //
    }

    void setCode(crypto::HashType codeHash, bytes code)
    {
        storage::Entry codeHashEntry;
        codeHashEntry.importFields({std::move(codeHash)});

        storage::Entry codeEntry;
        codeEntry.importFields({std::move(code)});
        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            // Query the code table first
            auto oldCodeEntry =
                m_storage.getRow(bcos::ledger::SYS_CODE_BINARY, codeHashEntry.get());
            if (!oldCodeEntry)
            {
                m_storage.setRow(bcos::ledger::SYS_CODE_BINARY, codeHash, std::move(codeEntry));
            }
            m_storage.setRow(m_tableName, ACCOUNT_CODE_HASH, std::move(codeHashEntry));

            return;
        }

        // old logic
        m_storage.setRow(m_tableName, ACCOUNT_CODE_HASH, std::move(codeHashEntry));
        m_storage.setRow(m_tableName, ACCOUNT_CODE, std::move(codeEntry));
    }
    void setCode(bytes code) { setCode(GlobalHashImpl::g_hashImpl->hash(code), std::move(code)); }
    void setCodeAndABI(bytes code, std::string abi)
    {
        auto codeHash = GlobalHashImpl::g_hashImpl->hash(code);
        setCode(codeHash, std::move(code));

        storage::Entry abiEntry;
        abiEntry.importFields({std::move(abi)});
        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            auto oldABIEntry = m_storage.getRow(bcos::ledger::SYS_CONTRACT_ABI, codeHash);
            if (oldABIEntry)
            {
                m_storage.setRow(bcos::ledger::SYS_CONTRACT_ABI, codeHash, std::move(abiEntry));
            }

            return;
        }
        m_storage.setRow(m_tableName, ACCOUNT_ABI, std::move(abiEntry));
    }

    size_t codeSizeAt(std::string_view address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: Check is precompiled
            std::string contractAddress(address);  // TODO: add address prefix
            auto codeEntry = m_storage.getRow(contractAddress, bcos::ledger::SYS_CODE_BINARY);
            if (codeEntry)
            {
                return codeEntry->length();
            }
            return 0;
        }
        return 1;
    }

    h256 codeHashAt(std::string_view address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: check is precompiled
            std::string contractAddress(address);  // TODO: add address prefix
            auto codeHashEntry = m_storage.getRow(contractAddress, ACCOUNT_CODE_HASH);
            if (codeHashEntry)
            {
                auto view = codeHashEntry->get();
                h256 codeHash(view.begin(), view.end());
                return codeHash;
            }
        }
        return {};
    }

    /// Does the account exist?
    bool exists(const std::string_view&) { return true; }

    /// Return the EVM gas-price schedule for this execution context.
    VMSchedule const& vmSchedule() const {}

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    h256 blockHash(int64_t number) const
    {
        // TODO: use depends on scheduler
        return m_blockHash(number);
    }
    int64_t blockNumber() const { return m_blockHeader.number(); }
    uint32_t blockVersion() const { return m_blockHeader.version(); }
    uint64_t timestamp() const { return m_blockHeader.timestamp(); }
    int64_t blockGasLimit() const
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // FISCO BCOS only has tx Gas limit. We use it as block gas limit
            return m_executive->blockContext().lock()->txGasLimit();
        }
        return 3000000000;  // TODO: add config
    }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data);

    /// ------ get interfaces related to HostContext------
    std::string_view myAddress() const;
    std::string_view caller() const { return fromEvmC(m_message->sender); }  // no call?
    std::string_view origin() const { return m_origin; }
    std::string_view codeAddress() const { return m_origin; }  // no call?
    bytesConstRef data() const { return {m_message->input_data, m_message->input_size}; }

    std::optional<storage::Entry> code() { return {}; }
    h256 codeHash() const;

    bool isCodeHasPrefix(std::string_view _prefix) const;
    u256 salt() const { return fromEvmC(m_message->create2_salt); }
    bool isCreate() const
    {  // TODO: check to
    }
    bool staticCall() const { return m_callParameters->staticCall; }
    int64_t gas() const { return m_callParameters->gas; }
    void suicide()
    {
        if (m_executive->blockContext().lock()->blockVersion() >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            m_executive->blockContext().lock()->suicide(m_tableName);
        }
    }

    bool isWasm();
    const evmc_message* getMessage() const { return m_message; }

private:
    const protocol::BlockHeader& m_blockHeader;
    const evmc_message* m_message;
    Storage& m_storage;
    std::string m_tableName;
    std::string m_origin;

    std::function<evmc_result(const evmc_message*)> m_externalCall;
    std::function<h256(int64_t)> m_blockHash;
};

}  // namespace bcos::transaction_executor
