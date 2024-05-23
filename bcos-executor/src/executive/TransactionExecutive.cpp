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
#include "bcos-framework/ledger/Features.h"
#include "bcos-table/src/ContractShardUtils.h"

#ifdef WITH_WASM
#include "../vm/gas_meter/GasInjector.h"
#endif

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

    auto message = execute(std::move(callParameters));

    message->contextID = contextID();
    message->seq = seq();
    message->hasContractTableChanged = m_hasContractTableChanged;

    EXECUTIVE_LOG(TRACE) << "Execute finish\t" << message->toFullString();

    return message;
}

CallParameters::UniquePtr TransactionExecutive::externalCall(CallParameters::UniquePtr input)
{
    auto newSeq = seq() + 1;
    bool isCreate = input->create;
    input->seq = newSeq;
    input->contextID = m_contextID;

    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << "externalCall start\t" << input->toFullString();
    }

    std::string newAddress;
    // if internalCreate, sometimes it will use given address, if receiveAddress is empty then give
    // a new address
    if (isCreate && !m_blockContext.isWasm() && std::empty(input->receiveAddress))
    {
        if (input->createSalt)
        {
            newAddress = bcos::newEVMAddress(m_hashImpl, input->senderAddress,
                bytesConstRef(input->data.data(), input->data.size()), *(input->createSalt));
        }
        else
        {
            newAddress =
                bcos::newEVMAddress(m_hashImpl, m_blockContext.number(), m_contextID, newSeq);
        }

        input->receiveAddress = newAddress;
        input->codeAddress = newAddress;
    }

    if (input->delegateCall)
    {
        assert(!m_blockContext.isWasm());
        auto tableName = getContractTableName(input->codeAddress, false);

        bool needTryFromContractTable =
            m_blockContext.features().get(ledger::Features::Flag::bugfix_call_noaddr_return);
        auto [codeHash, codeEntry] =
            getCodeByContractTableName(tableName, needTryFromContractTable);
        if (codeEntry && codeEntry.has_value() && !codeEntry->get().empty())
        {
            input->delegateCallCodeHash = codeHash;
            input->delegateCallCode = toBytes(codeEntry->get());
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << "Could not getCode during externalCall"
                                 << LOG_KV("codeAddress", input->codeAddress)
                                 << LOG_KV("needTryFromContractTable", needTryFromContractTable);
            input->delegateCallCode = bytes();

            auto& output = input;
            output->data = bytes();
            if (m_blockContext.features().get(ledger::Features::Flag::bugfix_call_noaddr_return))
            {
                // This is eth's bug, but we still need to compat with it :)
                // https://docs.soliditylang.org/en/v0.8.17/control-structures.html#error-handling-assert-require-revert-and-exceptions
                output->status = (int32_t)TransactionStatus::None;
                output->evmStatus = EVMC_SUCCESS;
            }
            else
            {
                output->status = (int32_t)TransactionStatus::RevertInstruction;
                output->evmStatus = EVMC_REVERT;
            }
            return std::move(output);
        }
    }

    if (input->data == bcos::protocol::GET_CODE_INPUT_BYTES)
    {
        EXECUTIVE_LOG(DEBUG) << "Get external code request"
                             << LOG_KV("codeAddress", input->codeAddress);

        auto tableName = getContractTableName(input->codeAddress, false);
        auto& output = input;

        bool needTryFromContractTable =
            m_blockContext.features().get(ledger::Features::Flag::bugfix_call_noaddr_return);
        auto [codeHash, codeEntry] =
            getCodeByContractTableName(tableName, needTryFromContractTable);

        if (codeEntry && codeEntry.has_value() && !codeEntry->get().empty())
        {
            output->data = toBytes(codeEntry->get());
            return std::move(output);
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << "Could not get external code from local storage"
                                 << LOG_KV("codeAddress", input->codeAddress)
                                 << LOG_KV("needTryFromContractTable", needTryFromContractTable);

            output->data = bytes();
            return std::move(output);
        }
    }

    auto executive = buildChildExecutive(input->codeAddress, m_contextID, newSeq);

    m_childExecutives.push_back(executive);  // add child executive for revert() if needed

    auto output = executive->start(std::move(input));

    // update seq
    m_seq = executive->seq();

    // update hasContractTableChanged
    if (executive->hasContractTableChanged())
    {
        this->setContractTableChanged();
    }

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
                             << LOG_KV("blockNumber", m_blockContext.number());
    }
    m_storageWrapper->setRecoder(m_recoder);
    auto transientStorage = getTransientStateStorage(m_contextID);
    transientStorage->setRecoder(m_transientRecoder);
    std::unique_ptr<HostContext> hostContext;
    CallParameters::UniquePtr callResults;
    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << LOG_DESC("Execute begin")
                             << LOG_KV(
                                    "feature_balance", m_blockContext.features().get(
                                                           ledger::Features::Flag::feature_balance))
                             << LOG_KV("value", callParameters->value);
    }

    if (m_blockContext.features().get(ledger::Features::Flag::feature_balance_policy1))
    {
        // policy1 disable transfer balance
        callParameters->value = 0;
    }

    if (m_blockContext.features().get(ledger::Features::Flag::feature_balance) &&
        callParameters->value > 0)
    {
        bool onlyTransfer = callParameters->data.empty();
        if (m_blockContext.features().get(
                ledger::Features::Flag::bugfix_support_transfer_receive_fallback))
        {
            // Note: To support receive fallback.
            // Transfer from EOA or contract tx's data is empty. But we still need to executive as
            // an normal transaction.
            // If "to" is contract, go to contract's receive() fallback.
            // If "to" is EOA, go to AccountPrecompiled receive() logic.
            onlyTransfer = false;
        }


        bool transferFromEVM = callParameters->seq != 0;
        int64_t requiredGas = transferFromEVM ? 0 : BALANCE_TRANSFER_GAS;
        auto currentContextAddress = callParameters->receiveAddress;

        callParameters =
            transferBalance(std::move(callParameters), requiredGas, currentContextAddress);

        if (callParameters->status != (int32_t)TransactionStatus::None)
        {
            revert();  // direct transfer need revert by hand
            EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Execute")
                                 << LOG_DESC("transferBalance failed and will revert");
            return callParameters;
        }

        if (onlyTransfer)
        {
            callParameters->type = CallParameters::FINISHED;
            callParameters->status = (int32_t)TransactionStatus::None;
            callParameters->evmStatus = EVMC_SUCCESS;
            // only transfer, not call contract
            return callParameters;
        }
    }

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

        // TODO: check this function is ok if we need to use this
        hostContext->sub().refunds +=
            hostContext->vmSchedule().suicideRefundGas * hostContext->sub().suicides.size();
    }
    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << LOG_DESC("Execute finished")
                             << LOG_KV("callResults", callResults->toFullString())
                             << LOG_KV("blockNumber", m_blockContext.number());
    }
    return callResults;
}


