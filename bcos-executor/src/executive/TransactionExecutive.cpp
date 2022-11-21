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
#include "../precompiled/BFSPrecompiled.h"
#include "../precompiled/extension/AccountPrecompiled.h"
#include "../precompiled/extension/AuthManagerPrecompiled.h"
#include "../precompiled/extension/ContractAuthMgrPrecompiled.h"
#include "../vm/DelegateHostContext.h"
#include "../vm/EVMHostInterface.h"
#include "../vm/HostContext.h"
#include "../vm/Precompiled.h"
#include "../vm/VMFactory.h"
#include "../vm/VMInstance.h"
#include "../vm/gas_meter/GasInjector.h"
#include "BlockContext.h"
#include "ExecutiveFactory.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-tool/BfsFileFactory.h>
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
    EXECUTIVE_LOG(TRACE) << "Execute start\t" << input->toFullString();


    auto& callParameters = input;
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null"));
    }

    m_storageWrapper = std::make_shared<StorageWrapper>(blockContext->storage(), m_recoder);

    auto message = execute(std::move(callParameters));

    EXECUTIVE_LOG(TRACE) << "Execute finish\t" << message->toFullString();

    return message;
}

CallParameters::UniquePtr TransactionExecutive::externalCall(CallParameters::UniquePtr input)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << "externalCall start\t" << input->toFullString();
    }
    auto newSeq = seq() + 1;
    bool isCreate = input->create;
    input->seq = newSeq;
    input->contextID = m_contextID;

    std::string newAddress;
    // if internalCreate, sometimes it will use given address, if receiveAddress is empty then give
    // a new address
    if (isCreate && !m_blockContext.lock()->isWasm() && std::empty(input->receiveAddress))
    {
        if (input->createSalt)
        {
            // TODO: Add sender in this process(consider compat with ethereum)
            newAddress = bcos::newEVMAddress(m_hashImpl, input->senderAddress,
                bytesConstRef(input->data.data(), input->data.size()), *(input->createSalt));
        }
        else
        {
            // TODO: Add sender in this process(consider compat with ethereum)
            newAddress = bcos::newEVMAddress(
                m_hashImpl, m_blockContext.lock()->number(), m_contextID, newSeq);
        }

        input->receiveAddress = newAddress;
        input->codeAddress = newAddress;
    }

    if (input->delegateCall)
    {
        assert(!m_blockContext.lock()->isWasm());
        auto tableName = getContractTableName(input->codeAddress, false);

        // get codeHash in contract table
        auto codeHashEntry = storage().getRow(tableName, ACCOUNT_CODE_HASH);
        if (!codeHashEntry || codeHashEntry->get().empty())
        {
            auto& output = input;
            EXECUTIVE_LOG(DEBUG) << "Could not getCodeHash during externalCall"
                                 << LOG_KV("codeAddress", input->codeAddress);
            output->data = bytes();
            output->status = (int32_t)TransactionStatus::RevertInstruction;
            output->evmStatus = EVMC_REVERT;
            return std::move(output);
        }

        auto codeHash = codeHashEntry->getField(0);

        // get code in code binary table
        auto entry = storage().getRow(bcos::ledger::SYS_CODE_BINARY, codeHash);
        if (!entry || entry->get().empty())
        {
            auto& output = input;
            EXECUTIVE_LOG(DEBUG) << "Could not getCode during externalCall"
                                 << LOG_KV("codeAddress", input->codeAddress);
            output->data = bytes();
            output->status = (int32_t)TransactionStatus::RevertInstruction;
            output->evmStatus = EVMC_REVERT;
            return std::move(output);
        }
        input->delegateCallCode = toBytes(entry->get());
    }

    if (input->data == bcos::protocol::GET_CODE_INPUT_BYTES)
    {
        EXECUTIVE_LOG(DEBUG) << "Get external code request"
                             << LOG_KV("codeAddress", input->codeAddress);

        auto tableName = getContractTableName(input->codeAddress, false);

        auto& output = input;
        // get codeHash in contract table
        auto codeHashEntry = storage().getRow(tableName, ACCOUNT_CODE_HASH);
        if (!codeHashEntry || codeHashEntry->get().empty())
        {
            EXECUTIVE_LOG(DEBUG) << "Could not get external code hash from local storage"
                                 << LOG_KV("codeAddress", input->codeAddress);
            output->data = bytes();
            return std::move(output);
        }

        auto codeHash = codeHashEntry->getField(0);

        // get code in code binary table
        auto entry = storage().getRow(bcos::ledger::SYS_CODE_BINARY, codeHash);
        if (!entry || entry->get().empty())
        {
            EXECUTIVE_LOG(DEBUG) << "Could not get external code from local storage"
                                 << LOG_KV("codeAddress", input->codeAddress);
            output->data = bytes();
            return std::move(output);
        }
        output->data = toBytes(entry->get());
        return std::move(output);
    }

    auto executiveFactory = std::make_shared<ExecutiveFactory>(m_blockContext, m_evmPrecompiled,
        m_constantPrecompiled, m_builtInPrecompiled, m_gasInjector);
    auto executive = executiveFactory->build(input->codeAddress, m_contextID, newSeq, false);

    auto output = executive->start(std::move(input));

    // update seq
    m_seq = executive->seq();

    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << "externalCall finish\t" << output->toFullString();
    }
    return output;
}


