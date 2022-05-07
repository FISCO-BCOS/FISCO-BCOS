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
 * @brief executive of vm
 * @file TransactionExecutive.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "TransactionExecutive.h"
#include "../precompiled/FileSystemPrecompiled.h"
#include "../precompiled/extension/AuthManagerPrecompiled.h"
#include "../precompiled/extension/ContractAuthMgrPrecompiled.h"
#include "../vm/EVMHostInterface.h"
#include "../vm/HostContext.h"
#include "../vm/Precompiled.h"
#include "../vm/VMFactory.h"
#include "../vm/VMInstance.h"
#include "../vm/gas_meter/GasInjector.h"
#include "BlockContext.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/Exceptions.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-utilities/Common.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;
using namespace bcos::codec;
using namespace bcos::precompiled;

/// Error info for VMInstance status code.
using errinfo_evmcStatusCode = boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;

CallParameters::UniquePtr TransactionExecutive::start(CallParameters::UniquePtr input)
{
    m_pullMessage.emplace([this, inputPtr = input.release()](Coroutine::push_type& push) {
        COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq) << "Create new coroutine";

        // Take ownership from input
        m_pushMessage.emplace(std::move(push));

        auto callParameters = std::unique_ptr<CallParameters>(inputPtr);
        auto blockContext = m_blockContext.lock();
        if (!blockContext)
        {
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
        }

        m_storageWrapper = std::make_unique<SyncStorageWrapper>(blockContext->storage(),
            std::bind(&TransactionExecutive::externalAcquireKeyLocks, this, std::placeholders::_1),
            m_recoder);
        if (blockContext->lastStorage())
        {
            m_lastStorageWrapper = std::make_shared<SyncStorageWrapper>(
                std::dynamic_pointer_cast<bcos::storage::StateStorage>(blockContext->lastStorage()),
                std::bind(
                    &TransactionExecutive::externalAcquireKeyLocks, this, std::placeholders::_1),
                m_recoder);
        }

        if (!callParameters->keyLocks.empty())
        {
            m_storageWrapper->importExistsKeyLocks(callParameters->keyLocks);
        }

        m_exchangeMessage = execute(std::move(callParameters));
        // Execute is finished, erase the key locks
        m_exchangeMessage->keyLocks.clear();

        // Return the ownership to input
        push = std::move(*m_pushMessage);

        COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq) << "Finish coroutine executing";
    });

    return dispatcher();
}

CallParameters::UniquePtr TransactionExecutive::dispatcher()
{
    try
    {
        for (auto it = std::begin(*m_pullMessage); it != std::end(*m_pullMessage); ++it)
        {
            if (*it)
            {
                COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq)
                    << "Context switch to main coroutine to call func";
                (*it)(ResumeHandler(*this));
            }

            if (m_exchangeMessage)
            {
                COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq)
                    << "Context switch to main coroutine to return output";
                return std::move(m_exchangeMessage);
            }
        }
    }
    catch (std::exception& e)
    {
        COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq)
            << "Error while dispatch, " << boost::diagnostic_information(e);
        BOOST_THROW_EXCEPTION(BCOS_ERROR_WITH_PREV(-1, "Error while dispatch", e));
    }

    COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq) << "Context switch to main coroutine, Finished!";
    return std::move(m_exchangeMessage);
}

CallParameters::UniquePtr TransactionExecutive::externalCall(CallParameters::UniquePtr input)
{
    input->keyLocks = m_storageWrapper->exportKeyLocks();

    spawnAndCall([this, inputPtr = input.release()](
                     ResumeHandler) { m_exchangeMessage = CallParameters::UniquePtr(inputPtr); });

    // When resume, exchangeMessage set to output
    auto output = std::move(m_exchangeMessage);

    // After coroutine switch, set the recoder
    m_storageWrapper->setRecoder(m_recoder);

    // Set the keyLocks
    m_storageWrapper->importExistsKeyLocks(output->keyLocks);

    return output;
}

void TransactionExecutive::externalAcquireKeyLocks(std::string acquireKeyLock)
{
    EXECUTOR_LOG(TRACE) << "Executor acquire key lock: " << acquireKeyLock;

    auto callParameters = std::make_unique<CallParameters>(CallParameters::KEY_LOCK);
    callParameters->senderAddress = m_contractAddress;
    callParameters->keyLocks = m_storageWrapper->exportKeyLocks();
    callParameters->acquireKeyLock = std::move(acquireKeyLock);

    spawnAndCall([this, inputPtr = callParameters.release()](
                     ResumeHandler) { m_exchangeMessage = CallParameters::UniquePtr(inputPtr); });

    // After coroutine switch, set the recoder, before the exception throw
    m_storageWrapper->setRecoder(m_recoder);

    auto output = std::move(m_exchangeMessage);
    if (output->type == CallParameters::REVERT)
    {
        // Deadlock, revert
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            ExecuteError::DEAD_LOCK, "Dead lock detected, revert transaction: " +
                                         boost::lexical_cast<std::string>(output->type)));
    }

    // Set the keyLocks
    m_storageWrapper->importExistsKeyLocks(output->keyLocks);
}