crypto::HashType TransactionExecutive::getCodeHash(const std::string_view& contractTableName)
{
    auto entry = storage().getRow(contractTableName, ACCOUNT_CODE_HASH);
    if (entry)
    {
        auto code = entry->getField(0);
        return crypto::HashType(code, crypto::HashType::StringDataType::FromBinary);
    }

    return {};
}

std::optional<storage::Entry> TransactionExecutive::getCodeEntryFromContractTable(
    const std::string_view contractTableName)
{
    return storage().getRow(contractTableName, ACCOUNT_CODE);
}

std::optional<storage::Entry> TransactionExecutive::getCodeByHash(const std::string_view& codeHash)
{
    auto entry = storage().getRow(bcos::ledger::SYS_CODE_BINARY, codeHash);
    if (entry && !entry->get().empty())
    {
        return entry;
    }
    return {};
}

std::tuple<h256, std::optional<storage::Entry>> TransactionExecutive::getCodeByContractTableName(
    const std::string_view& contractTableName, bool tryFromContractTable)
{
    auto hash = getCodeHash(contractTableName);
    auto entry = getCodeByHash(std::string_view((char*)hash.data(), hash.size()));
    if (entry && entry.has_value() && !entry->get().empty())
    {
        return {hash, std::move(entry)};
    }

    if (tryFromContractTable)
    {
        return {hash, getCodeEntryFromContractTable(contractTableName)};
    }
    return {};
}

bool TransactionExecutive::setCode(std::string_view contractTableName,
    std::variant<std::string_view, std::string, bcos::bytes> code)
{
    auto contractTable = storage().openTable(contractTableName);
    // set code hash in contract table
    if (contractTable)
    {
        crypto::HashType codeHash;
        Entry codeEntry;
        std::visit(
            [&codeHash, &codeEntry, this](auto&& code) {
                codeHash = m_hashImpl->hash(code);
                codeEntry.importFields({std::move(code)});
            },
            code);
        Entry codeHashEntry;
        codeHashEntry.importFields({codeHash.asBytes()});

        // not exist in code binary table, set it
        if (!storage().getRow(bcos::ledger::SYS_CODE_BINARY, codeHash.toRawString()))
        {
            // set code in code binary table
            storage().setRow(
                bcos::ledger::SYS_CODE_BINARY, codeHash.toRawString(), std::move(codeEntry));
        }

        // dry code hash in account table
        storage().setRow(contractTableName, ACCOUNT_CODE_HASH, std::move(codeHashEntry));
        return true;
    }
    return false;
}

void TransactionExecutive::setAbiByCodeHash(std::string_view codeHash, std::string_view abi)
{
    if (!storage().getRow(bcos::ledger::SYS_CONTRACT_ABI, codeHash))
    {
        Entry abiEntry;
        abiEntry.importFields({std::move(abi)});

        storage().setRow(bcos::ledger::SYS_CONTRACT_ABI, codeHash, std::move(abiEntry));
    }
}