CallParameters::UniquePtr TransactionExecutive::execute(CallParameters::UniquePtr callParameters)
{
    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << LOG_DESC("Execute begin")
                             << LOG_KV("callParameters", callParameters->toFullString())
                             << LOG_KV("blockNumber", blockContext().lock()->number());
    }
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
    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << LOG_DESC("Execute finished")
                             << LOG_KV("callResults", callResults->toFullString())
                             << LOG_KV("blockNumber", blockContext().lock()->number());
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

    EXECUTIVE_LOG(DEBUG) << BLOCK_NUMBER(blockContext->number()) << LOG_DESC("executive call")
                         << LOG_KV("contract", callParameters->receiveAddress)
                         << LOG_KV("sender", callParameters->senderAddress)
                         << LOG_KV("internalCall", callParameters->internalCall)
                         << LOG_KV("delegateCall", callParameters->delegateCall)
                         << LOG_KV("codeAddress", callParameters->codeAddress);

    auto tableName = getContractTableName(callParameters->receiveAddress, blockContext->isWasm());
    // delegateCall is just about to replace code, no need to check permission beforehand
    if (callParameters->delegateCall)
    {
        auto hostContext = make_unique<DelegateHostContext>(
            std::move(callParameters), shared_from_this(), std::move(tableName));
        return {std::move(hostContext), nullptr};
    }
    // check permission first
    if (blockContext->isAuthCheck() && !checkAuth(callParameters))
    {
        revert();
        return {nullptr, std::move(callParameters)};
    }
    if (isPrecompiled(callParameters->receiveAddress) || callParameters->internalCall)
    {
        return {nullptr, callPrecompiled(std::move(callParameters))};
    }

    auto hostContext = make_unique<HostContext>(
        std::move(callParameters), shared_from_this(), std::move(tableName));
    return {std::move(hostContext), nullptr};
}