CallParameters::UniquePtr TransactionExecutive::execute(CallParameters::UniquePtr callParameters)
{
    m_storageWrapper->setRecoder(m_recoder);

    std::unique_ptr<HostContext> hostContext;
    CallParameters::UniquePtr callResults;
    if (callParameters->create)
    {
        std::tie(hostContext, callResults) = create(std::move(callParameters));
    }
    else
    {
        std::tie(hostContext, callResults) = call(std::move(callParameters));
    }

    if (hostContext)
    {
        callResults = go(*hostContext, std::move(callResults));

        // TODO: check if needed
        hostContext->sub().refunds +=
            hostContext->vmSchedule().suicideRefundGas * hostContext->sub().suicides.size();
    }

    return callResults;
}

std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> TransactionExecutive::call(
    CallParameters::UniquePtr callParameters)
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }

    if (isPrecompiled(callParameters->codeAddress) || callParameters->internalCall)
    {
        return callPrecompiled(std::move(callParameters));
    }
    auto tableName = getContractTableName(callParameters->codeAddress, blockContext->isWasm());
    // check permission first
    if (blockContext->isAuthCheck())
    {
        if (!checkContractAvailable(callParameters))
        {
            revert();
            callParameters->status = (int32_t)TransactionStatus::ContractFrozen;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Contract is frozen";
            callParameters->data.clear();
            EXECUTIVE_LOG(ERROR) << callParameters->message << LOG_KV("tableName", tableName)
                                 << LOG_KV("origin", callParameters->origin);
            return {nullptr, std::move(callParameters)};
        }
        if (!checkAuth(callParameters, false))
        {
            revert();
            callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Call permission denied";
            callParameters->data.clear();
            EXECUTIVE_LOG(ERROR) << callParameters->message << LOG_KV("tableName", tableName)
                                 << LOG_KV("origin", callParameters->origin);
            return {nullptr, std::move(callParameters)};
        }
    }
    auto hostContext = make_unique<HostContext>(
        std::move(callParameters), shared_from_this(), std::move(tableName));
    return {std::move(hostContext), nullptr};
}

std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr>
TransactionExecutive::callPrecompiled(CallParameters::UniquePtr callParameters)
{
    callParameters->type = CallParameters::FINISHED;
    try
    {
        std::shared_ptr<PrecompiledExecResult> precompiledExecResult;
        if (callParameters->internalCall)
        {
            std::string contract;
            bytes data;
            auto blockContext = m_blockContext.lock();
            auto codec =
                std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
            codec->decode(ref(callParameters->data), contract, data);
            precompiledExecResult = execPrecompiled(contract, ref(data), callParameters->origin,
                callParameters->senderAddress, callParameters->gas);
        }
        else
        {
            precompiledExecResult =
                execPrecompiled(callParameters->codeAddress, ref(callParameters->data),
                    callParameters->origin, callParameters->senderAddress, callParameters->gas);
        }

        auto gas = precompiledExecResult->m_gas;
        if (callParameters->gas < gas)
        {
            callParameters->type = CallParameters::REVERT;
            callParameters->status = (int32_t)TransactionStatus::OutOfGas;
            return {nullptr, std::move(callParameters)};
        }
        callParameters->gas -= gas;
        callParameters->status = (int32_t)TransactionStatus::None;
        callParameters->data = std::move(precompiledExecResult->m_execResult);
    }
    catch (protocol::PrecompiledError const& e)
    {
        // Note: considering the scenario where the contract calls the contract, the error message
        // still needs to be written to the output
        writeErrInfoToOutput(e.what(), *callParameters);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::PrecompiledError;
        callParameters->message = e.what();
    }
    catch (Exception const& e)
    {
        writeErrInfoToOutput(e.what(), *callParameters);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)executor::toTransactionStatus(e);
        callParameters->message = e.what();
    }
    catch (std::exception& e)
    {
        // Note: Since the information of std::exception may be affected by the version of the c++
        // library, in order to ensure compatibility, the information is not written to output
        writeErrInfoToOutput("InternalPrecompiledError", *callParameters);
        EXECUTIVE_LOG(WARNING) << LOG_DESC("callPrecompiled")
                               << LOG_KV("error", boost::diagnostic_information(e));
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::Unknown;
        callParameters->message = e.what();
    }
    return {nullptr, std::move(callParameters)};
}