CallParameters::UniquePtr TransactionExecutive::transferBalance(
    CallParameters::UniquePtr callParameters, int64_t requireGas,
    std::string_view currentContextAddress)
{
    // origin: SDK account(EOA)
    // sender: account to sub
    // receiver: account to add
    auto& addAccount = callParameters->receiveAddress;
    auto& subAccount = callParameters->senderAddress;
    auto& origin = callParameters->origin;
    auto value = callParameters->value;
    auto& myAddress = currentContextAddress;

    // check enough gas
    if (callParameters->gas < requireGas)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Execute") << "out of gas in transferBalance"
                             << LOG_KV("origin", origin) << LOG_KV("subAccount", subAccount)
                             << LOG_KV("addAccount", addAccount) << LOG_KV("value", value)
                             << LOG_KV("gas", callParameters->gas)
                             << LOG_KV("requireGas", requireGas);
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::OutOfGas;
        callParameters->evmStatus = EVMC_OUT_OF_GAS;
        return callParameters;
    }
    else
    {
        callParameters->gas -= requireGas;
    }

    auto gas = callParameters->gas;


    EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << "now to transferBalance"
                         << LOG_KV("origin", origin) << LOG_KV("subAccount", subAccount)
                         << LOG_KV("addAccount", addAccount) << LOG_KV("value", value)
                         << LOG_KV("gas", gas);

    // first subAccountBalance, then addAccountBalance
    // sender = sender - value
    auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
    auto params = codec.encodeWithSig("subAccountBalance(uint256)", value);
    auto formTableName = getContractTableName(subAccount, m_blockContext.isWasm());
    std::vector<std::string> fromTableNameVector = {formTableName};
    auto inputParams = codec.encode(fromTableNameVector, params);
    auto subParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams);
    EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << "transferBalance start, now is sub."
                         << LOG_KV("tableName", formTableName) << LOG_KV("will sub balance", value);

    auto reposeSub = externalRequest(shared_from_this(), ref(subParams),
        std::string(BALANCE_PRECOMPILED_ADDRESS), myAddress, subAccount, false, false, gas, true);

    if (reposeSub->status != (int32_t)TransactionStatus::None)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Execute") << LOG_DESC("transferBalance sub failed")
                             << LOG_KV("subAccount", origin) << LOG_KV("tablename", formTableName);
        return reposeSub;
    }

    // to add balance
    // receiver = receiver + value
    auto params1 = codec.encodeWithSig("addAccountBalance(uint256)", value);
    auto toTableName = getContractTableName(addAccount, m_blockContext.isWasm());
    std::vector<std::string> toTableNameVector = {toTableName};
    auto inputParams1 = codec.encode(toTableNameVector, params1);
    auto addParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams1);

    EXECUTIVE_LOG(TRACE) << LOG_BADGE("Execute") << "transferBalance start, now is add."
                         << LOG_KV("tableName", toTableName) << LOG_KV("will add balance", value);
    auto reposeAdd = externalRequest(shared_from_this(), ref(addParams),
        std::string(BALANCE_PRECOMPILED_ADDRESS), myAddress, addAccount, false, false, gas, true);

    if (reposeAdd->status != (int32_t)TransactionStatus::None)
    {
        EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Execute")
                             << LOG_DESC("transferBalance add failed, need to restore")
                             << LOG_KV("tableName", formTableName)
                             << LOG_KV("will add balance", value);

        // if receiver add failed, sender need to restore
        // sender = sender + value
        auto revertParams = codec.encode(fromTableNameVector, params1);
        auto addParams1 = codec.encode(std::string(ACCOUNT_ADDRESS), revertParams);
        auto reponseRestore = externalRequest(shared_from_this(), ref(addParams1),
            std::string(BALANCE_PRECOMPILED_ADDRESS), myAddress, subAccount, false, false, gas,
            true);
        if (reponseRestore->status != (int32_t)TransactionStatus::None)
        {
            EXECUTIVE_LOG(DEBUG)
                << LOG_BADGE("Execute")
                << LOG_DESC(
                       "transferBalance to sub success but add failed, strike a balance failed.")
                << LOG_KV("restoreAccount", subAccount) << LOG_KV("tablename", formTableName);
            BOOST_THROW_EXCEPTION(PrecompiledError(
                "transferBalance to sub success but add failed, strike a balance failed."));
        }

        return reposeAdd;
    }
    EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Execute") << "transferBalance success"
                         << LOG_KV("subAccount", subAccount) << LOG_KV("addAccount", addAccount)
                         << LOG_KV("value", value) << LOG_KV("gas", gas);


    return callParameters;
}