CallParameters::UniquePtr TransactionExecutive::callPrecompiled(
    CallParameters::UniquePtr callParameters)
{
    auto precompiledCallParams = std::make_shared<PrecompiledExecResult>(callParameters);
    bytes data{};
    if (callParameters->internalCall)
    {
        std::string contract;
        auto blockContext = m_blockContext.lock();
        auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
        codec.decode(ref(callParameters->data), contract, data);
        precompiledCallParams->m_precompiledAddress = contract;
        precompiledCallParams->m_input = ref(data);
    }
    try
    {
        execPrecompiled(precompiledCallParams);

        if (precompiledCallParams->m_gasLeft < 0)
        {
            revert();
            EXECUTIVE_LOG(INFO) << "Revert transaction: call precompiled out of gas.";
            callParameters->type = CallParameters::REVERT;
            callParameters->status = (int32_t)TransactionStatus::OutOfGas;
            if (versionCompareTo(
                    blockContext().lock()->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Call precompiled out of gas.", *callParameters);
            }
            return callParameters;
        }
        precompiledCallParams->takeDataToCallParameter(callParameters);
    }
    catch (protocol::PrecompiledError const& e)
    {
        EXECUTIVE_LOG(INFO) << "Revert transaction: "
                            << "PrecompiledError"
                            << LOG_KV("address", precompiledCallParams->m_precompiledAddress)
                            << LOG_KV("error", e.what());
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
        EXECUTIVE_LOG(WARNING) << "Exception"
                               << LOG_KV("address", precompiledCallParams->m_precompiledAddress)
                               << LOG_KV("error", e.what());
        writeErrInfoToOutput(e.what(), *callParameters);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)executor::toTransactionStatus(e);
        callParameters->message = e.what();
    }
    catch (std::exception const& e)
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
    return callParameters;
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

    EXECUTIVE_LOG(DEBUG) << BLOCK_NUMBER(blockContext->number())
                         << LOG_DESC("executive deploy contract") << LOG_KV("tableName", tableName)
                         << LOG_KV("abi len", extraData->abi.size())
                         << LOG_KV("sender", callParameters->senderAddress)
                         << LOG_KV("internalCreate", callParameters->internalCreate);

    // check permission first
    if (blockContext->isAuthCheck() && !checkAuth(callParameters))
    {
        revert();
        return {nullptr, std::move(callParameters)};
    }
    if (callParameters->internalCreate)
    {
        callParameters->abi = std::move(extraData->abi);
        auto sender = callParameters->senderAddress;
        auto response = internalCreate(std::move(callParameters));
        if (blockContext->isAuthCheck() &&
            blockContext->blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
        {
            // Create auth table
            creatAuthTable(tableName, response->origin, std::move(sender));
        }
        return {nullptr, std::move(response)};
    }
    // Create table
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
        // this exception will be frequent to happened in liquid
        revert();
        callParameters->status = (int32_t)TransactionStatus::ContractAddressAlreadyUsed;
        callParameters->type = CallParameters::REVERT;
        callParameters->message = e.what();
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Contract address already used.", *callParameters);
        }
        EXECUTIVE_LOG(INFO) << "Revert transaction: " << LOG_DESC("createTable failed")
                            << callParameters->message << LOG_KV("tableName", tableName);
        return {nullptr, std::move(callParameters)};
    }

    if (blockContext->isWasm())
    {
        // Liquid
        std::tuple<bytes, bytes> input;
        auto codec = CodecWrapper(blockContext->hashHandler(), true);
        codec.decode(ref(callParameters->data), input);
        auto& [code, params] = input;

        if (!hasWasmPreamble(code))
        {
            revert();

            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "the code is not wasm bytecode";
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << callResults->message;
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput(
                    "WASM bytecode invalid or use unsupported opcode.", *callResults);
            }
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
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput(
                    "WASM bytecode invalid or use unsupported opcode.", *callResults);
            }
            // use wrong wasm code
            EXECUTIVE_LOG(WARNING) << "Revert transaction: " << callResults->message;
            return {nullptr, std::move(callResults)};
        }

        callParameters->data.swap(code);

        extraData->data = std::move(params);
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
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName;
    std::string codeString;
    codec.decode(ref(callParameters->data), tableName, codeString);
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("internalCreate") << LOG_KV("newAddress", newAddress)
                         << LOG_KV("codeString", codeString);

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
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Error occurs in building BFS dir.", *buildCallResults);
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << buildCallResults->message
                                << LOG_KV("newAddress", newAddress);
            return buildCallResults;
        }
        /// create contract table
        m_storageWrapper->createTable(newAddress, STORAGE_VALUE);
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
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Error occurs in building BFS dir.", *buildCallResults);
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << buildCallResults->message
                                << LOG_KV("newAddress", newAddress);
            return buildCallResults;
        }

        /// create link table
        auto linkTable = m_storageWrapper->createTable(tableName, STORAGE_VALUE);

        /// create code index contract
        auto codeTable = getContractTableName(newAddress, false);
        m_storageWrapper->createTable(codeTable, STORAGE_VALUE);

        /// set code field
        Entry entry = {};
        entry.importFields({codeString});
        m_storageWrapper->setRow(codeTable, ACCOUNT_CODE, std::move(entry));
        if (!callParameters->abi.empty())
        {
            Entry abiEntry = {};
            abiEntry.importFields({std::move(callParameters->abi)});
            m_storageWrapper->setRow(codeTable, ACCOUNT_ABI, std::move(abiEntry));
        }

        /// set link data
        tool::BfsFileFactory::buildLink(linkTable.value(), newAddress, "");
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
            if (blockContext->blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
            {
                EXECUTIVE_LOG(TRACE)
                    << "logEntries" << LOG_KV("LogSize", callResults->logEntries.size());
            }
            else
            {
                if (callResults->origin != callResults->senderAddress)
                {
                    EXECUTIVE_LOG(TRACE)
                        << "clear logEntries"
                        << LOG_KV("beforeClearLogSize", callResults->logEntries.size());
                    callResults->logEntries.clear();
                }
            }
            callResults = parseEVMCResult(std::move(callResults), ret);

            if (callResults->status != (int32_t)TransactionStatus::None)
            {
                EXECUTIVE_LOG(INFO)
                    << "Revert transaction: " << LOG_DESC("deploy failed due to status error")
                    << LOG_KV("status", callResults->status)
                    << LOG_KV("sender", callResults->senderAddress)
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
                if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                {
                    writeErrInfoToOutput("Deploy code is too large.", *callResults);
                }
                EXECUTIVE_LOG(DEBUG)
                    << "Revert transaction: " << LOG_DESC("deploy failed code too large")
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
                    if (versionCompareTo(
                            blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                    {
                        writeErrInfoToOutput("Exceptional Failed Code Deposit", *callResults);
                    }
                    EXECUTIVE_LOG(INFO)
                        << "Revert transaction: " << LOG_DESC("deploy failed OutOfGas")
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
                    if (versionCompareTo(
                            blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                    {
                        writeErrInfoToOutput(
                            "Error occurs in building BFS dir.", *buildCallResults);
                    }
                    EXECUTIVE_LOG(DEBUG) << "Revert transaction: " << buildCallResults->message
                                         << LOG_KV("tableName", tableName);
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
                    EXECUTIVE_LOG(WARNING)
                        << "Revert transaction: " << LOG_DESC("deploy failed code empty")
                        << LOG_KV("message", callResults->message);
                    // Clear the creation flag
                    callResults->create = false;
                    // Clear the data
                    if (versionCompareTo(
                            blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                    {
                        writeErrInfoToOutput(
                            "Create contract with empty code, invalid code input.", *callResults);
                    }
                    else if (versionCompareTo(
                                 blockContext->blockVersion(), BlockVersion::V3_0_VERSION) <= 0)
                    {
                        callResults->data.clear();
                    }
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
                if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                {
                    writeErrInfoToOutput("Call address error.", *callResult);
                }
                EXECUTIVE_LOG(INFO) << "Revert transaction: "
                                    << LOG_DESC("call address error, maybe address not exist")
                                    << LOG_KV("address", callResult->codeAddress)
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

            if (blockContext->blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
            {
                EXECUTIVE_LOG(TRACE)
                    << "logEntries" << LOG_KV("LogSize", callResults->logEntries.size());
                return callResults;
            }
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
        if (versionCompareTo(blockContext().lock()->blockVersion(), BlockVersion::V3_1_VERSION) >=
            0)
        {
            writeErrInfoToOutput(e.errorMessage(), *callResults);
        }

        revert();


        if (e.errorCode() == DEAD_LOCK)
        {
            // DEAD LOCK revert need provide sender and receiver
            EXECUTOR_LOG(DEBUG) << "Revert by dead lock, sender: " << callResults->senderAddress
                                << " receiver: " << callResults->receiveAddress;
        }
        /*else if (StorageError::UnknownError <= e.errorCode() &&
                 StorageError::TimestampMismatch <= e.errorCode())
        {
            // is storage error
            EXECUTOR_LOG(DEBUG)
                << "Storage exception during tx execute. trigger switch(if this tx is not call). e:"
                << diagnostic_information(e);
            auto blockContext = m_blockContext.lock();
            blockContext->triggerSwitch();
        }
         */
        else
        {
            EXECUTIVE_LOG(ERROR) << "BCOS Error: " << diagnostic_information(e);
        }

        return callResults;
    }
    catch (InternalVMError const& _e)
    {
        EXECUTIVE_LOG(ERROR) << "Internal VM Error ("
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
        EXECUTIVE_LOG(ERROR) << "Unexpected std::exception in VM. Not enough RAM? "
                             << LOG_KV("what", _e.what())
                             << LOG_KV("diagnostic", boost::diagnostic_information(_e));
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
    std::vector<std::string> codeParameters{};
    boost::split(codeParameters, code, boost::is_any_of(","));
    if (codeParameters.size() < 3)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "CallDynamicPrecompiled error code field."));
    }
    callParameters->codeAddress = callParameters->receiveAddress;
    callParameters->receiveAddress = codeParameters[1];
    // for scalability, erase [PRECOMPILED_PREFIX,codeAddress], left actual parameters
    codeParameters.erase(codeParameters.begin(), codeParameters.begin() + 2);
    // enc([call precompiled parameters],[user call parameters])
    auto newParams = codec.encode(codeParameters, callParameters->data);

    callParameters->data = std::move(newParams);
    EXECUTIVE_LOG(TRACE) << LOG_DESC("callDynamicPrecompiled")
                         << LOG_KV("codeAddr", callParameters->codeAddress)
                         << LOG_KV("recvAddr", callParameters->receiveAddress)
                         << LOG_KV("code", code);
    auto callResult = callPrecompiled(std::move(callParameters));

    callResult->receiveAddress = callResult->codeAddress;
    return callResult;
}

std::shared_ptr<precompiled::PrecompiledExecResult> TransactionExecutive::execPrecompiled(
    precompiled::PrecompiledExecResult::Ptr const& _precompiledParams)
{
    auto precompiled = getPrecompiled(_precompiledParams->m_precompiledAddress);

    if (precompiled)
    {
        auto execResult = precompiled->call(shared_from_this(), _precompiledParams);
        return execResult;
    }
    [[unlikely]] EXECUTIVE_LOG(ERROR)
        << LOG_DESC("[call]Can't find precompiled address")
        << LOG_KV("address", _precompiledParams->m_precompiledAddress);
    BOOST_THROW_EXCEPTION(PrecompiledError("can't find precompiled address."));
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
    m_evmPrecompiled = precompiledContract;
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
    m_constantPrecompiled = _constantPrecompiled;
}

void TransactionExecutive::revert()
{
    auto blockContext = m_blockContext.lock();
    if (!blockContext)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "blockContext is null!"));
    }
    EXECUTOR_BLK_LOG(INFO, blockContext->number()) << "Revert transaction";

    blockContext->storage()->rollback(*m_recoder);
    m_recoder->clear();
}

CallParameters::UniquePtr TransactionExecutive::parseEVMCResult(
    CallParameters::UniquePtr callResults, const Result& _result)
{
    auto blockContext = m_blockContext.lock();
    callResults->type = CallParameters::REVERT;
    // FIXME: if EVMC_REJECTED, then use default vm to run. maybe wasm call evm
    // need this
    callResults->evmStatus = _result.status();
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
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_REVERT") << LOG_KV("to", callResults->receiveAddress)
                            << LOG_KV("gasLeft", callResults->gas);
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
        EXECUTIVE_LOG(INFO) << "Revert transaction: " << LOG_DESC("OutOfGas")
                            << LOG_KV("to", callResults->receiveAddress)
                            << LOG_KV("gas", _result.gasLeft());
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        callResults->gas = _result.gasLeft();
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution out of gas.", *callResults);
        }
        break;
    }
    case EVMC_FAILURE:
    {
        revert();
        EXECUTIVE_LOG(INFO) << "Revert transaction: " << LOG_DESC("WASMTrap")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::WASMTrap;
        callResults->gas = _result.gasLeft();
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution failure.", *callResults);
        }
        break;
    }
    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_INVALID_INSTRUCTION/EVMC_INVALID_INSTRUCTION")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::BadInstruction;
        revert();
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution invalid/undefined opcode.", *callResults);
        }
        break;
    }
    case EVMC_BAD_JUMP_DESTINATION:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_BAD_JUMP_DESTINATION")
                            << LOG_KV("to", callResults->receiveAddress);
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::BadJumpDestination;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput(
                "Execution has violated the jump destination restrictions.", *callResults);
        }
        revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_STACK_OVERFLOW")
                            << LOG_KV("to", callResults->receiveAddress);
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::OutOfStack;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution stack overflow.", *callResults);
        }
        revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_STACK_UNDERFLOW")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution needs more items on EVM stack.", *callResults);
        }
        revert();
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(INFO) << LOG_DESC("VM error, BufferOverrun")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution tried to read outside memory bounds.", *callResults);
        }
        revert();
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(INFO) << LOG_DESC("VM error, DisallowedStateChange")
                            << LOG_KV("to", callResults->receiveAddress);
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput(
                "Execution tried to execute an operation which is restricted in static mode.",
                *callResults);
        }
        callResults->status = (int32_t)TransactionStatus::Unknown;
        revert();
        break;
    }
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    {
        EXECUTIVE_LOG(INFO)
            << LOG_DESC("WASM validation failed, contract hash algorithm dose not match host.")
            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Contract validation has failed.", *callResults);
        }
        revert();
        break;
    }
    case EVMC_ARGUMENT_OUT_OF_RANGE:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("WASM Argument Out Of Range")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::WASMArgumentOutOfRange;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput(
                "An argument to a state accessing method has a value outside of the accepted range "
                "of values.",
                *callResults);
        }
        revert();
        break;
    }
    case EVMC_WASM_TRAP:
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("WASM Unreachable/Trap Instruction")
                            << LOG_KV("to", callResults->receiveAddress)
                            << LOG_KV("status", _result.status());
        callResults->status = (int32_t)TransactionStatus::WASMUnreachableInstruction;
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("A WebAssembly trap has been hit during execution.", *callResults);
        }
        revert();
        break;
    }
    case EVMC_INTERNAL_ERROR:
    default:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_INTERNAL_ERROR/default revert")
                               << LOG_KV("to", callResults->receiveAddress)
                               << LOG_KV("status", _result.status());
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
    if (_tableName.starts_with(USER_SYS_PREFIX) ||
        getContractTableName(_sender, false).starts_with(USER_SYS_PREFIX))
    {
        return;
    }
    auto authTableName = std::string(_tableName).append(CONTRACT_SUFFIX);
    std::string admin;
    if (_sender != _origin)
    {
        // if contract external create contract, then inheritance admin, always be origin
        admin = std::string(_origin);
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
        tool::BfsFileFactory::buildAuth(table.value(), admin);
    }
}