std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> TransactionExecutive::create(
    CallParameters::UniquePtr callParameters)
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }
    auto newAddress = string(callParameters->codeAddress);
    auto tableName = getContractTableName(newAddress, blockContext->isWasm());
    auto extraData = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    extraData->abi = std::move(callParameters->abi);

    EXECUTIVE_LOG(DEBUG) << LOG_DESC("deploy") << LOG_KV("tableName", tableName)
                         << LOG_KV("abi len", extraData->abi.size())
                         << LOG_KV("internalCreate", callParameters->internalCreate);
    if (callParameters->internalCreate)
    {
        return {nullptr, internalCreate(std::move(callParameters))};
    }

    // check permission first
    if (blockContext->isAuthCheck())
    {
        if (!checkAuth(callParameters, true))
        {
            revert();
            callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Create permission denied";
            callParameters->create = false;
            EXECUTIVE_LOG(ERROR) << callParameters->message << LOG_KV("newAddress", newAddress)
                                 << LOG_KV("origin", callParameters->origin);
            return {nullptr, std::move(callParameters)};
        }
    }

    if (blockContext->isWasm())
    {
        // Liquid
        std::tuple<bytes, bytes> input;
        auto codec = std::make_shared<CodecWrapper>(blockContext->hashHandler(), true);
        codec->decode(ref(callParameters->data), input);
        auto& [code, params] = input;

        if (!hasWasmPreamble(code))
        {
            revert();

            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "the code is not wasm bytecode";
            return {nullptr, std::move(callResults)};
        }

        auto result = m_gasInjector->InjectMeter(code);
        if (result.status == wasm::GasInjector::Status::Success)
        {
            result.byteCode->swap(code);
        }
        else
        {
            revert();

            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "wasm bytecode invalid or use unsupported opcode";
            EXECUTIVE_LOG(ERROR) << callResults->message;
            return {nullptr, std::move(callResults)};
        }

        callParameters->data.swap(code);

        extraData->data = std::move(params);
    }
    else
    {
        // Solidity
        // Create the table
        try
        {
            m_storageWrapper->createTable(tableName, STORAGE_VALUE);
            EXECUTIVE_LOG(INFO) << "create contract table " << LOG_KV("table", tableName)
                                << LOG_KV("sender", callParameters->senderAddress);
            if (blockContext->isAuthCheck())
            {
                // Create auth table
                creatAuthTable(tableName, callParameters->origin, callParameters->senderAddress);
            }
        }
        catch (exception const& e)
        {
            revert();
            callParameters->status = (int32_t)TransactionStatus::ContractAddressAlreadyUsed;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = e.what();
            EXECUTIVE_LOG(ERROR) << LOG_DESC("createTable failed") << callParameters->message
                                 << LOG_KV("tableName", tableName);
            return {nullptr, std::move(callParameters)};
        }
    }
    auto hostContext =
        std::make_unique<HostContext>(std::move(callParameters), shared_from_this(), tableName);
    return {std::move(hostContext), std::move(extraData)};
}

CallParameters::UniquePtr TransactionExecutive::internalCreate(
    CallParameters::UniquePtr callParameters)
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }
    auto newAddress = string(callParameters->codeAddress);
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("internalCreate") << LOG_KV("newAddress", newAddress);
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName;
    std::string codeString;
    codec->decode(ref(callParameters->data), tableName, codeString);

    if (blockContext->isWasm())
    {
        /// BFS create contract table and write metadata in parent table
        if (!buildBfsPath(newAddress, callParameters->origin, newAddress, FS_TYPE_CONTRACT,
                callParameters->gas))
        {
            revert();
            auto buildCallResults = move(callParameters);
            buildCallResults->type = CallParameters::REVERT;
            buildCallResults->status = (int32_t)TransactionStatus::RevertInstruction;
            buildCallResults->message = "Error occurs in build BFS dir";
            EXECUTIVE_LOG(ERROR) << buildCallResults->message << LOG_KV("newAddress", newAddress);
            return buildCallResults;
        }
        /// set code field
        Entry entry = {};
        entry.importFields({codeString});
        m_storageWrapper->setRow(newAddress, ACCOUNT_CODE, std::move(entry));
    }
    else
    {
        /// BFS create link table and write metadata in parent table
        if (!buildBfsPath(
                tableName, callParameters->origin, newAddress, FS_TYPE_LINK, callParameters->gas))
        {
            revert();
            auto buildCallResults = move(callParameters);
            buildCallResults->type = CallParameters::REVERT;
            buildCallResults->status = (int32_t)TransactionStatus::RevertInstruction;
            buildCallResults->message = "Error occurs in build BFS dir";
            EXECUTIVE_LOG(ERROR) << buildCallResults->message << LOG_KV("newAddress", newAddress);
            return buildCallResults;
        }

        /// create code index contract
        auto codeAddress = getContractTableName(newAddress, false);
        m_storageWrapper->createTable(codeAddress, STORAGE_VALUE);

        /// set code field
        Entry entry = {};
        entry.importFields({codeString});
        m_storageWrapper->setRow(codeAddress, ACCOUNT_CODE, std::move(entry));

        /// set link data
        Entry addressEntry = {};
        addressEntry.importFields({newAddress});
        m_storageWrapper->setRow(tableName, FS_LINK_ADDRESS, std::move(addressEntry));
        Entry typeEntry = {};
        typeEntry.importFields({FS_TYPE_LINK});
        m_storageWrapper->setRow(tableName, FS_KEY_TYPE, std::move(typeEntry));
    }
    callParameters->type = CallParameters::FINISHED;
    callParameters->status = (int32_t)TransactionStatus::None;
    callParameters->internalCreate = false;
    callParameters->create = false;
    callParameters->data.clear();
    return callParameters;
}