std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> TransactionExecutive::call(
    CallParameters::UniquePtr callParameters)
{
    EXECUTIVE_LOG(DEBUG) << BLOCK_NUMBER(m_blockContext.number()) << LOG_DESC("executive call")
                         << LOG_KV("contract", callParameters->receiveAddress)
                         << LOG_KV("sender", callParameters->senderAddress)
                         << LOG_KV("internalCall", callParameters->internalCall)
                         << LOG_KV("delegateCall", callParameters->delegateCall)
                         << LOG_KV("codeAddress", callParameters->codeAddress);

    auto tableName = getContractTableName(callParameters->receiveAddress, m_blockContext.isWasm());
    // delegateCall is just about to replace code, no need to check permission beforehand
    if (callParameters->delegateCall)
    {
        auto hostContext = make_unique<DelegateHostContext>(
            std::move(callParameters), shared_from_this(), std::move(tableName));
        return {std::move(hostContext), nullptr};
    }
    // check permission first
    if (m_blockContext.isAuthCheck() && !checkAuth(callParameters))
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
    auto precompiledCallParams = std::make_shared<PrecompiledExecResult>(*callParameters);
    bytes data{};
    if (callParameters->internalCall)
    {
        std::string contract;
        auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
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
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Call precompiled out of gas.", *callParameters);
            }
            return callParameters;
        }
        precompiledCallParams->takeDataToCallParameter(callParameters);
    }
    // NotEnoughCashError
    catch (protocol::NotEnoughCashError const& e)
    {
        EXECUTIVE_LOG(INFO) << "Revert transaction: "
                            << "NotEnoughCashError"
                            << LOG_KV("address", precompiledCallParams->m_precompiledAddress)
                            << LOG_KV("message", e.what());
        writeErrInfoToOutput(e.what(), *callParameters);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::NotEnoughCash;
        callParameters->evmStatus = EVMC_INSUFFICIENT_BALANCE;
        callParameters->message = e.what();
    }
    catch (protocol::PrecompiledError const& e)
    {
        EXECUTIVE_LOG(INFO) << "Revert transaction: "
                            << "PrecompiledFailed"
                            << LOG_KV("address", precompiledCallParams->m_precompiledAddress)
                            << LOG_KV("message", e.what());
        // Note: considering the scenario where the contract calls the contract, the error message
        // still needs to be written to the output
        writeErrInfoToOutput(e.what(), *callParameters);
        revert();
        callParameters->type = CallParameters::REVERT;
        callParameters->status = (int32_t)TransactionStatus::PrecompiledError;
        if (m_blockContext.blockVersion() >= (uint32_t)(bcos::protocol::BlockVersion::V3_6_VERSION))
        {
            callParameters->evmStatus = EVMC_REVERT;
        }
        callParameters->message = e.what();
    }
    catch (Exception const& e)
    {
        EXECUTIVE_LOG(WARNING) << "Exception"
                               << LOG_KV("address", precompiledCallParams->m_precompiledAddress)
                               << LOG_KV("message", e.what());
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
        writeErrInfoToOutput("InternalPrecompiledFailed", *callParameters);
        EXECUTIVE_LOG(WARNING) << LOG_DESC("callPrecompiled")
                               << LOG_KV("message", boost::diagnostic_information(e));
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
    auto tableName = getContractTableName(
        callParameters->codeAddress, m_blockContext.isWasm(), callParameters->create);
    auto extraData = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    extraData->abi = std::move(callParameters->abi);

    EXECUTIVE_LOG(DEBUG) << BLOCK_NUMBER(m_blockContext.number())
                         << LOG_DESC("executive deploy contract") << LOG_KV("tableName", tableName)
                         << LOG_KV("abi len", extraData->abi.size())
                         << LOG_KV("sender", callParameters->senderAddress)
                         << LOG_KV("internalCreate", callParameters->internalCreate);

    // check permission first
    if (m_blockContext.isAuthCheck() && !checkAuth(callParameters))
    {
        revert();
        return {nullptr, std::move(callParameters)};
    }

    this->setContractTableChanged();

    if (callParameters->internalCreate)
    {
        callParameters->abi = std::move(extraData->abi);
        auto sender = callParameters->senderAddress;
        auto response = internalCreate(std::move(callParameters));
        // 3.1.0 < version < 3.3, authCheck==true, then create
        // version >= 3.3, always create
        if ((m_blockContext.isAuthCheck() &&
                m_blockContext.blockVersion() >= BlockVersion::V3_1_VERSION) ||
            (!m_blockContext.isWasm() &&
                m_blockContext.blockVersion() >= BlockVersion::V3_3_VERSION))
        {
            // Create auth table
            creatAuthTable(
                tableName, response->origin, std::move(sender), m_blockContext.blockVersion());
        }
        return {nullptr, std::move(response)};
    }
    // Create table
    try
    {
        if (callParameters->value == 0)
        {
            // only create table when value is 0 need to create table, if value > 0, table is
            // created in accountPrecompiled addBalance()
            m_storageWrapper->createTable(tableName, std::string(STORAGE_VALUE));


            EXECUTIVE_LOG(DEBUG) << "create contract table " << LOG_KV("table", tableName)
                                 << LOG_KV("sender", callParameters->senderAddress);
            if (m_blockContext.isAuthCheck() ||
                (!m_blockContext.isWasm() &&
                    m_blockContext.blockVersion() >= BlockVersion::V3_3_VERSION))
            {
                // Create auth table, always create auth table when version >= 3.3.0
                creatAuthTable(tableName, callParameters->origin, callParameters->senderAddress,
                    m_blockContext.blockVersion());
            }
        }
        else
        {
            EXECUTIVE_LOG(DEBUG) << "no need create contract table when deploy with value "
                                 << LOG_KV("table", tableName)
                                 << LOG_KV("sender", callParameters->senderAddress)
                                 << LOG_KV("value", callParameters->value);
        }

        if (m_blockContext.features().get(ledger::Features::Flag::feature_sharding))
        {
            if (callParameters->origin != callParameters->senderAddress)
            {
                // should contract create contract
                auto parentTableName =
                    getContractTableName(callParameters->senderAddress, m_blockContext.isWasm());
                storage::ContractShardUtils::setContractShardByParent(
                    *m_storageWrapper, parentTableName, tableName);
            }
        }
    }
    catch (exception const& e)
    {
        // this exception will be frequent to happened in liquid
        revert();
        callParameters->status = (int32_t)TransactionStatus::ContractAddressAlreadyUsed;
        callParameters->type = CallParameters::REVERT;
        callParameters->message = e.what();
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Contract address already used.", *callParameters);
        }
        EXECUTIVE_LOG(INFO) << "Revert transaction: " << LOG_DESC("createTable failed")
                            << callParameters->message << LOG_KV("tableName", tableName);
        return {nullptr, std::move(callParameters)};
    }

#ifdef WITH_WASM
    if (m_blockContext.isWasm())
    {
        // Liquid
        std::tuple<bytes, bytes> input;
        auto codec = CodecWrapper(m_blockContext.hashHandler(), true);
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
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput(
                    "WASM bytecode invalid or use unsupported opcode.", *callResults);
            }
            return {nullptr, std::move(callResults)};
        }

        auto result = m_gasInjector.InjectMeter(code);
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
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
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
#endif

    auto hostContext =
        std::make_unique<HostContext>(std::move(callParameters), shared_from_this(), tableName);
    return {std::move(hostContext), std::move(extraData)};
}