bool TransactionExecutive::buildBfsPath(std::string_view _absoluteDir, std::string_view _origin,
    std::string_view _sender, std::string_view _type, int64_t gasLeft)
{
    /// this method only write bfs metadata, not create final table
    /// you should create locally, after external call successfully
    EXECUTIVE_LOG(TRACE) << LOG_DESC("build BFS metadata") << LOG_KV("absoluteDir", _absoluteDir)
                         << LOG_KV("type", _type);
    auto response =
        externalTouchNewFile(shared_from_this(), _origin, _sender, _absoluteDir, _type, gasLeft);
    return response == (int)precompiled::CODE_SUCCESS;
}

bool TransactionExecutive::checkAuth(const CallParameters::UniquePtr& callParameters)
{
    auto blockContext = m_blockContext.lock();
    // check account first
    if (blockContext->blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        uint8_t accountStatus = checkAccountAvailable(callParameters);
        if (accountStatus == AccountStatus::freeze)
        {
            writeErrInfoToOutput("Account is frozen.", *callParameters);
            callParameters->status = (int32_t)TransactionStatus::AccountFrozen;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Account's status is abnormal";
            callParameters->create = false;
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << callParameters->message
                                << LOG_KV("origin", callParameters->origin);
            return false;
        }
        else if (accountStatus == AccountStatus::abolish)
        {
            writeErrInfoToOutput("Account is abolished.", *callParameters);
            callParameters->status = (int32_t)TransactionStatus::AccountAbolished;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Account's status is abnormal";
            callParameters->create = false;
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << callParameters->message
                                << LOG_KV("origin", callParameters->origin);
            return false;
        }
    }
    if (callParameters->create)
    {
        // if create contract, then
        //      check exec auth
        if (!checkExecAuth(callParameters))
        {
            auto newAddress = string(callParameters->codeAddress);
            callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Create permission denied";
            callParameters->create = false;
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Create permission denied.", *callParameters);
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << callParameters->message
                                << LOG_KV("newAddress", newAddress)
                                << LOG_KV("origin", callParameters->origin);
            return false;
        }
    }
    else
    {
        // if internal call, then not check auth
        if (callParameters->internalCall)
        {
            return true;
        }
        auto tableName =
            getContractTableName(callParameters->receiveAddress, blockContext->isWasm());
        // if call contract, then
        //      check contract available
        //      check exec auth
        if (!checkContractAvailable(callParameters))
        {
            callParameters->status = (int32_t)TransactionStatus::ContractFrozen;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Contract is frozen";
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Contract is frozen.", *callParameters);
            }
            else if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_0_VERSION) <=
                     0)
            {
                callParameters->data.clear();
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << callParameters->message
                                << LOG_KV("tableName", tableName)
                                << LOG_KV("origin", callParameters->origin);
            return false;
        }
        if (!checkExecAuth(callParameters))
        {
            callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Call permission denied";
            if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Call permission denied.", *callParameters);
            }
            else if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_0_VERSION) <=
                     0)
            {
                callParameters->data.clear();
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << callParameters->message
                                << LOG_KV("tableName", tableName)
                                << LOG_KV("origin", callParameters->origin);
            return false;
        }
    }
    return true;
}

