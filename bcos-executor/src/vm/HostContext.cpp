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
 * @file HostContext.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "HostContext.h"
#include "../Common.h"
#include "../executive/TransactionExecutive.h"
#include "EVMHostInterface.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include "evmc/evmc.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/thread.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <exception>
#include <iterator>
#include <limits>
#include <sstream>
#include <vector>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

namespace  // anonymous
{
/// Upper bound of stack space needed by single CALL/CREATE execution. Set
/// experimentally.
// static size_t const c_singleExecutionStackSize = 100 * 1024;

static const std::string SYS_ASSET_NAME = "name";
static const std::string SYS_ASSET_FUNGIBLE = "fungible";
static const std::string SYS_ASSET_TOTAL = "total";
static const std::string SYS_ASSET_SUPPLIED = "supplied";
static const std::string SYS_ASSET_ISSUER = "issuer";
static const std::string SYS_ASSET_DESCRIPTION = "description";
static const std::string SYS_ASSET_INFO = "_sys_asset_info_";

}  // anonymous namespace

namespace bcos
{
namespace executor
{
namespace
{
evmc_gas_metrics ethMetrics{32000, 20000, 5000, 200, 9000, 2300, 25000};

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(HostContext::hashImpl()->hash(bytesConstRef(data, size)));
}
}  // namespace

// crypto::Hash::Ptr g_hashImpl = nullptr;

HostContext::HostContext(CallParameters::UniquePtr callParameters,
    std::shared_ptr<TransactionExecutive> executive, std::string tableName)
  : m_callParameters(std::move(callParameters)),
    m_executive(std::move(executive)),
    m_tableName(tableName)
{
    interface = getHostInterface();
    wasm_interface = getWasmHostInterface();

    hash_fn = evm_hash_fn;
    version = 0x03000000;
    isSMCrypto = false;

    if (hashImpl() && hashImpl()->getHashImplType() == crypto::HashImplType::Sm3Hash)
    {
        isSMCrypto = true;
    }
    metrics = &ethMetrics;
    m_startTime = utcTimeUs();
}

std::string HostContext::get(const std::string_view& _key)
{
    auto start = utcTimeUs();
    auto entry = m_executive->storage().getRow(m_tableName, _key);
    if (entry)
    {
        m_getTimeUsed.fetch_add(utcTimeUs() - start);
        return std::string(entry->getField(0));
    }
    m_getTimeUsed.fetch_add(utcTimeUs() - start);
    return std::string();
}

void HostContext::set(const std::string_view& _key, std::string _value)
{
    auto start = utcTimeUs();
    Entry entry;
    entry.importFields({std::move(_value)});

    m_executive->storage().setRow(m_tableName, _key, std::move(entry));
    m_setTimeUsed.fetch_add(utcTimeUs() - start);
}

evmc_result HostContext::externalRequest(const evmc_message* _msg)
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
            auto receiveAddressBytes = fromEvmC(_msg->destination);
            request->receiveAddress.reserve(receiveAddressBytes.size() * 2);
            boost::algorithm::hex_lower(receiveAddressBytes.begin(), receiveAddressBytes.end(),
                std::back_inserter(request->receiveAddress));
        }

        request->codeAddress = request->receiveAddress;
        request->data.assign(_msg->input_data, _msg->input_data + _msg->input_size);
        break;
    case EVMC_DELEGATECALL:
    case EVMC_CALLCODE:
        // TODO: implement this, don't forget the compatibility
        evmc_result result;
        result.status_code = evmc_status_code(EVMC_INVALID_INSTRUCTION);
        result.release = nullptr;  // no output to release
        result.gas_left = 0;
        return result;
    case EVMC_CREATE:
        request->data.assign(_msg->input_data, _msg->input_data + _msg->input_size);
        request->create = true;
        break;
    }
    request->gas = _msg->gas;
    // if (built in precompiled) then execute locally
    auto blockContext = m_executive->blockContext().lock();

    if (m_executive->isBuiltInPrecompiled(request->receiveAddress))
    {
        return callBuiltInPrecompiled(request, false);
    }
    if (m_executive->isEthereumPrecompiled(request->receiveAddress) && !blockContext->isWasm())
    {
        return callBuiltInPrecompiled(request, true);
    }

    request->staticCall = m_callParameters->staticCall;

    auto response = m_executive->externalCall(std::move(request));

    // Convert CallParameters to evmc_result
    evmc_result result;
    result.status_code = evmc_status_code(response->status);

    result.create_address =
        toEvmC(boost::algorithm::unhex(response->newEVMContractAddress));  // TODO: check if ok

    // TODO: check if the response data need to release
    result.output_data = response->data.data();
    result.output_size = response->data.size();
    result.release = nullptr;  // Response own by HostContext
    result.gas_left = response->gas;

    // Put response to store in order to avoid data lost
    m_responseStore.emplace_back(std::move(response));

    return result;
}