CallParameters::UniquePtr TransactionExecutive::internalCreate(
    CallParameters::UniquePtr callParameters)
{
    auto newAddress = string(callParameters->codeAddress);
    auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
    std::string tableName;
    std::string codeString;
    codec.decode(ref(callParameters->data), tableName, codeString);
    EXECUTIVE_LOG(DEBUG) << LOG_DESC("internalCreate") << LOG_KV("newAddress", newAddress)
                         << LOG_KV("codeString", codeString);

    if (m_blockContext.isWasm())
    {
        /// BFS create contract table and write metadata in parent table
        if (!buildBfsPath(newAddress, callParameters->origin, newAddress, FS_TYPE_CONTRACT,
                callParameters->gas))
        {
            revert();
            auto buildCallResults = std::move(callParameters);
            buildCallResults->type = CallParameters::REVERT;
            buildCallResults->status = (int32_t)TransactionStatus::RevertInstruction;
            buildCallResults->message = "Error occurs in build BFS dir";
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Error occurs in building BFS dir.", *buildCallResults);
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << buildCallResults->message
                                << LOG_KV("newAddress", newAddress);
            return buildCallResults;
        }
        /// create contract table
        m_storageWrapper->createTable(newAddress, std::string(STORAGE_VALUE));
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
            auto buildCallResults = std::move(callParameters);
            buildCallResults->type = CallParameters::REVERT;
            buildCallResults->status = (int32_t)TransactionStatus::RevertInstruction;
            buildCallResults->message = "Error occurs in build BFS dir";
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Error occurs in building BFS dir.", *buildCallResults);
            }
            EXECUTIVE_LOG(INFO) << "Revert transaction: " << buildCallResults->message
                                << LOG_KV("newAddress", newAddress);
            return buildCallResults;
        }

        /// create link table
        auto linkTable = m_storageWrapper->createTable(tableName, std::string(STORAGE_VALUE));

        /// create code index contract
        auto codeTable = getContractTableName(newAddress, false);
        m_storageWrapper->createTable(codeTable, std::string(STORAGE_VALUE));

        if (m_blockContext.features().get(
                ledger::Features::Flag::bugfix_internal_create_redundant_storage))
        {
            setCode(codeTable, std::move(codeString));
            if (!callParameters->abi.empty())
            {
                auto codeHash = m_hashImpl->hash(codeString);
                setAbiByCodeHash(codeHash.toRawString(), std::move(callParameters->abi));
            }
        }
        else
        {
            /// set code field
            Entry entry = {};
            entry.importFields({std::move(codeString)});
            m_storageWrapper->setRow(codeTable, ACCOUNT_CODE, std::move(entry));
            if (!callParameters->abi.empty() &&
                blockContext().blockVersion() != (uint32_t)protocol::BlockVersion::V3_0_VERSION)
            {
                Entry abiEntry = {};
                abiEntry.importFields({std::move(callParameters->abi)});
                m_storageWrapper->setRow(codeTable, ACCOUNT_ABI, std::move(abiEntry));
            }
        }

        if (blockContext().blockVersion() == (uint32_t)protocol::BlockVersion::V3_0_VERSION)
        {
            /// set link data
            Entry addressEntry = {};
            addressEntry.importFields({newAddress});
            m_storageWrapper->setRow(tableName, FS_LINK_ADDRESS, std::move(addressEntry));
            Entry typeEntry = {};
            typeEntry.importFields({FS_TYPE_LINK});
            m_storageWrapper->setRow(tableName, FS_KEY_TYPE, std::move(typeEntry));
        }
        else
        {
            /// set link data
            tool::BfsFileFactory::buildLink(
                linkTable.value(), newAddress, "", blockContext().blockVersion());
        }
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

            evmc_call_kind kind = hostContext.isCreate() ? EVMC_CREATE : EVMC_CALL;
            uint32_t flags = hostContext.staticCall() ? EVMC_STATIC : 0;
            // this is ensured by solidity compiler
            assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
            auto leftGas = hostContext.gas();

            evmc_message evmcMessage{};
            evmcMessage.kind = kind;
            evmcMessage.flags = flags;
            evmcMessage.depth = 0;  // depth own by scheduler
            evmcMessage.gas = leftGas;

            if (hostContext.features().get(ledger::Features::Flag::feature_balance))
            {
                evmcMessage.value = toEvmC(hostContext.value());
            }
            else
            {
                evmcMessage.value = toEvmC(h256(0));
            }
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

                if (hostContext.myAddress().size() < sizeof(evmcMessage.recipient) * 2)
                {
                    std::uninitialized_fill_n(
                        evmcMessage.recipient.bytes, sizeof(evmcMessage.recipient), 0);
                }
                else
                {
                    boost::algorithm::unhex(hostContext.myAddress(), evmcMessage.recipient.bytes);
                }

                if (hostContext.caller().size() < sizeof(evmcMessage.sender) * 2)
                {
                    std::uninitialized_fill_n(
                        evmcMessage.sender.bytes, sizeof(evmcMessage.sender), 0);
                }
                else
                {
                    boost::algorithm::unhex(hostContext.caller(), evmcMessage.sender.bytes);
                }
            }
            evmcMessage.code_address = evmcMessage.recipient;

            return evmcMessage;
        };

        if (hostContext.isCreate())
        {
            auto evmcMessage = getEVMCMessage(m_blockContext, hostContext);

            auto code = hostContext.data();
            auto vmKind = VMKind::evmone;

            if (m_blockContext.isWasm())
            {
                vmKind = VMKind::BcosWasm;
            }
            auto revision = toRevision(hostContext.vmSchedule());
            // the code evm uses to deploy is not runtime code, so create can not use cache
            auto vm = m_blockContext.getVMFactory()->create(
                vmKind, revision, crypto::HashType(), code, true);
            auto ret = vm.execute(hostContext, &evmcMessage);

            auto callResults = hostContext.takeCallParameters();
            // clear unnecessary logs
            if (m_blockContext.blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
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
                    << "Revert transaction: " << LOG_DESC("deploy failed due to status failed")
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
            auto maxCodeSize = m_blockContext.isWasm() ?
                                   m_blockContext.vmSchedule().maxWasmCodeSize :
                                   hostContext.vmSchedule().maxEvmCodeSize;
            if (outputRef.size() > maxCodeSize)
            {
                revert();
                callResults->type = CallParameters::REVERT;
                callResults->status = (int32_t)TransactionStatus::OutOfGas;
                callResults->message =
                    "Code is too large: " + boost::lexical_cast<std::string>(outputRef.size()) +
                    " limit: " + boost::lexical_cast<std::string>(maxCodeSize);
                if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >=
                    0)
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
                            m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                    {
                        writeErrInfoToOutput("Exceptional Failed Code Deposit", *callResults);
                    }
                    EXECUTIVE_LOG(INFO)
                        << "Revert transaction: " << LOG_DESC("deploy failed OutOfGas")
                        << LOG_KV("need",
                               (int64_t)(outputRef.size() * hostContext.vmSchedule().createDataGas))
                        << LOG_KV("have", callResults->gas)
                        << LOG_KV("message", callResults->message);
                    return callResults;
                }
            }

            if (m_blockContext.isWasm())
            {
                // BFS create contract table and write metadata in parent table
                auto tableName = getContractTableName(hostContext.myAddress(), true);
                if (!buildBfsPath(tableName, callResults->origin, hostContext.myAddress(),
                        FS_TYPE_CONTRACT, callResults->gas))
                {
                    revert();
                    auto buildCallResults = std::move(callResults);
                    buildCallResults->type = CallParameters::REVERT;
                    buildCallResults->status = (int32_t)TransactionStatus::RevertInstruction;
                    buildCallResults->message = "Error occurs in build BFS dir";
                    if (versionCompareTo(
                            m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
                    {
                        writeErrInfoToOutput(
                            "Error occurs in building BFS dir.", *buildCallResults);
                    }
                    EXECUTIVE_LOG(INFO) << "Revert transaction: " << buildCallResults->message
                                        << LOG_KV("tableName", tableName);
                    return buildCallResults;
                }
            }

            assert(extraData != nullptr);

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
                if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >=
                    0)
                {
                    writeErrInfoToOutput(
                        "Create contract with empty code, invalid code input.", *callResults);
                }
                else if (versionCompareTo(
                             m_blockContext.blockVersion(), BlockVersion::V3_0_VERSION) <= 0)
                {
                    callResults->data.clear();
                }
                revert();
                return callResults;
            }
            hostContext.setCodeAndAbi(outputRef.toBytes(), extraData->abi);

            callResults->gas -= outputRef.size() * hostContext.vmSchedule().createDataGas;
            callResults->newEVMContractAddress = callResults->codeAddress;

            // Clear the creation flag
            callResults->create = false;

            // Clear the data
            callResults->data.clear();

            return callResults;
        }
        else
        {  // execute
            auto codeEntry = hostContext.code();
            if (!codeEntry)
            {
                revert();
                auto callResult = hostContext.takeCallParameters();

                if (m_blockContext.features().get(
                        ledger::Features::Flag::bugfix_call_noaddr_return) &&
                    callResult->staticCall &&
                    callResult->seq > 0  // must staticCall from contract(not from rpc call)
                )
                {
                    // Note: to be the same as eth
                    // if bugfix_call_noaddr_return is not set, callResult->evmStatus is still
                    // default to EVMC_SUCCESS and serial mode is execute same as eth, but DMC is
                    // using callResult->status, so we need to compat with DMC here
                    if (m_blockContext.features().get(
                            ledger::Features::Flag::bugfix_staticcall_noaddr_return))
                    {
                        callResult->data = bytes();
                    }
                    callResult->type = CallParameters::FINISHED;
                    callResult->evmStatus = EVMC_SUCCESS;
                    callResult->status = (int32_t)TransactionStatus::None;
                    callResult->message = "Error contract address.";
                    return callResult;
                }

                callResult->type = CallParameters::REVERT;
                callResult->status = (int32_t)TransactionStatus::CallAddressError;
                callResult->message = "Error contract address.";
                if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >=
                    0)
                {
                    writeErrInfoToOutput("Call address error.", *callResult);
                }
                EXECUTIVE_LOG(INFO) << "Revert transaction: "
                                    << LOG_DESC("call address failed, maybe address not exist")
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
            auto revision = toRevision(hostContext.vmSchedule());
            auto vm = m_blockContext.getVMFactory()->create(vmKind, revision,
                hostContext.codeHash(), bytes_view((uint8_t*)code.data(), code.size()));
            auto evmcMessage = getEVMCMessage(m_blockContext, hostContext);
            auto ret = vm.execute(hostContext, &evmcMessage);
            auto callResults = hostContext.takeCallParameters();
            callResults = parseEVMCResult(std::move(callResults), ret);

            if (m_blockContext.blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
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
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
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
            m_blockContext.triggerSwitch();
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
    auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
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
    [[unlikely]] EXECUTIVE_LOG(WARNING)
        << LOG_DESC("[call]Can't find precompiled address")
        << LOG_KV("address", _precompiledParams->m_precompiledAddress);
    BOOST_THROW_EXCEPTION(PrecompiledError("can't find precompiled address."));
}

bool TransactionExecutive::isPrecompiled(const std::string& address) const
{
    return m_precompiled->at(address, m_blockContext.blockVersion(), m_blockContext.isAuthCheck(),
               m_blockContext.features()) != nullptr;
}

std::shared_ptr<Precompiled> TransactionExecutive::getPrecompiled(const std::string& address) const
{
    return m_precompiled->at(address, m_blockContext.blockVersion(), m_blockContext.isAuthCheck(),
        m_blockContext.features());
}

std::shared_ptr<precompiled::Precompiled> bcos::executor::TransactionExecutive::getPrecompiled(
    const std::string& address, uint32_t version, bool isAuth,
    const ledger::Features& features) const
{
    return m_precompiled->at(address, version, isAuth, features);
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
    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>> evmPrecompiled)
{
    m_evmPrecompiled = std::move(evmPrecompiled);
}

void TransactionExecutive::setPrecompiled(std::shared_ptr<PrecompiledMap> _precompiled)
{
    m_precompiled = std::move(_precompiled);
}

void TransactionExecutive::revert()
{
    EXECUTOR_BLK_LOG(INFO, m_blockContext.number())
        << "Revert transaction" << LOG_KV("contextID", m_contextID) << LOG_KV("seq", m_seq);

    if (m_blockContext.features().get(ledger::Features::Flag::bugfix_revert))
    {
        // revert child beforehand from back to front
        for (auto& childExecutive : RANGES::views::reverse(m_childExecutives))
        {
            childExecutive->revert();
        }
    }

    m_blockContext.storage()->rollback(*m_recoder);
    auto transientStateStorage = getTransientStateStorage(m_contextID);
    transientStateStorage->rollback(*m_transientRecoder);
    m_recoder->clear();
}

CallParameters::UniquePtr TransactionExecutive::parseEVMCResult(
    CallParameters::UniquePtr callResults, const Result& _result)
{
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
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVM_REVERT") << LOG_KV("to", callResults->receiveAddress)
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
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
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
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
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
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
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
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput(
                "Execution has violated the jump destination restrictions.", *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_STACK_OVERFLOW:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_STACK_OVERFLOW")
                            << LOG_KV("to", callResults->receiveAddress);
        // m_remainGas = 0;
        callResults->status = (int32_t)TransactionStatus::OutOfStack;
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution stack overflow.", *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_STACK_UNDERFLOW:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVMC_STACK_UNDERFLOW")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution needs more items on EVM stack.", *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_INVALID_MEMORY_ACCESS:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(INFO) << LOG_DESC("VM error, BufferOverrun")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::StackUnderflow;
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Execution tried to read outside memory bounds.", *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_STATIC_MODE_VIOLATION:
    {
        // m_remainGas = 0;
        EXECUTIVE_LOG(INFO) << LOG_DESC("VM error, DisallowedStateChange")
                            << LOG_KV("to", callResults->receiveAddress);
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput(
                "Execution tried to execute an operation which is restricted in static mode.",
                *callResults);
        }
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_staticcall_noaddr_return))
        {
            callResults->data = bytes();
        }
        callResults->status = (int32_t)TransactionStatus::Unknown;
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    {
        EXECUTIVE_LOG(INFO)
            << LOG_DESC("WASM validation failed, contract hash algorithm dose not match host.")
            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::WASMValidationFailure;
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput("Contract validation has failed.", *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_ARGUMENT_OUT_OF_RANGE:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("WASM Argument Out Of Range")
                            << LOG_KV("to", callResults->receiveAddress);
        callResults->status = (int32_t)TransactionStatus::WASMArgumentOutOfRange;
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            writeErrInfoToOutput(
                "An argument to a state accessing method has a value outside of the accepted range "
                "of values.",
                *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_WASM_TRAP:
    case EVMC_WASM_UNREACHABLE_INSTRUCTION:
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("WASM Unreachable/Trap Instruction")
                            << LOG_KV("to", callResults->receiveAddress)
                            << LOG_KV("status", _result.status());
        callResults->status = (int32_t)TransactionStatus::WASMUnreachableInstruction;
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0 &&
            versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_4_VERSION) < 0)
        {
            writeErrInfoToOutput("A WebAssembly trap has been hit during execution.", *callResults);
        }
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
        break;
    }
    case EVMC_INTERNAL_ERROR:
    default:
    {
        EXECUTIVE_LOG(WARNING) << LOG_DESC("EVMC_INTERNAL_ERROR/default revert")
                               << LOG_KV("to", callResults->receiveAddress)
                               << LOG_KV("status", _result.status());
        revert();
        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_evm_exception_gas_used))
        {
            callResults->gas = _result.gasLeft();
        }
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