bool TransactionExecutive::checkExecAuth(const CallParameters::UniquePtr& callParameters)
{
    // static call does not have 'origin', so return true for now
    // precompiled return true by default
    if (callParameters->staticCall || isPrecompiled(callParameters->receiveAddress))
    {
        return true;
    }
    auto blockContext = m_blockContext.lock();
    const auto* authMgrAddress =
        blockContext->isWasm() ? precompiled::AUTH_MANAGER_NAME : precompiled::AUTH_MANAGER_ADDRESS;
    auto contractAuthPrecompiled = dynamic_pointer_cast<precompiled::ContractAuthMgrPrecompiled>(
        m_constantPrecompiled->at(AUTH_CONTRACT_MGR_ADDRESS));
    std::string address = callParameters->origin;
    auto path = callParameters->receiveAddress;
    EXECUTIVE_LOG(TRACE) << "check auth" << LOG_KV("codeAddress", path)
                         << LOG_KV("isCreate", callParameters->create)
                         << LOG_KV("originAddress", address);
    bool result = true;
    if (callParameters->create)
    {
        /// external call authMgrAddress to check deploy auth
        auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
        auto input = blockContext->isWasm() ?
                         codec.encodeWithSig("hasDeployAuth(string)", address) :
                         codec.encodeWithSig("hasDeployAuth(address)", Address(address));
        auto response = externalRequest(shared_from_this(), ref(input), callParameters->origin,
            callParameters->receiveAddress, authMgrAddress, false, false, callParameters->gas);
        codec.decode(ref(response->data), result);
    }
    else
    {
        bytesRef func = ref(callParameters->data).getCroppedData(0, 4);
        result = contractAuthPrecompiled->checkMethodAuth(shared_from_this(), path, func, address);
    }
    EXECUTIVE_LOG(TRACE) << "check auth finished" << LOG_KV("codeAddress", path)
                         << LOG_KV("result", result);
    return result;
}

