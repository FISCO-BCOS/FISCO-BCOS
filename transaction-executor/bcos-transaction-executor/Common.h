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

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmc/instructions.h>
#include <boost/algorithm/string/case_conv.hpp>

namespace bcos::transaction_executor
{
DERIVE_BCOS_EXCEPTION(PermissionDenied);
DERIVE_BCOS_EXCEPTION(InternalVMError);
DERIVE_BCOS_EXCEPTION(InvalidInputSize);
DERIVE_BCOS_EXCEPTION(InvalidEncoding);

inline auto getLogger(LogLevel level)
{
    BOOST_LOG_SEV(bcos::FileLoggerHandler, (boost::log::trivial::severity_level)(level));
    return bcos::FileLoggerHandler;
}

static constexpr std::string_view USER_TABLE_PREFIX = "/tables/";
static constexpr std::string_view USER_APPS_PREFIX = "/apps/";
static constexpr std::string_view USER_SYS_PREFIX = "/sys/";
static constexpr std::string_view USER_USR_PREFIX = "/usr/";

static constexpr std::string_view STORAGE_VALUE = "value";
static constexpr std::string_view ACCOUNT_CODE_HASH = "codeHash";
static constexpr std::string_view ACCOUNT_CODE = "code";
static constexpr std::string_view ACCOUNT_BALANCE = "balance";
static constexpr std::string_view ACCOUNT_ABI = "abi";
static constexpr std::string_view ACCOUNT_NONCE = "nonce";
static constexpr std::string_view ACCOUNT_ALIVE = "alive";
static constexpr std::string_view ACCOUNT_FROZEN = "frozen";

/// auth
static constexpr std::string_view CONTRACT_SUFFIX = "_accessAuth";
static constexpr std::string_view ADMIN_FIELD = "admin";
static constexpr std::string_view STATUS_FIELD = "status";
static constexpr std::string_view METHOD_AUTH_TYPE = "method_auth_type";
static constexpr std::string_view METHOD_AUTH_WHITE = "method_auth_white";
static constexpr std::string_view METHOD_AUTH_BLACK = "method_auth_black";

/// account
static constexpr std::string_view ACCOUNT_STATUS = "status";
static constexpr std::string_view ACCOUNT_LAST_UPDATE = "last_update";
static constexpr std::string_view ACCOUNT_LAST_STATUS = "last_status";

enum AccountStatus
{
    normal = 0,
    freeze = 1,
    abolish = 2,
};

/// contract status
static constexpr const std::string_view CONTRACT_NORMAL = "normal";
static constexpr const std::string_view CONTRACT_FROZEN = "frozen";
static constexpr const std::string_view CONTRACT_ABOLISH = "abolish";

constexpr inline std::string_view statusToString(uint8_t _status) noexcept
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
constexpr static std::string_view FS_KEY_NAME = "name";
constexpr static std::string_view FS_KEY_TYPE = "type";
constexpr static std::string_view FS_KEY_SUB = "sub";
constexpr static std::string_view FS_ACL_TYPE = "acl_type";
constexpr static std::string_view FS_ACL_WHITE = "acl_white";
constexpr static std::string_view FS_ACL_BLACK = "acl_black";
constexpr static std::string_view FS_KEY_EXTRA = "extra";
constexpr static std::string_view FS_LINK_ADDRESS = "link_address";
constexpr static std::string_view FS_LINK_ABI = "link_abi";

/// FileSystem file type
constexpr static std::string_view FS_TYPE_DIR = "directory";
constexpr static std::string_view FS_TYPE_CONTRACT = "contract";
constexpr static std::string_view FS_TYPE_LINK = "link";

#define EXECUTIVE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[EXECUTOR]"

struct GlobalHashImpl
{
    static bcos::crypto::Hash::Ptr g_hashImpl;
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
    VMSchedule() : tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}) {}
    VMSchedule(bool _efcd, bool _hdc, unsigned const& _txCreateGas)
      : tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}),
        exceptionalFailedCodeDeposit(_efcd),
        haveDelegateCall(_hdc),
        txCreateGas(_txCreateGas)
    {}

    std::array<unsigned, 8> tierStepGas;
    bool exceptionalFailedCodeDeposit = true;
    bool haveDelegateCall = false;
    bool eip150Mode = true;
    bool eip158Mode = true;
    bool haveBitwiseShifting = false;
    bool haveRevert = true;
    bool haveReturnData = true;
    bool haveStaticCall = true;
    bool haveCreate2 = true;
    bool haveExtcodehash = false;
    bool enableIstanbul = false;
    bool enableLondon = false;
    /// gas cost for specified calculation
    /// exp gas cost
    constexpr static unsigned expGas = 10;
    unsigned expByteGas = 10;
    /// sha3 gas cost
    constexpr static unsigned sha3Gas = 30;
    constexpr static unsigned sha3WordGas = 6;
    /// load/store gas cost
    unsigned sloadGas = 50;
    constexpr static unsigned sstoreSetGas = 20000;
    constexpr static unsigned sstoreResetGas = 5000;
    constexpr static unsigned sstoreRefundGas = 15000;
    /// jump gas cost
    constexpr static unsigned jumpdestGas = 1;
    /// log gas cost
    constexpr static unsigned logGas = 375;
    constexpr static unsigned logDataGas = 8;
    constexpr static unsigned logTopicGas = 375;
    /// creat contract gas cost
    constexpr static unsigned createGas = 32000;
    /// call function of contract gas cost
    unsigned callGas = 40;
    constexpr static unsigned callStipend = 2300;
    constexpr static unsigned callValueTransferGas = 9000;
    constexpr static unsigned callNewAccountGas = 25000;

    constexpr static unsigned suicideRefundGas = 24000;
    constexpr static unsigned memoryGas = 3;
    constexpr static unsigned quadCoeffDiv = 512;
    constexpr static unsigned createDataGas = 20;
    /// transaction related gas
    constexpr static unsigned txGas = 21000;
    unsigned txCreateGas = 53000;
    unsigned txDataZeroGas = 4;
    constexpr static unsigned txDataNonZeroGas = 68;
    constexpr static unsigned copyGas = 3;
    /// extra code related gas
    unsigned extcodesizeGas = 20;
    unsigned extcodecopyGas = 20;
    constexpr static unsigned extcodehashGas = 400;
    unsigned balanceGas = 20;
    unsigned suicideGas = 0;
    unsigned blockhashGas = 20;
    unsigned maxCodeSize = unsigned(-1);

    // boost::optional<u256> blockRewardOverwrite;

    bool staticCallDepthLimit() const { return !eip150Mode; }
    bool suicideChargesNewAccountGas() const { return eip150Mode; }
    bool emptinessIsNonexistence() const { return eip158Mode; }
    bool zeroValueTransferChargesNewAccountGas() const { return !eip158Mode; }
};

