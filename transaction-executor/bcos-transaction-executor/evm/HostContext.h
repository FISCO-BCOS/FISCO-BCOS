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

#include "../CallParameters.h"
#include "../Common.h"
#include "../Executive.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-framework/storage2/Storage2.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

namespace bcos::transaction_executor
{

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    // TODO: add hash impl
    // return toEvmC(HostContext::hashImpl()->hash(bytesConstRef(data, size)));
    return {};
}

template <class CallParametersType, storage2::Storage Storage>
class HostContext : public evmc_host_context
{
public:
    using UniquePtr = std::unique_ptr<HostContext>;
    using UniqueConstPtr = std::unique_ptr<const HostContext>;

    /// Full constructor.
    HostContext(CallParametersType& callParameters, Storage& storage, std::string tableName)
      : m_callParameters(callParameters), m_storage(storage), m_tableName(std::move(tableName))
    {
        hash_fn = evm_hash_fn;
        // version = m_executive->blockContext().lock()->blockVersion();
        isSMCrypto = false;
        if (hashImpl() && hashImpl()->getHashImplType() == crypto::HashImplType::Sm3Hash)
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

    /// Read storage location.
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

    /// Write a value in storage.
    // void setStore(const u256& _n, const u256& _v);
    void setStore(const evmc_bytes32* key, const evmc_bytes32* value)
    {
        auto keyView = std::string_view((char*)key->bytes, sizeof(key->bytes));
        bytes valueBytes(value->bytes, value->bytes + sizeof(value->bytes));

        storage::Entry entry;
        entry.importFields({std::move(valueBytes)});
        task::syncWait(m_storage.setRow(
            m_tableName, keyView, std::move(entry)));  // Use syncWait because evm doesn't know task
    }

    /// Create a new contract.
    evmc_result externalRequest(const evmc_message* _msg)
    {
        // Convert evmc_message to CallParameters
        auto request = std::make_unique<CallParameters>(CallParameters::MESSAGE);

        request->senderAddress = myAddress();
        request->origin = origin();
        request->status = 0;

        switch (_msg->kind)
        {
        case EVMC_CREATE2:
            request->createSalt = fromEvmC(_msg->create2_salt);
            break;
        case EVMC_CALL:
            if (m_executive->blockContext().lock()->isWasm())
            {
                request->receiveAddress.assign((char*)_msg->destination_ptr, _msg->destination_len);
            }
            else
            {
                request->receiveAddress = evmAddress2String(_msg->destination);
            }

            request->codeAddress = request->receiveAddress;
            request->data.assign(_msg->input_data, _msg->input_data + _msg->input_size);
            break;
        case EVMC_DELEGATECALL:
        case EVMC_CALLCODE:
        {
            if (!m_executive->blockContext().lock()->isWasm())
            {
                if (blockContext->blockVersion() >=
                    (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
                {
                    request->delegateCall = true;
                    request->codeAddress = evmAddress2String(_msg->destination);
                    request->delegateCallSender = evmAddress2String(_msg->sender);
                    request->receiveAddress = codeAddress();
                    request->data.assign(_msg->input_data, _msg->input_data + _msg->input_size);
                    break;
                }
            }

            // old logic
            evmc_result result;
            result.status_code = evmc_status_code(EVMC_INVALID_INSTRUCTION);
            result.release = nullptr;  // no output to release
            result.gas_left = 0;
            return result;
        }
        case EVMC_CREATE:
            request->data.assign(_msg->input_data, _msg->input_data + _msg->input_size);
            request->create = true;
            break;
        }
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            request->logEntries = std::move(m_callParameters->logEntries);
        }
        request->gas = _msg->gas;
        // if (built in precompiled) then execute locally

        if (m_executive->isBuiltInPrecompiled(request->receiveAddress))
        {
            return callBuiltInPrecompiled(request, false);
        }
        if (!blockContext->isWasm() && m_executive->isEthereumPrecompiled(request->receiveAddress))
        {
            return callBuiltInPrecompiled(request, true);
        }

        request->staticCall = m_callParameters->staticCall;

        auto response = m_executive->externalCall(std::move(request));

        // Convert CallParameters to evmc_resultx
        evmc_result result{.status_code = toEVMStatus(response, *blockContext),
            .gas_left = response->gas,
            .output_data = response->data.data(),
            .output_size = response->data.size(),
            .release = nullptr,  // TODO: check if the response data need to release
            .create_address = toEvmC(boost::algorithm::unhex(response->newEVMContractAddress)),
            .padding = {}};

        // Put response to store in order to avoid data lost
        m_responseStore.emplace_back(std::move(response));

        return result;
    }

    evmc_status_code toEVMStatus(
        CallParametersType const& response, const BlockContext& blockContext);

    evmc_result callBuiltInPrecompiled(CallParametersType const& _request, bool _isEvmPrecompiled);

    bool setCode(bytes code);

    void setCodeAndAbi(bytes code, std::string abi);

    size_t codeSizeAt(std::string_view address);

    h256 codeHashAt(std::string_view address);

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
        if (m_executive->blockContext().lock()->blockVersion() >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // FISCO BCOS only has tx Gas limit. We use it as block gas limit
            return m_executive->blockContext().lock()->txGasLimit();
        }
        else
        {
            return 3000000000;  // TODO: add config
        }
    }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data);

    /// ------ get interfaces related to HostContext------
    std::string_view myAddress() const;
    virtual std::string_view caller() const { return m_callParameters->senderAddress; }
    std::string_view origin() const { return m_callParameters->origin; }
    std::string_view codeAddress() const { return m_callParameters->codeAddress; }
    bytesConstRef data() const { return ref(m_callParameters->data); }
    virtual std::optional<storage::Entry> code();
    bool isCodeHasPrefix(std::string_view _prefix) const;
    h256 codeHash();
    u256 salt() const { return m_salt; }
    SubState& sub() { return m_sub; }
    bool isCreate() const { return m_callParameters->create; }
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

    CallParametersType takeCallParameters()
    {
        if (m_executive->blockContext().lock()->blockVersion() >=
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

protected:
    const CallParametersType& getCallParameters() const { return m_callParameters; }
    virtual bcos::bytes externalCodeRequest(const std::string_view& address);

private:
    void depositFungibleAsset(
        const std::string_view& _to, const std::string& _assetName, uint64_t _amount);
    void depositNotFungibleAsset(const std::string_view& _to, const std::string& _assetName,
        uint64_t _assetID, const std::string& _uri);

    CallParametersType& m_callParameters;
    Storage& m_storage;
    std::string m_tableName;

    u256 m_salt;     ///< Values used in new address construction by CREATE2
    SubState m_sub;  ///< Sub-band VM state (suicides, refund counter, logs).

    std::list<CallParametersType> m_responseStore;
};

}  // namespace bcos::transaction_executor
