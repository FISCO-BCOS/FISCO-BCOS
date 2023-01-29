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
 * @brief vm common
 * @file Common.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 * @brief vm common
 * @file Common.h
 * @author: ancelmo
 * @date: 2021-10-08
 */

#pragma once

#include "CallParameters.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-utilities/Exceptions.h>
#include <evmc/instructions.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <set>

namespace bcos
{
DERIVE_BCOS_EXCEPTION(PermissionDenied);
DERIVE_BCOS_EXCEPTION(InternalVMError);
DERIVE_BCOS_EXCEPTION(InvalidInputSize);
DERIVE_BCOS_EXCEPTION(InvalidEncoding);

namespace executor
{

using bytes_view = std::basic_string_view<uint8_t>;

#define EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR")
#define EXECUTOR_BLK_LOG(LEVEL, number) EXECUTOR_LOG(LEVEL) << BLOCK_NUMBER(number)
#define EXECUTOR_NAME_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR:" + std::to_string(m_schedulerTermId))
#define COROUTINE_TRACE_LOG(LEVEL, contextID, seq) \
    BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR") << "[" << (contextID) << "," << (seq) << "]"
#define PARA_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARA") << LOG_BADGE(utcTime())


static constexpr std::string_view USER_TABLE_PREFIX = "/tables/";
static constexpr std::string_view USER_APPS_PREFIX = "/apps/";
static constexpr std::string_view USER_SYS_PREFIX = "/sys/";
static constexpr std::string_view USER_USR_PREFIX = "/usr/";
static constexpr std::string_view USER_SHARD_PREFIX = "/shards/";

static const char* const STORAGE_VALUE = "value";
static const char* const ACCOUNT_CODE_HASH = "codeHash";
static const char* const ACCOUNT_CODE = "code";
static const char* const ACCOUNT_BALANCE = "balance";
static const char* const ACCOUNT_ABI = "abi";
static const char* const ACCOUNT_NONCE = "nonce";
static const char* const ACCOUNT_ALIVE = "alive";
static const char* const ACCOUNT_FROZEN = "frozen";
static const char* const ACCOUNT_SHARD = "shard";

/// auth
static constexpr const std::string_view CONTRACT_SUFFIX = "_accessAuth";
static constexpr const std::string_view ADMIN_FIELD = "admin";
static constexpr const std::string_view STATUS_FIELD = "status";
static constexpr const std::string_view METHOD_AUTH_TYPE = "method_auth_type";
static constexpr const std::string_view METHOD_AUTH_WHITE = "method_auth_white";
static constexpr const std::string_view METHOD_AUTH_BLACK = "method_auth_black";

/// account
static constexpr const std::string_view ACCOUNT_STATUS = "status";
static constexpr const std::string_view ACCOUNT_LAST_UPDATE = "last_update";
static constexpr const std::string_view ACCOUNT_LAST_STATUS = "last_status";
enum AccountStatus : uint8_t
{
    normal = 0,
    freeze = 1,
    abolish = 2,
};

/// contract status
static constexpr const std::string_view CONTRACT_NORMAL = "normal";
static constexpr const std::string_view CONTRACT_FROZEN = "frozen";
static constexpr const std::string_view CONTRACT_ABOLISH = "abolish";
static constexpr inline std::string_view StatusToString(uint8_t _status) noexcept
{
    switch (_status)
    {
    case 0:
        return CONTRACT_NORMAL;
    case 1:
        return CONTRACT_FROZEN;
    case 2:
        return CONTRACT_ABOLISH;
    default:
        return "";
    }
}

/// FileSystem table keys
static const char* const FS_KEY_NAME = "name";
static const char* const FS_KEY_TYPE = "type";
static const char* const FS_KEY_SUB = "sub";
static const char* const FS_ACL_TYPE = "acl_type";
static const char* const FS_ACL_WHITE = "acl_white";
static const char* const FS_ACL_BLACK = "acl_black";
static const char* const FS_KEY_EXTRA = "extra";
static const char* const FS_LINK_ADDRESS = "link_address";
static const char* const FS_LINK_ABI = "link_abi";

/// FileSystem file type
static const char* const FS_TYPE_DIR = "directory";
static const char* const FS_TYPE_CONTRACT = "contract";
static const char* const FS_TYPE_LINK = "link";

#define EXECUTIVE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[EXECUTOR]"

struct GlobalHashImpl
{
    static crypto::Hash::Ptr g_hashImpl;
};

struct SubState
{
    std::set<std::string> suicides;  ///< Any accounts that have suicided.
    protocol::LogEntriesPtr logs = std::make_shared<protocol::LogEntries>();  ///< Any logs.
    u256 refunds;  ///< Refund counter of SSTORE nonzero->zero.