evmc_result HostContext::callBuiltInPrecompiled(
    std::unique_ptr<CallParameters> const& _request, bool _isEvmPrecompiled)
{
    auto callResults = std::make_unique<CallParameters>(CallParameters::FINISHED);
    evmc_result preResult{};
    int32_t resultCode;
    bytes resultData;

    if (_isEvmPrecompiled)
    {
        callResults->gas =
            m_executive->costOfPrecompiled(_request->receiveAddress, ref(_request->data));
        auto [success, output] =
            m_executive->executeOriginPrecompiled(_request->receiveAddress, ref(_request->data));
        resultCode =
            (int32_t)(success ? TransactionStatus::None : TransactionStatus::RevertInstruction);
        resultData.swap(output);
    }
    else
    {
        try
        {
            auto precompiledCallParams =
                std::make_shared<precompiled::PrecompiledExecResult>(_request);
            precompiledCallParams = m_executive->execPrecompiled(precompiledCallParams);
            callResults->gas = precompiledCallParams->m_gas;
            resultCode = (int32_t)TransactionStatus::None;
            resultData = std::move(precompiledCallParams->m_execResult);
        }
        catch (protocol::PrecompiledError& e)
        {
            resultCode = (int32_t)TransactionStatus::PrecompiledError;
        }
        catch (std::exception& e)
        {
            resultCode = (int32_t)TransactionStatus::Unknown;
        }
    }

    if (resultCode != (int32_t)TransactionStatus::None)
    {
        callResults->type = CallParameters::REVERT;
        callResults->status = resultCode;
        preResult.status_code = EVMC_INTERNAL_ERROR;
        preResult.gas_left = 0;
        m_responseStore.emplace_back(std::move(callResults));
        return preResult;
    }

    preResult.gas_left = callResults->gas;
    if (preResult.gas_left < 0)
    {
        callResults->type = CallParameters::REVERT;
        callResults->status = (int32_t)TransactionStatus::OutOfGas;
        preResult.status_code = EVMC_OUT_OF_GAS;
        preResult.gas_left = 0;
        return preResult;
    }
    callResults->status = (int32_t)TransactionStatus::None;
    callResults->data.swap(resultData);
    preResult.output_size = callResults->data.size();
    preResult.output_data = callResults->data.data();
    preResult.release = nullptr;
    m_responseStore.emplace_back(std::move(callResults));
    return preResult;
}

bool HostContext::setCode(bytes code)
{
    // set code will cause exception when exec revert
    auto table = m_executive->storage().openTable(m_tableName);
    if (table)
    {
        Entry codeHashEntry;
        auto codeHash = hashImpl()->hash(code);
        codeHashEntry.importFields({codeHash.asBytes()});
        m_executive->storage().setRow(m_tableName, ACCOUNT_CODE_HASH, std::move(codeHashEntry));

        Entry codeEntry;
        codeEntry.importFields({std::move(code)});
        m_executive->storage().setRow(m_tableName, ACCOUNT_CODE, std::move(codeEntry));
        return true;
    }
    return false;
}

void HostContext::setCodeAndAbi(bytes code, string abi)
{
    EXECUTOR_LOG(TRACE) << LOG_DESC("save code and abi") << LOG_KV("tableName", m_tableName)
                        << LOG_KV("codeSize", code.size()) << LOG_KV("abiSize", abi.size());
    if (setCode(std::move(code)))
    {
        Entry abiEntry;
        abiEntry.importFields({std::move(abi)});
        m_executive->storage().setRow(m_tableName, ACCOUNT_ABI, abiEntry);
    }
}

size_t HostContext::codeSizeAt(const std::string_view& _a)
{
    (void)_a;
    return 1;  // TODO: 1 code size ok?
}

h256 HostContext::codeHashAt(const std::string_view& _a)
{
    (void)_a;
    return h256();  // TODO: ok?
}

VMSchedule const& HostContext::vmSchedule() const
{
    return m_executive->vmSchedule();
}

u256 HostContext::store(const u256& _n)
{
    auto start = utcTimeUs();

    auto key = toEvmC(_n);
    auto keyView = std::string_view((char*)key.bytes, sizeof(key.bytes));

    auto entry = m_executive->storage().getRow(m_tableName, keyView);
    if (entry)
    {
        // if (c_fileLogLevel >= bcos::LogLevel::TRACE)
        // {  // FIXME: this log is only for debug, comment it when release
        //     EXECUTOR_LOG(TRACE) << LOG_DESC("store") << LOG_KV("key", toHex(keyView))
        //                         << LOG_KV("value", toHex(entry->get()));
        // }
        m_getTimeUsed.fetch_add(utcTimeUs() - start);
        return fromBigEndian<u256>(entry->getField(0));
    }
    // else
    // {// FIXME: this log is only for debug, comment it when release
    //     EXECUTOR_LOG(TRACE) << LOG_DESC("store") << LOG_KV("key", toHex(keyView))
    //                         << LOG_KV("value", "not found");
    // }
    m_getTimeUsed.fetch_add(utcTimeUs() - start);
    return u256();
}

