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
#include <functional>
#include <set>

namespace bcos
{
DERIVE_BCOS_EXCEPTION(PermissionDenied);
DERIVE_BCOS_EXCEPTION(InternalVMError);
DERIVE_BCOS_EXCEPTION(InvalidInputSize);
DERIVE_BCOS_EXCEPTION(InvalidEncoding);

namespace executor
{
#define EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR")
#define EXECUTOR_NAME_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR:" + m_name + "-" + std::to_string(m_schedulerTermId))
#define COROUTINE_TRACE_LOG(LEVEL, contextID, seq) \
    BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR") << "[" << contextID << "," << seq << "]"
#define PARA_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("PARA") << LOG_BADGE(utcTime())
#define NUMBER(number) LOG_KV("blockNumber", number)


static const char* const USER_TABLE_PREFIX = "/tables/";
static const char* const USER_APPS_PREFIX = "/apps/";
static const char* const USER_SYS_PREFIX = "/sys/";

static const char* const STORAGE_VALUE = "value";
static const char* const ACCOUNT_CODE_HASH = "codeHash";
static const char* const ACCOUNT_CODE = "code";
static const char* const ACCOUNT_BALANCE = "balance";
static const char* const ACCOUNT_ABI = "abi";
static const char* const ACCOUNT_NONCE = "nonce";
static const char* const ACCOUNT_ALIVE = "alive";
static const char* const ACCOUNT_FROZEN = "frozen";

/// auth
static const char* const CONTRACT_SUFFIX = "_accessAuth";
static const char* const ADMIN_FIELD = "admin";
static const char* const STATUS_FIELD = "status";
static const char* const METHOD_AUTH_TYPE = "method_auth_type";
static const char* const METHOD_AUTH_WHITE = "method_auth_white";
static const char* const METHOD_AUTH_BLACK = "method_auth_black";

/// contract status
static const char* const CONTRACT_FROZEN = "frozen";
static const char* const CONTRACT_NORMAL = "normal";

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
    unsigned expGas = 10;
    unsigned expByteGas = 10;
    /// sha3 gas cost
    unsigned sha3Gas = 30;
    unsigned sha3WordGas = 6;
    /// load/store gas cost
    unsigned sloadGas = 50;
    unsigned sstoreSetGas = 20000;
    unsigned sstoreResetGas = 5000;
    unsigned sstoreRefundGas = 15000;
    /// jump gas cost
    unsigned jumpdestGas = 1;
    /// log gas cost
    unsigned logGas = 375;
    unsigned logDataGas = 8;
    unsigned logTopicGas = 375;
    /// creat contract gas cost
    unsigned createGas = 32000;
    /// call function of contract gas cost
    unsigned callGas = 40;
    unsigned callStipend = 2300;
    unsigned callValueTransferGas = 9000;
    unsigned callNewAccountGas = 25000;

    unsigned suicideRefundGas = 24000;
    unsigned memoryGas = 3;
    unsigned quadCoeffDiv = 512;
    unsigned createDataGas = 20;
    /// transaction related gas
    unsigned txGas = 21000;
    unsigned txCreateGas = 53000;
    unsigned txDataZeroGas = 4;
    unsigned txDataNonZeroGas = 68;
    unsigned copyGas = 3;
    /// extra code related gas
    unsigned extcodesizeGas = 20;
    unsigned extcodecopyGas = 20;
    unsigned extcodehashGas = 400;
    unsigned balanceGas = 20;
    unsigned suicideGas = 0;
    unsigned blockhashGas = 20;
    unsigned maxCodeSize = unsigned(-1);

    boost::optional<u256> blockRewardOverwrite;

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

struct ImportRequirements
{
    using value = unsigned;
    enum
    {
        ValidSeal = 1,               ///< Validate seal
        TransactionBasic = 8,        ///< Check the basic structure of the transactions.
        TransactionSignatures = 32,  ///< Check the basic structure of the transactions.
        Parent = 64,                 ///< Check parent block header
        PostGenesis = 256,           ///< Require block to be non-genesis.
        CheckTransactions = TransactionBasic | TransactionSignatures,  ///< Check transaction
                                                                       ///< signatures.
        OutOfOrderChecks = ValidSeal | CheckTransactions,  ///< Do all checks that can be done
                                                           ///< independently of prior blocks having
                                                           ///< been imported.
        InOrderChecks = Parent,  ///< Do all checks that cannot be done independently
                                 ///< of prior blocks having been imported.
        Everything = ValidSeal | CheckTransactions | Parent,
        None = 0
    };
};

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
inline evmc_address toEvmC(const std::string_view& _addr)
{  // TODO: add another interfaces for wasm
    evmc_address ret;
    memcpy(ret.bytes, _addr.data(), 20);
    return ret;
}

/**
 * @brief : trans ethereum hash to evm hash
 * @param _h : hash value
 * @return evmc_bytes32 : transformed hash
 */
inline evmc_bytes32 toEvmC(h256 const& _h)
{
    return reinterpret_cast<evmc_bytes32 const&>(_h);
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

inline std::string getContractTableName(const std::string_view& _address, bool _isWasm)
{
    std::string formatAddress(_address);
    if (!_isWasm)
    {
        boost::algorithm::to_lower(formatAddress);
    }

    std::string address = (_address[0] == '/') ? formatAddress.substr(1) : formatAddress;

    return std::string("/apps/").append(address);
}

}  // namespace bcos