CallParameters::UniquePtr TransactionExecutive::go(
    HostContext& hostContext, CallParameters::UniquePtr extraData)
{
    try
    {
        auto getEVMCMessage = [&extraData](const BlockContext& blockContext,
                                  const HostContext& hostContext) -> evmc_message {
            // the block number will be larger than 0,
            // can be controlled by the programmers
            if (!blockContext.isAuthCheck())
            {
                assert(blockContext.number() > 0);
            }

            evmc_call_kind kind = hostContext.isCreate() ? EVMC_CREATE : EVMC_CALL;
            uint32_t flags = hostContext.staticCall() ? EVMC_STATIC : 0;
            // this is ensured by solidity compiler
            assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
            auto leftGas = hostContext.gas();

            evmc_message evmcMessage;
            evmcMessage.kind = kind;
            evmcMessage.flags = flags;
            evmcMessage.depth = 0;  // depth own by scheduler
            evmcMessage.gas = leftGas;
            evmcMessage.value = toEvmC(h256(0));
            evmcMessage.create2_salt = toEvmC(0x0_cppui256);

            if (blockContext.isWasm())
            {
                evmcMessage.destination_ptr = (uint8_t*)hostContext.myAddress().data();
                evmcMessage.destination_len = hostContext.codeAddress().size();

                evmcMessage.sender_ptr = (uint8_t*)hostContext.caller().data();
                evmcMessage.sender_len = hostContext.caller().size();

                if (hostContext.isCreate())
                {
                    assert(extraData != nullptr);
                    evmcMessage.input_data = extraData->data.data();
                    evmcMessage.input_size = extraData->data.size();
                }
                else
                {
                    evmcMessage.input_data = hostContext.data().data();
                    evmcMessage.input_size = hostContext.data().size();
                }
            }
            else
            {
                evmcMessage.input_data = hostContext.data().data();
                evmcMessage.input_size = hostContext.data().size();

                auto myAddressBytes = boost::algorithm::unhex(std::string(hostContext.myAddress()));
                auto callerBytes = boost::algorithm::unhex(std::string(hostContext.caller()));

                evmcMessage.destination = toEvmC(myAddressBytes);
                evmcMessage.sender = toEvmC(callerBytes);
            }

            return evmcMessage;
        };

        auto blockContext = m_blockContext.lock();
        if (!blockContext)
        {
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null!"));
        }

        if (hostContext.isCreate())
        {
            auto mode = toRevision(hostContext.vmSchedule());
            auto evmcMessage = getEVMCMessage(*blockContext, hostContext);

            auto code = hostContext.data();
            auto vmKind = VMKind::evmone;

            if (blockContext->isWasm())
            {
                vmKind = VMKind::BcosWasm;
            }

            auto vm = VMFactory::create(vmKind);

            auto ret = vm.exec(hostContext, mode, &evmcMessage, code.data(), code.size());

            auto callResults = hostContext.takeCallParameters();
            // clear unnecessary logs
            if (callResults->origin != callResults->senderAddress)
            {
                EXECUTIVE_LOG(TRACE)
                    << "clear logEntries"
                    << LOG_KV("beforeClearLogSize", callResults->logEntries.size());
                callResults->logEntries.clear();
            }
            callResults = parseEVMCResult(std::move(callResults), ret);

            if (callResults->status != (int32_t)TransactionStatus::None)
            {
                EXECUTIVE_LOG(ERROR)
                    << LOG_DESC("deploy failed") << LOG_KV("sender", callResults->senderAddress)
                    << LOG_KV("address", callResults->codeAddress);
                revert();
                callResults->type = CallParameters::REVERT;
                // Clear the creation flag
                callResults->create = false;
                return callResults;
            }

            auto outputRef = ret.output();
            if (outputRef.size() > hostContext.vmSchedule().maxCodeSize)
            {
                revert();
                callResults->type = CallParameters::REVERT;
                callResults->status = (int32_t)TransactionStatus::OutOfGas;
                callResults->message =
                    "Code is too large: " + boost::lexical_cast<std::string>(outputRef.size()) +
                    " limit: " +
                    boost::lexical_cast<std::string>(hostContext.vmSchedule().maxCodeSize);
                EXECUTIVE_LOG(ERROR) << LOG_DESC("deploy failed code too large")
                                     << LOG_KV("message", callResults->message);
                return callResults;
            }

            if ((int64_t)(outputRef.size() * hostContext.vmSchedule().createDataGas) >
                callResults->gas)
            {
                if (hostContext.vmSchedule().exceptionalFailedCodeDeposit)
                {
                    revert();
                    callResults->type = CallParameters::REVERT;
                    callResults->status = (int32_t)TransactionStatus::OutOfGas;
                    callResults->message = "exceptionalFailedCodeDeposit";
                    EXECUTIVE_LOG(ERROR) << LOG_DESC("deploy failed OutOfGas")
                                         << LOG_KV("message", callResults->message);
                    return callResults;
                }
            }

            if (blockContext->isWasm())
            {
                // BFS create contract table and write metadata in parent table
                auto tableName = getContractTableName(hostContext.myAddress(), true);
                if (!buildBfsPath(tableName, callResults->origin, hostContext.myAddress(),
                        FS_TYPE_CONTRACT, callResults->gas))
                {
                    revert();
                    auto buildCallResults = move(callResults);
                    buildCallResults->type = CallParameters::REVERT;
                    buildCallResults->status = (int32_t)TransactionStatus::RevertInstruction;
                    buildCallResults->message = "Error occurs in build BFS dir";
                    EXECUTIVE_LOG(ERROR)
                        << buildCallResults->message << LOG_KV("tableName", tableName);
                    return buildCallResults;
                }
            }

            assert(extraData != nullptr);
            hostContext.setCodeAndAbi(outputRef.toBytes(), extraData->abi);
            if (!blockContext->isWasm())
            {
                if (outputRef.empty())
                {
                    callResults->type = CallParameters::REVERT;
                    callResults->status = (int32_t)TransactionStatus::Unknown;
                    callResults->message = "Create contract with empty code, wrong code input.";
                    EXECUTIVE_LOG(ERROR) << LOG_DESC("deploy failed code empty")
                                         << LOG_KV("message", callResults->message);
                    // Clear the creation flag
                    callResults->create = false;
                    // Clear the data
                    callResults->data.clear();
                    revert();
                    return callResults;
                }
                hostContext.setCode(outputRef.toBytes());
            }

            callResults->gas -= outputRef.size() * hostContext.vmSchedule().createDataGas;
            callResults->newEVMContractAddress = callResults->codeAddress;

            // Clear the creation flag
            callResults->create = false;

            // Clear the data
            callResults->data.clear();

            return callResults;
        }
        else
        {
            auto codeEntry = hostContext.code();
            if (!codeEntry.has_value())
            {
                revert();
                auto callResult = hostContext.takeCallParameters();
                callResult->type = CallParameters::REVERT;
                callResult->status = (int32_t)TransactionStatus::CallAddressError;
                callResult->message = "Error contract address.";
                EXECUTIVE_LOG(ERROR)
                    << LOG_DESC("call address error") << LOG_KV("address", callResult->codeAddress)
                    << LOG_KV("sender", callResult->senderAddress);
                return callResult;
            }
            auto code = codeEntry->get();
            if (hasPrecompiledPrefix(code))
            {
                return callDynamicPrecompiled(hostContext.takeCallParameters(), std::string(code));
            }

            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::BcosWasm;
            }
            auto vm = VMFactory::create(vmKind);

            auto mode = toRevision(hostContext.vmSchedule());
            auto evmcMessage = getEVMCMessage(*blockContext, hostContext);
            auto ret = vm.exec(hostContext, mode, &evmcMessage,
                reinterpret_cast<const byte*>(code.data()), code.size());

            auto callResults = hostContext.takeCallParameters();
            callResults = parseEVMCResult(std::move(callResults), ret);

            // clear unnecessary logs
            if (callResults->origin != callResults->senderAddress)
            {
                EXECUTIVE_LOG(TRACE)
                    << "clear logEntries"
                    << LOG_KV("beforeClearLogSize", callResults->logEntries.size());
                callResults->logEntries.clear();
            }
            return callResults;
        }
    }
    catch (bcos::Error& e)
    {
        auto callResults = hostContext.takeCallParameters();
        if (!callResults)
        {
            callResults = std::make_unique<CallParameters>(CallParameters::REVERT);
        }
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::RevertInstruction;
        callResults->message = e.errorMessage();

        revert();

        if (e.errorCode() == DEAD_LOCK)
        {
            // DEAD LOCK revert need provide sender and receiver
            EXECUTOR_LOG(DEBUG) << "Revert by dead lock, sender: " << callResults->senderAddress
                                << " receiver: " << callResults->receiveAddress;
        }
        else
        {
            EXECUTIVE_LOG(ERROR) << "BCOS Error: " << diagnostic_information(e);
        }

        return callResults;
    }
    catch (InternalVMError const& _e)
    {
        EXECUTIVE_LOG(WARNING) << "Internal VM Error ("
                               << *boost::get_error_info<errinfo_evmcStatusCode>(_e) << ")\n"
                               << diagnostic_information(_e);
        exit(1);
    }
    catch (Exception const& _e)
    {
        // TODO: AUDIT: check that this can never reasonably happen. Consider what
        // to do if it does.
        EXECUTIVE_LOG(ERROR) << "Unexpected exception in VM. There may be a bug "
                                "in this implementation. "
                             << diagnostic_information(_e);
        exit(1);
        // Another solution would be to reject this transaction, but that also
        // has drawbacks. Essentially, the amount of ram has to be increased here.
    }
    catch (std::exception& _e)
    {
        // TODO: AUDIT: check that this can never reasonably happen. Consider what
        // to do if it does.
        EXECUTIVE_LOG(ERROR) << "Unexpected std::exception in VM. Not enough RAM? " << _e.what();
        exit(1);
        // Another solution would be to reject this transaction, but that also
        // has drawbacks. Essentially, the amount of ram has to be increased here.
    }
}