void HostContext::setStore(u256 const& _n, u256 const& _v)
{
    auto start = utcTimeUs();
    auto key = toEvmC(_n);
    auto keyView = std::string_view((char*)key.bytes, sizeof(key.bytes));

    auto value = toEvmC(_v);
    bytes valueBytes(value.bytes, value.bytes + sizeof(value.bytes));

    // if (c_fileLogLevel >= bcos::LogLevel::TRACE)
    // {  // FIXME: this log is only for debug, comment it when release
    //     EXECUTOR_LOG(TRACE) << LOG_DESC("setStore") << LOG_KV("key", toHex(keyView))
    //                         << LOG_KV("value", toHex(valueBytes));
    // }
    Entry entry;
    entry.importFields({std::move(valueBytes)});
    m_executive->storage().setRow(m_tableName, keyView, std::move(entry));
    m_setTimeUsed.fetch_add(utcTimeUs() - start);
}

void HostContext::log(h256s&& _topics, bytesConstRef _data)
{
    // if (m_isWasm || myAddress().empty())
    // {
    //     m_sub.logs->push_back(
    //         protocol::LogEntry(bytes(myAddress().data(), myAddress().data() +
    //         myAddress().size()),
    //             std::move(_topics), _data.toBytes()));
    // }
    // else
    // {
    //     // convert solidity address to hex string
    //     auto hexAddress = *toHexString(myAddress());
    //     boost::algorithm::to_lower(hexAddress);  // this is in case of toHexString be modified
    //     toChecksumAddress(hexAddress, hashImpl()->hash(hexAddress).hex());
    //     m_sub.logs->push_back(
    //         protocol::LogEntry(asBytes(hexAddress), std::move(_topics), _data.toBytes()));
    // }
    m_callParameters->logEntries.emplace_back(
        bytes(myAddress().data(), myAddress().data() + myAddress().size()), std::move(_topics),
        _data.toBytes());
}

h256 HostContext::blockHash() const
{
    return m_executive->blockContext().lock()->hash();
}
int64_t HostContext::blockNumber() const
{
    return m_executive->blockContext().lock()->number();
}
int64_t HostContext::timestamp() const
{
    return m_executive->blockContext().lock()->timestamp();
}

std::string_view HostContext::myAddress() const
{
    return m_executive->contractAddress();
}

std::optional<storage::Entry> HostContext::code()
{
    auto start = utcTimeUs();
    auto entry = m_executive->storage().getRow(m_tableName, ACCOUNT_CODE);
    m_getTimeUsed.fetch_add(utcTimeUs() - start);
    return entry;
}

h256 HostContext::codeHash()
{
    auto entry = m_executive->storage().getRow(m_tableName, ACCOUNT_CODE_HASH);
    if (entry)
    {
        auto code = entry->getField(0);

        return h256(std::string(code));  // TODO: h256 support decode from string_view
    }

    return h256();
}

bool HostContext::registerAsset(const std::string& _assetName, const std::string_view& _addr,
    bool _fungible, uint64_t _total, const std::string& _description)
{
    (void)_assetName;
    (void)_addr;
    (void)_fungible;
    (void)_total;
    (void)_description;

    return true;
}

bool HostContext::issueFungibleAsset(
    const std::string_view& _to, const std::string& _assetName, uint64_t _amount)
{
    (void)_to;
    (void)_assetName;
    (void)_amount;

    return true;
}

uint64_t HostContext::issueNotFungibleAsset(
    const std::string_view& _to, const std::string& _assetName, const std::string& _uri)
{
    (void)_to;
    (void)_assetName;
    (void)_uri;
    return 0;
}

void HostContext::depositFungibleAsset(
    const std::string_view& _to, const std::string& _assetName, uint64_t _amount)
{
    (void)_to;
    (void)_assetName;
    (void)_amount;
}

void HostContext::depositNotFungibleAsset(const std::string_view& _to,
    const std::string& _assetName, uint64_t _assetID, const std::string& _uri)
{
    (void)_to;
    (void)_assetName;
    (void)_assetID;
    (void)_uri;
}

bool HostContext::transferAsset(const std::string_view& _to, const std::string& _assetName,
    uint64_t _amountOrID, bool _fromSelf)
{
    (void)_to;
    (void)_assetName;
    (void)_amountOrID;
    (void)_fromSelf;
    return true;
}

uint64_t HostContext::getAssetBanlance(
    const std::string_view& _account, const std::string& _assetName)
{
    (void)_account;
    (void)_assetName;
    return 0;
}

std::string HostContext::getNotFungibleAssetInfo(
    const std::string_view& _owner, const std::string& _assetName, uint64_t _assetID)
{
    (void)_owner;
    (void)_assetName;
    (void)_assetID;
    return {};
}

std::vector<uint64_t> HostContext::getNotFungibleAssetIDs(
    const std::string_view& _account, const std::string& _assetName)
{
    (void)_account;
    (void)_assetName;
    return std::vector<uint64_t>();
}

bool HostContext::isWasm()
{
    return m_executive->isWasm();
}

}  // namespace executor
}  // namespace bcos