    SubState& operator+=(SubState const& _s)
    {
        suicides += _s.suicides;
        refunds += _s.refunds;
        *logs += *_s.logs;
        return *this;
    }

    void clear()
    {
        suicides.clear();
        logs->clear();
        refunds = 0;
    }
};

struct VMSchedule
{
    VMSchedule() {}
    bool exceptionalFailedCodeDeposit = true;
    bool enableLondon = true;
    bool enablePairs = false;
    unsigned sstoreRefundGas = 15000;
    unsigned suicideRefundGas = 24000;
    unsigned createDataGas = 20;
    unsigned maxEvmCodeSize = 0x40000;
    unsigned maxWasmCodeSize = 0xF00000;  // 15MB

};

static const VMSchedule FiscoBcosSchedule = [] {
    VMSchedule schedule = VMSchedule();
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleV320 = [] {
    VMSchedule schedule = VMSchedule();
    schedule.enablePairs = true;
    schedule.maxEvmCodeSize = 0x100000;  // 1MB
    schedule.maxWasmCodeSize = 0xF00000;  // 15MB
    return schedule;
}();

protocol::TransactionStatus toTransactionStatus(Exception const& _e);

}  // namespace executor

bool hasWasmPreamble(const std::string_view& _input);
bool hasWasmPreamble(const bytesConstRef& _input);
bool hasWasmPreamble(const bytes& _input);
bool hasPrecompiledPrefix(const std::string_view& _code);
/**
 * @brief : trans string addess to evm address
 * @param _addr : the string address
 * @return evmc_address : the transformed evm address
 */
inline evmc_address toEvmC(const std::string_view& addr)
{  // TODO: add another interfaces for wasm
    evmc_address ret;
    constexpr static auto evmAddressLength = sizeof(ret);

    if (addr.size() < evmAddressLength)
    {
        std::uninitialized_fill_n(ret.bytes, evmAddressLength, 0);
    }
    else
    {
        std::uninitialized_copy_n(addr.begin(), evmAddressLength, ret.bytes);
    }
    return ret;
}

/**
 * @brief : trans ethereum hash to evm hash
 * @param _h : hash value
 * @return evmc_bytes32 : transformed hash
 */
inline evmc_bytes32 toEvmC(h256 const& hash)
{
    evmc_bytes32 evmBytes;
    static_assert(sizeof(evmBytes) == h256::SIZE, "Hash size mismatch!");
    std::uninitialized_copy(hash.begin(), hash.end(), evmBytes.bytes);
    return evmBytes;
}
/**
 * @brief : trans uint256 number of evm-represented to u256
 * @param _n : the uint256 number that can parsed by evm
 * @return u256 : transformed u256 number
 */
inline u256 fromEvmC(evmc_bytes32 const& _n)
{
    return fromBigEndian<u256>(_n.bytes);
}

/**
 * @brief : trans evm address to ethereum address
 * @param _addr : the address that can parsed by evm
 * @return string_view : the transformed ethereum address
 */
inline std::string_view fromEvmC(evmc_address const& _addr)
{
    return {(char*)_addr.bytes, 20};
}

inline std::string fromBytes(const bytes& _addr)
{
    return {(char*)_addr.data(), _addr.size()};
}

inline std::string fromBytes(const bytesConstRef& _addr)
{
    return _addr.toString();
}

inline bytes toBytes(const std::string_view& _addr)
{
    return {(char*)_addr.data(), (char*)(_addr.data() + _addr.size())};
}

inline std::string getContractTableName(
    const std::string_view& prefix, const std::string_view& _address)
{
    std::string out;
    if (_address[0] == '/')
    {
        out.reserve(prefix.size() + _address.size() - 1);
        std::copy(prefix.begin(), prefix.end(), std::back_inserter(out));
        std::copy(_address.begin() + 1, _address.end(), std::back_inserter(out));
    }
    else
    {
        out.reserve(prefix.size() + _address.size());
        std::copy(prefix.begin(), prefix.end(), std::back_inserter(out));
        std::copy(_address.begin(), _address.end(), std::back_inserter(out));
    }

    return out;
}

}  // namespace bcos