CallParameters::UniquePtr TransactionExecutive::callDynamicPrecompiled(
    CallParameters::UniquePtr callParameters, const std::string& code)
{
    auto blockContext = m_blockContext.lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("callDynamicPrecompiled") << LOG_KV("code", code);
    std::vector<std::string> codeParameters;
    boost::split(codeParameters, code, boost::is_any_of(","));
    if (codeParameters.size() < 3)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "CallDynamicPrecompiled error code field."));
    }
    callParameters->codeAddress = codeParameters[1];
    // for scalability, erase [PRECOMPILED_PREFIX,codeAddress], left actual parameters
    codeParameters.erase(codeParameters.begin(), codeParameters.begin() + 2);
    // enc([call precompiled parameters],[user call parameters])
    auto newParams = codec.encode(codeParameters, callParameters->data);
    callParameters->data = std::move(newParams);
    return std::get<1>(callPrecompiled(std::move(callParameters)));
}

void TransactionExecutive::spawnAndCall(std::function<void(ResumeHandler)> function)
{
    (*m_pushMessage)(std::move(function));
}

std::shared_ptr<precompiled::PrecompiledExecResult> TransactionExecutive::execPrecompiled(
    const std::string& address, bytesConstRef param, const std::string& origin,
    const std::string& sender, int64_t gasLeft)
{
    try
    {
        auto p = getPrecompiled(address);

        if (p)
        {
            auto execResult = p->call(shared_from_this(), param, origin, sender, gasLeft);
            return execResult;
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << LOG_DESC("[call]Can't find address")
                                 << LOG_KV("address", address);
            return nullptr;
        }
    }
    catch (PrecompiledError const& e)
    {
        EXECUTIVE_LOG(ERROR) << "PrecompiledError" << LOG_KV("address", address)
                             << LOG_KV("error", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (Exception const& e)
    {
        EXECUTIVE_LOG(ERROR) << "Exception" << LOG_KV("address", address)
                             << LOG_KV("error", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << LOG_DESC("[call]Precompiled call error")
                             << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(PrecompiledError("InternalPrecompiledError"));
    }
}

bool TransactionExecutive::isPrecompiled(const std::string& address) const
{
    return m_constantPrecompiled->count(address) > 0;
}

std::shared_ptr<Precompiled> TransactionExecutive::getPrecompiled(const std::string& address) const
{
    auto constantPrecompiled = m_constantPrecompiled->find(address);

    if (constantPrecompiled != m_constantPrecompiled->end())
    {
        return constantPrecompiled->second;
    }
    return {};
}

std::pair<bool, bcos::bytes> TransactionExecutive::executeOriginPrecompiled(
    const string& _a, bytesConstRef _in) const
{
    return m_evmPrecompiled->at(_a)->execute(_in);
}

int64_t TransactionExecutive::costOfPrecompiled(const string& _a, bytesConstRef _in) const
{
    return m_evmPrecompiled->at(_a)->cost(_in).convert_to<int64_t>();
}

void TransactionExecutive::setEVMPrecompiled(
    std::shared_ptr<const std::map<std::string, PrecompiledContract::Ptr>> precompiledContract)
{
    m_evmPrecompiled = std::move(precompiledContract);
}
void TransactionExecutive::setConstantPrecompiled(
    const string& address, std::shared_ptr<precompiled::Precompiled> precompiled)
{
    m_constantPrecompiled->insert({address, precompiled});
}
void TransactionExecutive::setConstantPrecompiled(
    std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
        _constantPrecompiled)
{
    m_constantPrecompiled = std::move(_constantPrecompiled);
}

void TransactionExecutive::revert()
{
    EXECUTOR_LOG(INFO) << "Revert transaction";

    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null!"));
    }

    blockContext->storage()->rollback(*m_recoder);
    m_recoder->clear();
}

CallParameters::UniquePtr TransactionExecutive::parseEVMCResult(
    CallParameters::UniquePtr callResults, const Result& _result)
{
    callResults->type = CallParameters::REVERT;
    // FIXME: if EVMC_REJECTED, then use default vm to run. maybe wasm call evm
    // need this
    auto outputRef = _result.output();
    switch (_result.status())
    {
    case EVMC_SUCCESS:
    {
        callResults->type = CallParameters::FINISHED;
        callResults->status = _result.status();
        callResults->gas = _result.gasLeft();
        if (!callResults->create)
        {
            callResults->data.assign(outputRef.begin(), outputRef.end());  // TODO: avoid the data
                                                                           // copy
        }
        break;
    }
    case EVMC_REVERT:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_REVERT") << LOG_KV("gasLeft", callResults->gas);
        // FIXME: Copy the output for now, but copyless version possible.
        callResults->gas = _result.gasLeft();
        revert();
        // Note: both the precompiled or the application-developer may calls writeErrorInfo to the
        // data when revert
        callResults->data.assign(outputRef.begin(), outputRef.end());
        // m_output = owning_bytes_ref(
        //     bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
        callResults->status = (int32_t)TransactionStatus::RevertInstruction;
        // m_excepted = TransactionStatus::RevertInstruction;
        break;
    }
    case EVMC_OUT_OF_GAS:
    {
        revert();
        EXECUTIVE_LOG(WARNING) << LOG_DESC("OutOfGas") << LOG_KV("gas", _result.gasLeft());
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        callResults->gas = _result.gasLeft();
        break;
    }
    case EVMC_FAILURE:
    {
        revert();
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASMTrap");
        callResults->status = (int32_t)TransactionStatus::WASMTrap;
        callResults->gas = _result.gasLeft();
        break;
    }

    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_INVALID_INSTRUCTION/EVMC_INVALID_INSTRUCTION");
        // m_remainGas = 0; //TODO: why set remainGas to 0?
        callResults->status = (int32_t)TransactionStatus::BadInstruction;
        revert();
        break;
    }
    case EVMC_BAD_JUMP_DESTINATION:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_BAD_JUMP_DESTINATION");
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::BadJumpDestination;
        revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_STACK_OVERFLOW");
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::OutOfStack;
        revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_STACK_UNDERFLOW");
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        revert();
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, BufferOverrun");
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        revert();
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(WARNING) << LOG_DESC("VM error, DisallowedStateChange");
        callResults->status = (int32_t)TransactionStatus::Unknown;
        revert();
        break;
    }
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC(
            "WASM validation failed, contract hash algorithm dose not match host.");
        callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
        revert();
        break;
    }
    case EVMC_ARGUMENT_OUT_OF_RANGE:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Argument Out Of Range");
        callResults->status = (int32_t)TransactionStatus::WASMArgumentOutOfRange;
        revert();
        break;
    }
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("WASM Unreachable Instruction");
        callResults->status = (int32_t)TransactionStatus::WASMUnreachableInstruction;
        revert();
        break;
    }
    case EVMC_INTERNAL_ERROR:
    default:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_INTERNAL_ERROR/default revert")
                               << LOG_KV("errCode", EVMC_INTERNAL_ERROR);
        revert();
        if (_result.status() <= EVMC_INTERNAL_ERROR)
        {
            BOOST_THROW_EXCEPTION(InternalVMError{} << errinfo_evmcStatusCode(_result.status()));
        }
        else
        {  // These cases aren't really internal errors, just more specific
           // error codes returned by the VM. Map all of them to OOG.m_externalCallCallback
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::OutOfGas;
        }
    }
    }

    return callResults;
}

