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

#include "bcos-concepts/ByteBuffer.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <iterator>
#include <limits>
#include <memory>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/view/drop.hpp>
#include <set>
#include <bcos-utilities/BoostLog.h>

namespace bcos
{
DERIVE_BCOS_EXCEPTION(PermissionDenied);
DERIVE_BCOS_EXCEPTION(InternalVMError);
DERIVE_BCOS_EXCEPTION(InvalidInputSize);
DERIVE_BCOS_EXCEPTION(InvalidEncoding);

namespace executor
{

constexpr static evmc_address EMPTY_EVM_ADDRESS = {};
constexpr static evmc_bytes32 EMPTY_EVM_BYTES32 = {};
constexpr static evmc_uint256be EMPTY_EVM_UINT256 = {};
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

static constexpr std::string_view STORAGE_VALUE = "value";
static constexpr std::string_view ACCOUNT_CODE_HASH = "codeHash";
static constexpr std::string_view ACCOUNT_CODE = "code";
static constexpr std::string_view ACCOUNT_ABI = "abi";
static constexpr std::string_view ACCOUNT_NONCE = "nonce";
static constexpr std::string_view ACCOUNT_ALIVE = "alive";
static constexpr std::string_view ACCOUNT_FROZEN = "frozen";
static constexpr std::string_view ACCOUNT_SHARD = "shard";

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
static constexpr const std::string_view ACCOUNT_BALANCE = "balance";

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
static constexpr std::string_view FS_KEY_NAME = "name";
static constexpr std::string_view FS_KEY_TYPE = "type";
static constexpr std::string_view FS_KEY_SUB = "sub";
static constexpr std::string_view FS_ACL_TYPE = "acl_type";
static constexpr std::string_view FS_ACL_WHITE = "acl_white";
static constexpr std::string_view FS_ACL_BLACK = "acl_black";
static constexpr std::string_view FS_KEY_EXTRA = "extra";
static constexpr std::string_view FS_LINK_ADDRESS = "link_address";
static constexpr std::string_view FS_LINK_ABI = "link_abi";

/// FileSystem file type
static constexpr std::string_view FS_TYPE_DIR = "directory";
static constexpr std::string_view FS_TYPE_CONTRACT = "contract";
static constexpr std::string_view FS_TYPE_LINK = "link";

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
    bool exceptionalFailedCodeDeposit = true;
    bool enableLondon = true;
    bool enablePairs = false;
    bool enableCanCun = false;
    bool enablePrague = false;
    bool enableOsaka = false;
    unsigned sstoreRefundGas = 15000;
    unsigned suicideRefundGas = 24000;
    unsigned createDataGas = 20;
    unsigned maxEvmCodeSize = 0x40000;
};

static const VMSchedule FiscoBcosSchedule = [] {
    VMSchedule schedule;
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleV320 = [] {
    VMSchedule schedule;
    schedule.enablePairs = true;
    schedule.maxEvmCodeSize = 0x100000;  // 1MB
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleCancun = [] {
    VMSchedule schedule;
    schedule.enableCanCun = true;
    schedule.maxEvmCodeSize = 0x100000;  // 1MB
    return schedule;
}();

static const VMSchedule FiscoBcosSchedulePrague = [] {
    VMSchedule schedule;
    schedule.enablePairs = true;
    schedule.enableCanCun = true;
    schedule.enablePrague = true;
    schedule.maxEvmCodeSize = 0x100000;  // 1MB
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleOsaka = [] {
    VMSchedule schedule;
    schedule.enablePairs = true;
    schedule.enableCanCun = true;
    schedule.enablePrague = true;
    schedule.enableOsaka = true;
    schedule.maxEvmCodeSize = 0x100000;  // 1MB
    return schedule;
}();

constexpr static int64_t BALANCE_TRANSFER_GAS = 21000;

// EIP-7623: calldata floor cost constants (Prague+)
// token = 1 for zero byte, TOKENS_PER_NONZERO_BYTE for non-zero byte; floor = tokens * 10
constexpr static int64_t TOKENS_PER_NONZERO_BYTE = 4;  // EIP-7623 token weight for non-zero byte
constexpr static int64_t TOTAL_COST_FLOOR_PER_TOKEN = 10;  // EIP-7623 floor cost per token

/// EIP-7623 calldata floor: max(standard calldata gas, tokens * 10).
inline int64_t calcEip7623CalldataGas(bcos::bytesConstRef data)
{
    constexpr auto maxSafeBytes =
        static_cast<size_t>(std::numeric_limits<int64_t>::max() /
                            (TOKENS_PER_NONZERO_BYTE * TOTAL_COST_FLOOR_PER_TOKEN));
    if (data.size() > maxSafeBytes)
    {
        return std::numeric_limits<int64_t>::max();
    }

    int64_t normalDataCost = 0;
    int64_t numTokens = 0;
    for (auto byte : data)
    {
        if (byte == 0)
        {
            normalDataCost += 4;
            ++numTokens;
        }
        else
        {
            normalDataCost += 16;
            numTokens += TOKENS_PER_NONZERO_BYTE;
        }
    }
    return std::max(normalDataCost, numTokens * TOTAL_COST_FLOOR_PER_TOKEN);
}


protocol::TransactionStatus toTransactionStatus(Exception const& _e);

enum ExecutiveType : uint8_t
{
    common = 0,
    coroutine = 1,
    billing = 2
};

}  // namespace executor

bool hasPrecompiledPrefix(const std::string_view& _code);
/**
 * @brief : trans string addess to evm address
 * @param _addr : the string address
 * @return evmc_address : the transformed evm address
 */
inline evmc_address toEvmC(const std::string_view& addr)
{
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

inline evmc_uint256be toEvmC(const u256& _n)
{
    evmc_uint256be ret;
    auto gasPriceBytes = toBigEndian(_n);
    ::ranges::copy(gasPriceBytes, ret.bytes);
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
    ::ranges::copy(hash, evmBytes.bytes);
    return evmBytes;
}
/// Convert a 20-byte bcos::Address to evmc_address (binary copy, no hex encoding).
inline evmc_address toEvmC(bcos::Address const& address)
{
    evmc_address out{};
    static_assert(sizeof(out.bytes) == bcos::Address::SIZE, "Address size mismatch!");
    ::ranges::copy(address, out.bytes);
    return out;
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
        ::ranges::copy(prefix, std::back_inserter(out));
        ::ranges::copy(::ranges::views::drop(_address, 1), std::back_inserter(out));
    }
    else
    {
        out.reserve(prefix.size() + _address.size());
        ::ranges::copy(prefix, std::back_inserter(out));
        ::ranges::copy(_address, std::back_inserter(out));
    }

    return out;
}

bytes getComponentBytes(size_t index, const std::string& typeName, const bytesConstRef& data);
evmc_address unhexAddress(std::string_view view);
std::string addressBytesStr2HexString(std::string_view receiveAddressBytes);
std::string address2HexString(const evmc_address& address);
std::array<char, sizeof(evmc_address) * 2> address2HexArray(const evmc_address& address);

task::Task<bool> checkTransferPermission(auto& storage, std::string_view sender)
{
    if (co_await storage2::existsOne(
            storage, executor_v1::StateKeyView{ledger::SYS_BALANCE_CALLER, sender}))
    {
        co_return true;
    }
    co_return false;
}

task::Task<bool> checkTransferPermission(auto& storage, evmc_address sender)
{
    auto hexAddress = address2HexArray(sender);
    co_return co_await checkTransferPermission(storage, concepts::bytebuffer::toView(hexAddress));
}

}  // namespace bcos