/// exceptionalFailedCodeDeposit: false
/// haveDelegateCall: false
/// tierStepGas: {0, 2, 3, 5, 8, 10, 20, 0}
/// txCreateGas: 21000
static const VMSchedule FrontierSchedule = VMSchedule(false, false, 21000);
/// value of params are equal to HomesteadSchedule
static const VMSchedule HomesteadSchedule = VMSchedule(true, true, 53000);
/// EIP150(refer to:
/// https://github.com/ethereum/EIPs/blob/master/EIPS/eip-150.md)
static const VMSchedule EIP150Schedule = [] {
    VMSchedule schedule = HomesteadSchedule;
    schedule.eip150Mode = true;
    schedule.extcodesizeGas = 700;
    schedule.extcodecopyGas = 700;
    schedule.balanceGas = 400;
    schedule.sloadGas = 200;
    schedule.callGas = 700;
    schedule.suicideGas = 5000;
    return schedule;
}();
/// EIP158
static const VMSchedule EIP158Schedule = [] {
    VMSchedule schedule = EIP150Schedule;
    schedule.expByteGas = 50;
    schedule.eip158Mode = true;
    schedule.maxCodeSize = 0x6000;
    return schedule;
}();

static const VMSchedule ByzantiumSchedule = [] {
    VMSchedule schedule = EIP158Schedule;
    schedule.haveRevert = true;
    schedule.haveReturnData = true;
    schedule.haveStaticCall = true;
    // schedule.blockRewardOverwrite = {3 * ether};
    return schedule;
}();

static const VMSchedule ConstantinopleSchedule = [] {
    VMSchedule schedule = ByzantiumSchedule;
    schedule.blockhashGas = 800;
    schedule.haveCreate2 = true;
    schedule.haveBitwiseShifting = true;
    schedule.haveExtcodehash = true;
    return schedule;
}();

static const VMSchedule FiscoBcosSchedule = [] {
    VMSchedule schedule = ConstantinopleSchedule;
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleV2 = [] {
    VMSchedule schedule = ConstantinopleSchedule;
    schedule.maxCodeSize = 0x40000;
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleV3 = [] {
    VMSchedule schedule = FiscoBcosScheduleV2;
    schedule.enableIstanbul = true;
    return schedule;
}();

static const VMSchedule FiscoBcosScheduleV4 = [] {
    VMSchedule schedule = FiscoBcosScheduleV3;
    schedule.enableLondon = true;
    return schedule;
}();

static const VMSchedule BCOSWASMSchedule = [] {
    VMSchedule schedule = FiscoBcosScheduleV4;
    schedule.maxCodeSize = 0xF00000;  // 15MB
    // Ensure that zero bytes are not subsidised and are charged the same as
    // non-zero bytes.
    schedule.txDataZeroGas = schedule.txDataNonZeroGas;
    return schedule;
}();

static const VMSchedule DefaultSchedule = FiscoBcosScheduleV4;

protocol::TransactionStatus toTransactionStatus(Exception const& _e);

bool hasWasmPreamble(const std::string_view& _input);
bool hasWasmPreamble(const bcos::bytesConstRef& _input);
bool hasWasmPreamble(const bcos::bytes& _input);
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

/**
 * @brief : trans ethereum hash to evm hash
 * @param _h : hash value
 * @return evmc_bytes32 : transformed hash
 */
inline evmc_bytes32 toEvmC(bcos::h256 const& hash)
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

inline std::string getContractTableName(const std::string_view& _address)
{
    constexpr static std::string_view prefix("/apps/");
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
}  // namespace bcos::transaction_executor