void TransactionExecutive::creatAuthTable(
    std::string_view _tableName, std::string_view _origin, std::string_view _sender)
{
    // Create the access table
    //  /sys/ not create
    if (_tableName.substr(0, 5) == USER_SYS_PREFIX ||
        getContractTableName(_sender, false).substr(0, 5) == USER_SYS_PREFIX)
    {
        return;
    }
    auto authTableName = std::string(_tableName).append(CONTRACT_SUFFIX);
    // if contract external create contract, then inheritance admin
    // FIXME: check this available in multi executor
    std::string admin;
    if (_sender != _origin)
    {
        auto senderAuthTable = getContractTableName(_sender, false).append(CONTRACT_SUFFIX);
        auto entry = m_storageWrapper->getRow(std::move(senderAuthTable), ADMIN_FIELD);
        admin = std::string(entry->getField(0));
    }
    else
    {
        admin = std::string(_sender);
    }
    EXECUTIVE_LOG(DEBUG) << "creatAuthTable in deploy" << LOG_KV("tableName", _tableName)
                         << LOG_KV("origin", _origin) << LOG_KV("sender", _sender)
                         << LOG_KV("admin", admin);
    auto table = m_storageWrapper->createTable(authTableName, STORAGE_VALUE);

    if (table)
    {
        Entry adminEntry(table->tableInfo());
        adminEntry.importFields({admin});
        m_storageWrapper->setRow(authTableName, ADMIN_FIELD, std::move(adminEntry));

        Entry statusEntry(table->tableInfo());
        statusEntry.importFields({CONTRACT_NORMAL});
        m_storageWrapper->setRow(authTableName, STATUS_FIELD, std::move(statusEntry));

        Entry emptyType;
        emptyType.importFields({""});
        m_storageWrapper->setRow(authTableName, METHOD_AUTH_TYPE, std::move(emptyType));

        Entry emptyWhite;
        emptyWhite.importFields({""});
        m_storageWrapper->setRow(authTableName, METHOD_AUTH_WHITE, std::move(emptyWhite));

        Entry emptyBlack;
        emptyBlack.importFields({""});
        m_storageWrapper->setRow(authTableName, METHOD_AUTH_BLACK, std::move(emptyBlack));
    }
}