void TransactionExecutive::creatAuthTable(std::string_view _tableName, std::string_view _origin,
    std::string_view _sender, uint32_t version)
{
    // Create the access table
    //  /sys/ not create
    if (version <=> BlockVersion::V3_3_VERSION >= 0 &&
        (_tableName.starts_with(precompiled::SYS_ADDRESS_PREFIX) ||
            _tableName.starts_with(USER_SYS_PREFIX)))
    {
        return;
    }
    if (version <=> BlockVersion::V3_3_VERSION < 0 &&
        (_tableName.starts_with(USER_SYS_PREFIX) ||
            _sender.starts_with(precompiled::SYS_ADDRESS_PREFIX)))
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
    auto table = m_storageWrapper->createTable(authTableName, std::string(STORAGE_VALUE));

    if (table) [[likely]]
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
    const auto* to = m_blockContext.isWasm() ? BFS_NAME : BFS_ADDRESS;
    auto response = externalTouchNewFile(
        shared_from_this(), _origin, _sender, to, _absoluteDir, _type, gasLeft);
    return response == (int)precompiled::CODE_SUCCESS;
}

bool TransactionExecutive::checkAuth(const CallParameters::UniquePtr& callParameters)
{
    // check account first
    if (m_blockContext.blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
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
        // if create contract, then check exec auth
        // if bugfix_internal_create_permission_denied is set, then internal create will not check
        if (m_blockContext.features().get(
                ledger::Features::Flag::bugfix_internal_create_permission_denied) &&
            callParameters->internalCreate)
        {
            return true;
        }
        if (!checkExecAuth(callParameters))
        {
            auto newAddress = string(callParameters->codeAddress);
            callParameters->status = (int32_t)TransactionStatus::PermissionDenied;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Create permission denied";
            callParameters->create = false;
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
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
            getContractTableName(callParameters->receiveAddress, m_blockContext.isWasm());
        // if call contract, then
        //      check contract available
        //      check exec auth
        auto contractStatus = checkContractAvailable(callParameters);
        if (contractStatus != static_cast<uint8_t>(ContractStatus::Available))
        {
            callParameters->status = (int32_t)TransactionStatus::ContractFrozen;
            callParameters->type = CallParameters::REVERT;
            callParameters->message = "Contract is frozen";
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_2_VERSION) >= 0)
            {
                if (contractStatus == static_cast<uint8_t>(ContractStatus::Abolish))
                {
                    callParameters->status = (int32_t)TransactionStatus::ContractAbolished;
                    callParameters->message = "Contract is abolished";
                    writeErrInfoToOutput("Contract is abolished.", *callParameters);
                }
            }
            else if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >=
                     0)
            {
                writeErrInfoToOutput("Contract is frozen.", *callParameters);
            }
            else if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_0_VERSION) <=
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
            if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
            {
                writeErrInfoToOutput("Call permission denied.", *callParameters);
            }
            else if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_0_VERSION) <=
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
    const auto* authMgrAddress = m_blockContext.isWasm() ? precompiled::AUTH_MANAGER_NAME :
                                                           precompiled::AUTH_MANAGER_ADDRESS;
    auto contractAuthPrecompiled = dynamic_pointer_cast<precompiled::ContractAuthMgrPrecompiled>(
        getPrecompiled(AUTH_CONTRACT_MGR_ADDRESS, m_blockContext.blockVersion(),
            m_blockContext.isAuthCheck(), m_blockContext.features()));
    EXECUTIVE_LOG(TRACE) << "check auth" << LOG_KV("codeAddress", callParameters->receiveAddress)
                         << LOG_KV("isCreate", callParameters->create)
                         << LOG_KV("originAddress", callParameters->origin);
    bool result = true;
    if (callParameters->create)
    {
        /// external call authMgrAddress to check deploy auth
        auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
        auto input =
            m_blockContext.isWasm() ?
                codec.encodeWithSig("hasDeployAuth(string)", callParameters->origin) :
                codec.encodeWithSig("hasDeployAuth(address)", Address(callParameters->origin));
        auto response = externalRequest(shared_from_this(), ref(input), callParameters->origin,
            callParameters->receiveAddress, authMgrAddress, false, false, callParameters->gas);
        codec.decode(ref(response->data), result);
    }
    else
    {
        bytesRef func = ref(callParameters->data).getCroppedData(0, 4);
        result = contractAuthPrecompiled->checkMethodAuth(
            shared_from_this(), callParameters->receiveAddress, func, callParameters->origin);
        if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_2_VERSION) >= 0 &&
            callParameters->origin != callParameters->senderAddress)
        {
            auto senderCheck = contractAuthPrecompiled->checkMethodAuth(shared_from_this(),
                callParameters->receiveAddress, func, callParameters->senderAddress);
            result = result && senderCheck;
        }
    }
    EXECUTIVE_LOG(TRACE) << "check auth finished"
                         << LOG_KV("codeAddress", callParameters->receiveAddress)
                         << LOG_KV("result", result);
    return result;
}

