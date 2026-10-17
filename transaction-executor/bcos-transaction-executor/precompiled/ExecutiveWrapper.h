/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file ExecutiveWrapper.h
 */

#pragma once
#include "../EVMCResult.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/vm/HostContext.h"
#include <bcos-utilities/BoostLog.h>
#include <evmc/evmc.h>
#include <memory>

#define EXECUTIVE_WRAPPER(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTIVE_WRAPPER")

namespace bcos::executor_v1
{

inline std::shared_ptr<precompiled::Precompiled> getInnerPrecompiled(auto const& precompiled)
{
    return std::get<std::shared_ptr<precompiled::Precompiled>>(precompiled.m_precompiled);
}

template <class T>
concept ExternalCaller = std::is_invocable_r_v<EVMCResult, T, const evmc_message&>;

template <ExternalCaller Caller, class PrecompiledManager>
class ExecutiveWrapper : public executor::TransactionExecutive
{
private:
    std::unique_ptr<executor::BlockContext> m_blockContext;
    Caller m_externalCaller;
    PrecompiledManager const& m_precompiledManager;
    ledger::Features m_features;

public:
    ExecutiveWrapper(std::unique_ptr<executor::BlockContext> blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq, Caller externalCaller,
        PrecompiledManager const& precompiledManager, ledger::Features features)
      : executor::TransactionExecutive(*blockContext, std::move(contractAddress), contextID, seq),
        m_blockContext(std::move(blockContext)),
        m_externalCaller(std::move(externalCaller)),
        m_precompiledManager(precompiledManager),
        m_features(std::move(features))
    {}

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(std::string_view _address,
        uint32_t version, bool isAuth, const ledger::Features& features) const override
    {
        auto addressBytes = fromHex(_address);
        auto address = fromBigEndian<u160>(addressBytes);
        const auto* precompiled =
            m_precompiledManager.getPrecompiled(address.convert_to<unsigned long>(), features);
        if (precompiled == nullptr)
        {
            return nullptr;
        }

        return getInnerPrecompiled(*precompiled);
    }

    bool isPrecompiled(const std::string& _address) const override
    {
        auto evmcAddr = unhexAddress(_address);
        return m_precompiledManager.getPrecompiled(evmcAddr) != nullptr;
    }

    executor::CallParameters::UniquePtr externalCall(
        executor::CallParameters::UniquePtr input) override
    {
        if (input->internalCreate)
        {
            if (input->codeAddress.empty())
            {
                input->codeAddress = bcos::newEVMAddress(
                    m_hashImpl, m_blockContext->number(), m_contextID, seq() + 1);
                input->receiveAddress = input->codeAddress;
                // Publish the consumed base (review F6), gated on bit 66: a second
                // internalCreate through this wrapper must derive from the NEXT value.
                // Nothing else advances this counter on the wrapper path — the EVM lane's
                // ++m_seq lives in the v2 HostContext, and each dispatch receives this
                // executive's seq by value — so two creates through one wrapper used to
                // derive f(seq+1) twice and land both stubs on one address. Off, the
                // historical snapshot semantics is kept so historical blocks replay
                // byte-identically.
                if (m_features.get(ledger::Features::Flag::bugfix_internal_create_seq))
                {
                    m_seq = seq() + 1;
                }
            }
            EXECUTIVE_WRAPPER(TRACE) << "codeAddress:" << input->codeAddress;
            auto [hostContext, callResults] = create(std::move(input));
            return std::move(callResults);
        }

        evmc_message evmcMessage{.kind = input->create ? EVMC_CREATE : EVMC_CALL,
            .flags = input->staticCall ? static_cast<uint32_t>(EVMC_STATIC) : 0,
            .depth = 0,
            .gas = input->gas,
            .state_gas = {},
            .recipient = unhexAddress(input->receiveAddress),
            .sender = unhexAddress(input->senderAddress),
            .input_data = input->data.data(),
            .input_size = input->data.size(),
            .value = toEvmC(0x0_cppui256),
            .code_address = unhexAddress(input->codeAddress),
            .code = nullptr,
            .code_size = 0};

        struct InternalCallParams
        {
            std::string precompiledContract;
            bcos::bytes precompiledInput;
        };
        std::optional<InternalCallParams> internalCallParams;
        if (input->internalCall)
        {
            internalCallParams.emplace();
            auto& [precompiledContract, precompiledInput] = *internalCallParams;
            CodecWrapper codec(m_blockContext->hashHandler());
            codec.decode(ref(input->data), precompiledContract, precompiledInput);
            evmcMessage.code_address = unhexAddress(precompiledContract);
            evmcMessage.input_data = precompiledInput.data();
            evmcMessage.input_size = precompiledInput.size();
        }
        auto result = m_externalCaller(evmcMessage);

        auto callResult =
            std::make_unique<executor::CallParameters>(executor::CallParameters::FINISHED);
        callResult->evmStatus = result.status_code;
        callResult->gas = result.gas_left;
        callResult->data.assign(result.output_data, result.output_data + result.output_size);

        const bool applyBugfix =
            m_blockContext->features().get(ledger::Features::Flag::bugfix_v1_error_handling);
        if (applyBugfix)
        {
            callResult->status = static_cast<int32_t>(result.status);
            if (result.status_code != 0 && result.output_data != nullptr && result.output_size > 0)
            {
                callResult->message.assign(reinterpret_cast<const char*>(result.output_data),
                    reinterpret_cast<const char*>(result.output_data) + result.output_size);
            }
        }
        else
        {
            callResult->status = result.status_code;
            // Legacy (pre-bugfix_v1_error_handling) branch: error text used to ride
            // back through evmc_result::create_address reinterpreted as ErrorMessage*.
            // That field is gone in evmc ABI 19 and no producer of the pointer exists
            // in-tree, so this branch never obtained a message anyway (buffer was
            // always null) — keep the equivalent no-message behavior.
        }

        return callResult;
    }
};

inline auto buildLegacyExecutive(auto& storage, protocol::BlockHeader const& blockHeader,
    std::string contractAddress, ExternalCaller auto externalCaller, auto const& precompiledManager,
    int64_t contextID, int64_t seq, bool authCheck, ledger::Features features)
{
    auto storageWrapper =
        std::make_shared<storage::LegacyStateStorageWrapper<std::decay_t<decltype(storage)>>>(
            storage);

    auto blockContext = std::make_unique<executor::BlockContext>(
        storageWrapper, nullptr, executor::GlobalHashImpl::g_hashImpl, blockHeader, authCheck);
    return std::make_shared<
        ExecutiveWrapper<decltype(externalCaller), std::decay_t<decltype(precompiledManager)>>>(
        std::move(blockContext), std::move(contractAddress), contextID, seq,
        std::move(externalCaller), precompiledManager, std::move(features));
}
}  // namespace bcos::executor_v1