bool TransactionExecutive::buildBfsPath(std::string_view _absoluteDir, std::string_view _origin,
    std::string_view _sender, std::string_view _type, int64_t gasLeft)
{
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("buildBfsPath") << LOG_KV("absoluteDir", _absoluteDir);
    auto response =
        externalTouchNewFile(shared_from_this(), _origin, _sender, _absoluteDir, _type, gasLeft);
    return response == (int)precompiled::CODE_SUCCESS;
}

bool TransactionExecutive::checkAuth(
    const CallParameters::UniquePtr& callParameters, bool _isCreate)
{
    if (callParameters->staticCall)
        return true;
    auto blockContext = m_blockContext.lock();
    auto authMgrAddress =
        blockContext->isWasm() ? precompiled::AUTH_MANAGER_NAME : precompiled::AUTH_MANAGER_ADDRESS;
    auto contractAuthPrecompiled = dynamic_pointer_cast<precompiled::ContractAuthMgrPrecompiled>(
        m_constantPrecompiled->at(AUTH_CONTRACT_MGR_ADDRESS));
    std::string address = callParameters->origin;
    auto path = callParameters->codeAddress;
    EXECUTIVE_LOG(DEBUG) << "check auth" << LOG_KV("codeAddress", path)
                         << LOG_KV("isCreate", _isCreate) << LOG_KV("originAddress", address);
    if (_isCreate)
    {
        /// external call authMgrAddress to check deploy auth
        auto codec =
            std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
        auto input = blockContext->isWasm() ?
                         codec->encodeWithSig("hasDeployAuth(string)", address) :
                         codec->encodeWithSig("hasDeployAuth(address)", Address(address));
        auto response = externalRequest(shared_from_this(), ref(input), callParameters->origin,
            callParameters->receiveAddress, authMgrAddress, false, false, callParameters->gas);
        bool result = true;
        codec->decode(ref(response->data), result);
        return result;
    }
    bytesRef func = ref(callParameters->data).getCroppedData(0, 4);
    return contractAuthPrecompiled->checkMethodAuth(
        shared_from_this(), std::move(path), func, address);
}

bool TransactionExecutive::checkContractAvailable(const CallParameters::UniquePtr& callParameters)
{
    auto blockContext = m_blockContext.lock();
    auto contractAuthPrecompiled = dynamic_pointer_cast<precompiled::ContractAuthMgrPrecompiled>(
        m_constantPrecompiled->at(AUTH_CONTRACT_MGR_ADDRESS));
    auto path = callParameters->codeAddress;
    EXECUTIVE_LOG(DEBUG) << "check contract status" << LOG_KV("codeAddress", path);

    return contractAuthPrecompiled->checkContractAvailable(shared_from_this(), std::move(path));
}