int32_t TransactionExecutive::checkContractAvailable(
    const CallParameters::UniquePtr& callParameters)
{
    // precompiled always available
    if (isPrecompiled(callParameters->receiveAddress) ||
        c_systemTxsAddress.contains(callParameters->receiveAddress))
    {
        return 0;
    }
    auto contractAuthPrecompiled = dynamic_pointer_cast<precompiled::ContractAuthMgrPrecompiled>(
        getPrecompiled(AUTH_CONTRACT_MGR_ADDRESS, m_blockContext.blockVersion(),
            m_blockContext.isAuthCheck(), m_blockContext.features()));
    // if status is normal, then return 0; else if status is abnormal, then return else
    // if return <0, it means status row not exist, check pass by default in this case
    auto status = contractAuthPrecompiled->getContractStatus(
        shared_from_this(), callParameters->receiveAddress);
    return status < 0 ? 0 : status;
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

    return precompiled::AccountPrecompiled::getAccountStatus(
        callParameters->origin, shared_from_this());
}

std::string TransactionExecutive::getContractTableName(
    const std::string_view& _address, bool isWasm, bool isCreate)
{
    auto version = m_blockContext.blockVersion();

    if (m_blockContext.isAuthCheck() ||
        protocol::versionCompareTo(version, protocol::BlockVersion::V3_3_VERSION) >= 0)
    {
        if (_address.starts_with(precompiled::SYS_ADDRESS_PREFIX))
        {
            return std::string(USER_SYS_PREFIX).append(_address);
        }
    }

    std::string_view formatAddress = _address;
    if (isWasm)
    {
        // NOTE: if version < 3.2, then it will allow deploying contracts under /tables. It's a
        // bug, but it should maintain data compatibility.
        // NOTE2: if it's internalCreate it should allow creating table under /tables
        if (protocol::versionCompareTo(version, protocol::BlockVersion::V3_2_VERSION) < 0 ||
            !isCreate)
        {
            if (_address.starts_with(USER_TABLE_PREFIX))
            {
                return std::string(formatAddress);
            }
        }
        formatAddress = formatAddress.starts_with('/') ? formatAddress.substr(1) : formatAddress;
    }

    return std::string(USER_APPS_PREFIX).append(formatAddress);
}

std::shared_ptr<storage::StateStorageInterface> TransactionExecutive::getTransientStateStorage(
    int64_t contextID)
{
    auto transientStorageMap = blockContext().getTransientStorageMap();
    bcos::storage::StateStorageInterface::Ptr transientStorage;
    bool has;
    {
        tssMap::ReadAccessor::Ptr readAccessor;
        has = transientStorageMap->find<tssMap::ReadAccessor>(readAccessor, contextID);
        if (has)
        {
            transientStorage = readAccessor->value();
        }
    }
    if (!has)
    {
        {
            tssMap::WriteAccessor::Ptr writeAccessor;
            auto hasWrite =
                transientStorageMap->find<tssMap::WriteAccessor>(writeAccessor, contextID);

            if (!hasWrite)
            {
                transientStorage = std::make_shared<bcos::storage::StateStorage>(nullptr, true);
                transientStorageMap->insert(writeAccessor, {contextID, transientStorage});
            }
            else
            {
                transientStorage = writeAccessor->value();
            }
        }
    }
    return transientStorage;
}
