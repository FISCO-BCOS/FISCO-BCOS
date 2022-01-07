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
#include "../precompiled/extension/ContractAuthPrecompiled.h"
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
#include "bcos-utilities/Common.h"
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
        // Dead lock, revert
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
            hostContext->evmSchedule().suicideRefundGas * hostContext->sub().suicides.size();
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

    if (isPrecompiled(callParameters->codeAddress))
    {
        return callPrecompiled(std::move(callParameters));
    }
    else
    {
        auto tableName = getContractTableName(callParameters->codeAddress);
        // check permission first
        if (blockContext->isAuthCheck() && !blockContext->isWasm())
        {
            if (!checkAuth(callParameters, false))
            {
                revert();
                callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
                callParameters->type = CallParameters::REVERT;
                callParameters->message = "Call permission denied";
                EXECUTIVE_LOG(ERROR) << callParameters->message << LOG_KV("tableName", tableName)
                                     << LOG_KV("origin", callParameters->origin);
                return {nullptr, std::move(callParameters)};
            }
        }
        auto hostContext = make_unique<HostContext>(
            std::move(callParameters), shared_from_this(), std::move(tableName));
        return {std::move(hostContext), nullptr};
    }
}

std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr>
TransactionExecutive::callPrecompiled(CallParameters::UniquePtr callParameters)
{
    callParameters->type = CallParameters::FINISHED;
    try
    {
        auto precompiledResult = execPrecompiled(callParameters->codeAddress,
            ref(callParameters->data), callParameters->origin, callParameters->senderAddress);
        auto gas = precompiledResult->m_gas;
        if (callParameters->gas < gas)
        {
            callParameters->type = CallParameters::REVERT;
            callParameters->status = (int32_t)TransactionStatus::OutOfGas;
            return {nullptr, std::move(callParameters)};
        }
        callParameters->gas -= gas;
        callParameters->status = (int32_t)TransactionStatus::None;
        callParameters->data.swap(precompiledResult->m_execResult);
    }
    catch (protocol::PrecompiledError& e)
    {
        const string* _msg = boost::get_error_info<errinfo_comment>(e);
        writeErrInfoToOutput(_msg ? *_msg : "error occurs in precompiled, but error_info is empty",
            callParameters->data);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::PrecompiledError;
    }
    catch (Exception& e)
    {
        writeErrInfoToOutput(e.what(), callParameters->data);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)executor::toTransactionStatus(e);
    }
    catch (std::exception& e)
    {
        writeErrInfoToOutput(e.what(), callParameters->data);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::Unknown;
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

    auto code = bytes();
    auto params = bytes();
    auto abi = string();

    if (blockContext->isWasm())
    {
        auto data = ref(callParameters->data);

        auto input = std::make_pair(std::make_pair(code, params), abi);
        auto codec = std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), true);
        codec->decode(data, input);

        std::tie(code, params) = std::get<0>(input);
        if (!hasWasmPreamble(code))
        {
            revert();

            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
            callResults->message = "wasm bytecode invalid or use unsupported opcode";
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

        abi = std::get<1>(input);
        callParameters->data.swap(code);
    }

    auto newAddress = string(callParameters->codeAddress);

    // check permission first
    if (blockContext->isAuthCheck())
    {
        if (!checkAuth(callParameters, true))
        {
            revert();
            callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Create permission denied";
            EXECUTIVE_LOG(ERROR) << callParameters->message << LOG_KV("newAddress", newAddress)
                                 << LOG_KV("origin", callParameters->origin);
            return {nullptr, std::move(callParameters)};
        }
    }

    // Create the table first
    auto tableName = getContractTableName(newAddress);
    try
    {
        m_storageWrapper->createTable(tableName, STORAGE_VALUE);
        EXECUTIVE_LOG(INFO) << "create contract table " << tableName;
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
        EXECUTIVE_LOG(ERROR) << callParameters->message << LOG_KV("tableName", tableName);
        return {nullptr, std::move(callParameters)};
    }

    if (blockContext->isWasm())
    {
        // BFS recursive build parent dir and write metadata in parent table
        if (!buildBfsPath(tableName))
        {
            revert();
            auto callResults = std::move(callParameters);
            callResults->type = CallParameters::REVERT;
            callResults->status = (int32_t)TransactionStatus::RevertInstruction;
            callResults->message = "Error occurs in build BFS dir";
            EXECUTIVE_LOG(ERROR) << callResults->message << LOG_KV("tableName", tableName);
            return {nullptr, std::move(callResults)};
        }
        auto extraData = std::make_unique<CallParameters>(CallParameters::MESSAGE);
        extraData->data = params;
        extraData->origin = abi;
        auto hostContext =
            std::make_unique<HostContext>(std::move(callParameters), shared_from_this(), tableName);
        return {std::move(hostContext), std::move(extraData)};
    }
    else
    {
        auto hostContext =
            std::make_unique<HostContext>(std::move(callParameters), shared_from_this(), tableName);
        return {std::move(hostContext), nullptr};
    }
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
            auto mode = toRevision(hostContext.evmSchedule());
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
                callResults->type = CallParameters::REVERT;
                // Clear the creation flag
                callResults->create = false;
                return callResults;
            }

            auto outputRef = ret.output();
            if (outputRef.size() > hostContext.evmSchedule().maxCodeSize)
            {
                callResults->type = CallParameters::REVERT;
                callResults->status = (int32_t)TransactionStatus::OutOfGas;
                callResults->message =
                    "Code is too large: " + boost::lexical_cast<std::string>(outputRef.size()) +
                    " limit: " +
                    boost::lexical_cast<std::string>(hostContext.evmSchedule().maxCodeSize);
                EXECUTIVE_LOG(ERROR) << callResults->message;
                return callResults;
            }

            if ((int64_t)(outputRef.size() * hostContext.evmSchedule().createDataGas) >
                callResults->gas)
            {
                if (hostContext.evmSchedule().exceptionalFailedCodeDeposit)
                {
                    callResults->type = CallParameters::REVERT;
                    callResults->status = (int32_t)TransactionStatus::OutOfGas;
                    callResults->message = "exceptionalFailedCodeDeposit";
                    EXECUTIVE_LOG(ERROR) << callResults->message;
                    return callResults;
                }
            }

            if (blockContext->isWasm())
            {
                assert(extraData != nullptr);
                hostContext.setCodeAndAbi(outputRef.toBytes(), extraData->origin);
            }
            else
            {
                if (outputRef.empty())
                {
                    EXECUTOR_LOG(ERROR) << "Create contract with empty code, wrong code input.";
                    callResults->type = CallParameters::REVERT;
                    callResults->status = (int32_t)TransactionStatus::Unknown;
                    callResults->message = "Create contract with empty code, wrong code input.";
                    // Clear the creation flag
                    callResults->create = false;
                    // Clear the data
                    callResults->data.clear();
                    revert();
                    return callResults;
                }
                hostContext.setCode(outputRef.toBytes());
            }

            callResults->gas -= outputRef.size() * hostContext.evmSchedule().createDataGas;
            callResults->newEVMContractAddress = callResults->codeAddress;

            // Clear the creation flag
            callResults->create = false;

            // Clear the data
            callResults->data.clear();

            return callResults;
        }
        else
        {
            auto code = hostContext.code();
            if (code.empty())
            {
                auto callResult = hostContext.takeCallParameters();
                callResult->type = CallParameters::REVERT;
                callResult->status = (int32_t)TransactionStatus::CallAddressError;
                callResult->message = "Error contract address.";
                return callResult;
            }

            auto vmKind = VMKind::evmone;
            if (hasWasmPreamble(code))
            {
                vmKind = VMKind::BcosWasm;
            }
            auto vm = VMFactory::create(vmKind);

            auto mode = toRevision(hostContext.evmSchedule());
            auto evmcMessage = getEVMCMessage(*blockContext, hostContext);
            auto ret = vm.exec(hostContext, mode, &evmcMessage, code.data(), code.size());

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
    catch (PermissionDenied const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::PermissionDenied;
        revert();

        return callResults;
    }
    catch (PrecompiledError const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::PrecompiledError;
        revert();

        return callResults;
    }
    catch (OutOfGas& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        revert();
        EXECUTIVE_LOG(ERROR) << "Out of gas (" << *boost::get_error_info<errinfo_evmcStatusCode>(_e)
                             << ")\n"
                             << diagnostic_information(_e);

        return callResults;
    }
    catch (bcos::Error& e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::RevertInstruction;
        callResults->message = e.errorMessage();

        EXECUTIVE_LOG(ERROR) << "BCOS Error: " << diagnostic_information(e);
        revert();

        if (e.errorCode() == DEAD_LOCK)
        {
            // DEAD LOCK revert need provide sender and receiver
            EXECUTOR_LOG(ERROR) << "Revert by dead lock, sender: " << callResults->senderAddress
                                << " receiver: " << callResults->receiveAddress;
        }

        return callResults;
    }
    catch (InternalVMError const& _e)
    {
        auto callResults = hostContext.takeCallParameters();
        callResults->type = CallParameters::REVERT;
        using errinfo_evmcStatusCode =
            boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;
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

void TransactionExecutive::spawnAndCall(std::function<void(ResumeHandler)> function)
{
    (*m_pushMessage)(std::move(function));
}

std::shared_ptr<precompiled::PrecompiledExecResult> TransactionExecutive::execPrecompiled(
    const std::string& address, bytesConstRef param, const std::string& origin,
    const std::string& sender)
{
    try
    {
        auto p = getPrecompiled(address);

        if (p)
        {
            auto execResult = p->call(shared_from_this(), param, origin, sender);
            return execResult;
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << LOG_DESC("[call]Can't find address")
                                 << LOG_KV("address", address);
            return nullptr;
        }
    }
    catch (protocol::PrecompiledError& e)
    {
        const string* _msg = boost::get_error_info<errinfo_comment>(e);
        EXECUTIVE_LOG(ERROR) << "PrecompiledError" << LOG_KV("address", address)
                             << LOG_KV("message:", _msg ? *_msg : "");
        BOOST_THROW_EXCEPTION(e);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << LOG_DESC("[call]Precompiled call error")
                             << LOG_KV("EINFO", boost::diagnostic_information(e));

        throw PrecompiledError();
    }
}

bool TransactionExecutive::isPrecompiled(const std::string& address) const
{
    return m_constantPrecompiled.count(address) > 0;
}

std::shared_ptr<Precompiled> TransactionExecutive::getPrecompiled(const std::string& address) const
{
    auto constantPrecompiled = m_constantPrecompiled.find(address);

    if (constantPrecompiled != m_constantPrecompiled.end())
    {
        return constantPrecompiled->second;
    }
    return {};
}

bool TransactionExecutive::isBuiltInPrecompiled(const std::string& _a) const
{
    std::stringstream prefix;
    prefix << std::setfill('0') << std::setw(36);
    if (_a.find(prefix.str()) != 0)
        return false;
    return m_builtInPrecompiled->find(_a) != m_builtInPrecompiled->end();
}

bool TransactionExecutive::isEthereumPrecompiled(const string& _a) const
{
    std::stringstream prefix;
    prefix << std::setfill('0') << std::setw(39);
    if (_a.find(prefix.str()) != 0)
        return false;
    return m_evmPrecompiled->find(_a) != m_evmPrecompiled->end();
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
    m_constantPrecompiled.insert(std::make_pair(address, precompiled));
}
void TransactionExecutive::setConstantPrecompiled(
    std::map<std::string, std::shared_ptr<precompiled::Precompiled>> _constantPrecompiled)
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
        // FIXME: Copy the output for now, but copyless version possible.
        callResults->gas = _result.gasLeft();
        revert();
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
        // m_remainGas = 0; //TODO: why set remainGas to 0?
        callResults->status = (int32_t)TransactionStatus::BadInstruction;
        revert();
        break;
    }
    case EVMC_BAD_JUMP_DESTINATION:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::BadJumpDestination;
        revert();
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::OutOfStack;
        revert();
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
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
        revert();
        if (_result.status() <= EVMC_INTERNAL_ERROR)
        {
            BOOST_THROW_EXCEPTION(InternalVMError{} << errinfo_evmcStatusCode(_result.status()));
        }
        else
        {  // These cases aren't really internal errors, just more specific
           // error codes returned by the VM. Map all of them to OOG.m_externalCallCallback
            BOOST_THROW_EXCEPTION(OutOfGas());
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
    if (_tableName.substr(0, 5) == "/sys/" || getContractTableName(_sender).substr(0, 5) == "/sys/")
    {
        return;
    }
    auto authTableName = std::string(_tableName).append(CONTRACT_SUFFIX);
    // if contract external create contract, then inheritance admin
    std::string_view admin;
    if (_sender != _origin)
    {
        auto senderAuthTable = getContractTableName(_sender).append(CONTRACT_SUFFIX);
        auto entry = m_storageWrapper->getRow(std::move(senderAuthTable), ADMIN_FIELD);
        admin = entry->getField(0);
    }
    else
    {
        admin = _sender;
    }
    auto table = m_storageWrapper->createTable(authTableName, STORAGE_VALUE);

    if (table)
    {
        Entry adminEntry(table->tableInfo());
        adminEntry.importFields({std::string(admin)});
        m_storageWrapper->setRow(authTableName, ADMIN_FIELD, std::move(adminEntry));

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

bool TransactionExecutive::buildBfsPath(std::string const& _absoluteDir)
{
    if (_absoluteDir.empty())
    {
        return false;
    }
    // transfer /usr/local/bin => ["usr", "local", "bin"]
    std::vector<std::string> dirList;
    std::string absoluteDir = _absoluteDir;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    boost::split(dirList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    std::string root = "/";

    for (size_t i = 0; i < dirList.size(); i++)
    {
        auto dir = dirList.at(i);
        auto table = m_storageWrapper->openTable(root);
        if (!table)
        {
            EXECUTIVE_LOG(ERROR) << LOG_BADGE("recursiveBuildDir")
                                 << LOG_DESC("can not open path table")
                                 << LOG_KV("tableName", root);
            return false;
        }
        if (root != "/")
        {
            root += "/";
        }
        auto typeEntry = table->getRow(FS_KEY_TYPE);
        if (typeEntry)
        {
            // can get type, then this type is directory
            // try open root + dir
            auto nextDirTable = storage().openTable(root + dir);
            if (i != dirList.size() - 1 && nextDirTable.has_value())
            {
                // root + dir table exist, try to get type entry
                auto tryGetTypeEntry = nextDirTable->getRow(FS_KEY_TYPE);
                if (tryGetTypeEntry.has_value() && tryGetTypeEntry->getField(0) == FS_TYPE_DIR)
                {
                    // if success and dir is directory, continue
                    root += dir;
                    continue;
                }
                else
                {
                    // can not get type, it means this dir is not a directory
                    EXECUTIVE_LOG(ERROR)
                        << LOG_BADGE("recursiveBuildDir")
                        << LOG_DESC("file had already existed, and not directory type")
                        << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
                    return false;
                }
            }

            // root + dir not exist, create root + dir and build bfs info in root table
            auto subEntry = table->getRow(FS_KEY_SUB);
            auto&& out = asBytes(std::string(subEntry->getField(0)));
            // codec to map
            std::map<std::string, std::string> bfsInfo;
            codec::scale::decode(bfsInfo, gsl::make_span(out));
            if (i == dirList.size() - 1)
            {
                // add bfs contract info to root
                bfsInfo.insert(std::make_pair(dir, FS_TYPE_CONTRACT));
            }
            else
            {
                /// create table and build bfs info
                bfsInfo.insert(std::make_pair(dir, FS_TYPE_DIR));
                auto newTable = m_storageWrapper->createTable(root + dir, SYS_VALUE_FIELDS);
                Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
                std::map<std::string, std::string> newSubMap;
                tEntry.importFields({FS_TYPE_DIR});
                newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
                aclTypeEntry.importFields({"0"});
                aclWEntry.importFields({""});
                aclBEntry.importFields({""});
                extraEntry.importFields({""});
                newTable->setRow(FS_KEY_TYPE, std::move(tEntry));
                newTable->setRow(FS_KEY_SUB, std::move(newSubEntry));
                newTable->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
                newTable->setRow(FS_ACL_WHITE, std::move(aclWEntry));
                newTable->setRow(FS_ACL_BLACK, std::move(aclBEntry));
                newTable->setRow(FS_KEY_EXTRA, std::move(extraEntry));
            }
            subEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
            table->setRow(FS_KEY_SUB, std::move(subEntry.value()));
            root += dir;
        }
        else
        {
            EXECUTIVE_LOG(ERROR) << LOG_BADGE("recursiveBuildDir")
                                 << LOG_DESC("file had already existed, and not directory type")
                                 << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
            return false;
        }
    }
    return true;
}

bool TransactionExecutive::checkAuth(
    const CallParameters::UniquePtr& callParameters, bool _isCreate)
{
    if (callParameters->staticCall)
        return true;
    auto blockContext = m_blockContext.lock();
    auto contractAuthPrecompiled =
        std::make_shared<ContractAuthPrecompiled>(blockContext->hashHandler());
    Address address(callParameters->origin);
    auto path = string(callParameters->codeAddress);
    EXECUTIVE_LOG(DEBUG) << "check auth" << LOG_KV("codeAddress", path)
                         << LOG_KV("isCreate", _isCreate) << LOG_KV("originAddress", address.hex());
    if (_isCreate)
    {
        return contractAuthPrecompiled->checkDeployAuth(shared_from_this(), address);
    }
    bytesRef func = ref(callParameters->data).getCroppedData(0, 4);
    return contractAuthPrecompiled->checkMethodAuth(shared_from_this(), path, func, address);
}