bool TransactionExecutive::checkContractAvailable(const CallParameters::UniquePtr& callParameters)
{
    // precompiled always available
    if (isPrecompiled(callParameters->receiveAddress) ||
        c_systemTxsAddress.contains(callParameters->receiveAddress))
    {
        return true;
    }
    auto blockContext = m_blockContext.lock();
    auto contractAuthPrecompiled = dynamic_pointer_cast<precompiled::ContractAuthMgrPrecompiled>(
        m_constantPrecompiled->at(AUTH_CONTRACT_MGR_ADDRESS));
    auto path = callParameters->receiveAddress;

    return contractAuthPrecompiled->getContractStatus(shared_from_this(), std::move(path)) != 0;
}

uint8_t TransactionExecutive::checkAccountAvailable(const CallParameters::UniquePtr& callParameters)
{
    if (callParameters->staticCall || callParameters->origin != callParameters->senderAddress ||
        callParameters->internalCall)
    {
        // static call sender and origin will be empty
        // contract calls, pass through
        return 0;
    }
    auto blockContext = m_blockContext.lock();
    AccountPrecompiled::Ptr accountPrecompiled =
        dynamic_pointer_cast<precompiled::AccountPrecompiled>(
            m_constantPrecompiled->at(ACCOUNT_ADDRESS));

    return accountPrecompiled->getAccountStatus(callParameters->origin, shared_from_this());
}