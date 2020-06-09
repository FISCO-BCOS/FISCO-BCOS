/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file Common.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once
#include "libdevcore/Exceptions.h"
#include "libprecompiled/Precompiled.h"
#include <libdevcore/Address.h>
#include <memory>
#include <string>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}

namespace storage
{
class Table;
}

namespace precompiled
{
#define PRECOMPILED_LOG(LEVEL) LOG(LEVEL) << "[PRECOMPILED]"

/// _sys_table_access_ table fields
const std::string SYS_AC_TABLE_NAME = "table_name";
const std::string SYS_AC_ADDRESS = "address";
const std::string SYS_AC_ENABLENUM = "enable_num";

enum PrecompiledError : int
{
    // ChainGovernancePrecompiled -52099 ~ -52000
    CODE_CURRENT_VALUE_IS_EXPECTED_VALUE = -52012,
    CODE_ACCOUNT_FROZEN = -52011,
    CODE_ACCOUNT_ALREADY_AVAILABLE = -52010,
    CODE_INVALID_ACCOUNT_ADDRESS = -52009,
    CODE_ACCOUNT_NOT_EXIST = -52008,
    CODE_OPERATOR_NOT_EXIST = -52007,
    CODE_OPERATOR_EXIST = -52006,
    CODE_COMMITTEE_MEMBER_CANNOT_BE_OPERATOR = -52005,
    CODE_OPERATOR_CANNOT_BE_COMMITTEE_MEMBER = -52004,
    CODE_INVALID_THRESHOLD = -52003,
    CODE_INVALID_REQUEST_PERMISSION_DENIED = -52002,
    CODE_COMMITTEE_MEMBER_NOT_EXIST = -52001,
    CODE_COMMITTEE_MEMBER_EXIST = -52000,

    // ContractLifeCyclePrecompiled -51999 ~ -51900
    CODE_INVALID_NO_AUTHORIZED = -51905,
    CODE_INVALID_TABLE_NOT_EXIST = -51904,
    CODE_INVALID_CONTRACT_ADDRESS = -51903,
    CODE_INVALID_CONTRACT_REPEAT_AUTHORIZATION = -51902,
    CODE_INVALID_CONTRACT_AVAILABLE = -51901,
    CODE_INVALID_CONTRACT_FEOZEN = -51900,

    // RingSigPrecompiled -51899 ~ -51800
    VERIFY_RING_SIG_FAILED = -51800,

    // GroupSigPrecompiled -51799 ~ -51700
    VERIFY_GROUP_SIG_FAILED = -51700,

    // PaillierPrecompiled -51699 ~ -51600
    CODE_INVALID_CIPHERS = -51600,

    // CRUDPrecompiled -51599 ~ -51500
    CODE_CONDITION_OPERATION_UNDEFINED = -51502,
    CODE_PARSE_CONDITION_ERROR = -51501,
    CODE_PARSE_ENTRY_ERROR = -51500,

    // DagTransferPrecompiled -51499 ~ -51400
    CODE_INVALID_OPENTALBLE_FAILED = -51406,
    CODE_INVALID_BALANCE_OVERFLOW = -51405,
    CODE_INVALID_INSUFFICIENT_BALANCE = -51404,
    CODE_INVALID_USER_ALREADY_EXIST = -51403,
    CODE_INVALID_USER_NOT_EXIST = -51402,
    CODE_INVALID_AMOUNT = -51401,
    CODE_INVALID_USER_NAME = -51400,

    // SystemConfigPrecompiled -51399 ~ -51300
    CODE_INVALID_CONFIGURATION_VALUES = -51300,

    // CNSPrecompiled -51299 ~ -51200
    CODE_VERSION_LENGTH_OVERFLOW = -51201,
    CODE_ADDRESS_AND_VERSION_EXIST = -51200,

    // ConsensusPrecompiled -51199 ~ -51100
    CODE_LAST_SEALER = -51101,
    CODE_INVALID_NODEID = -51100,

    // PermissionPrecompiled -51099 ~ -51000
    CODE_COMMITTEE_PERMISSION = -51004,
    CODE_CONTRACT_NOT_EXIST = -51003,
    CODE_TABLE_NAME_OVERFLOW = -51002,
    CODE_TABLE_AND_ADDRESS_NOT_EXIST = -51001,
    CODE_TABLE_AND_ADDRESS_EXIST = -51000,

    // Common error code among all precompiled contracts -50199 ~ -50100
    CODE_ADDRESS_INVALID = -50102,
    CODE_UNKNOW_FUNCTION_CALL = -50101,
    CODE_TABLE_NOT_EXIST = -50100,

    // correct return: code great or equal 0
    CODE_SUCCESS = 0
};

class PrecompiledException : public dev::Exception
{
public:
    PrecompiledException(const std::string& what) : dev::Exception(what) {}
    bytes ToOutput();
};

void getErrorCodeOut(bytes& out, int const& result);
std::string getTableName(const std::string& _tableName);
std::string getContractTableName(Address const& _contractAddress);
void checkNameValidate(const std::string& tableName, std::string& keyField,
    std::vector<std::string>& valueFieldList, bool throwStorageException = true);
int checkLengthValidate(const std::string& field_value, int32_t max_length, int32_t errorCode,
    bool throwStorageException = true);

uint32_t getParamFunc(bytesConstRef _param);
uint32_t getFuncSelectorByFunctionName(std::string const& _functionName);

bytesConstRef getParamData(bytesConstRef _param);

dev::h512s getNodeListByType(std::shared_ptr<dev::storage::Table> _consTable, int64_t _blockNumber,
    std::string const& _type);

std::shared_ptr<std::pair<std::string, int64_t>> getSysteConfigByKey(
    std::shared_ptr<dev::storage::Table> _sysConfigTable, std::string const& _key,
    int64_t const& _num);


const int SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH = 64;
const int SYS_TABLE_VALUE_FIELD_MAX_LENGTH = 1024;
const int CNS_VERSION_MAX_LENGTH = 128;
const int USER_TABLE_KEY_VALUE_MAX_LENGTH = 255;
const int USER_TABLE_FIELD_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH_S = 50;
const int USER_TABLE_FIELD_VALUE_MAX_LENGTH = 16 * 1024 * 1024 - 1;

// Define precompiled contract address
const Address SYS_CONFIG_ADDRESS = Address(0x1000);
const Address TABLE_FACTORY_ADDRESS = Address(0x1001);
const Address CRUD_ADDRESS = Address(0x1002);
const Address CONSENSUS_ADDRESS = Address(0x1003);
const Address CNS_ADDRESS = Address(0x1004);
const Address PERMISSION_ADDRESS = Address(0x1005);
const Address PARALLEL_CONFIG_ADDRESS = Address(0x1006);
const Address CONTRACT_LIFECYCLE_ADDRESS = Address(0x1007);
const Address CHAINGOVERNANCE_ADDRESS = Address(0x1008);
const Address KVTABLE_FACTORY_ADDRESS = Address(0x1010);
const Address WORKING_SEALER_MGR_ADDRESS = Address(0x1011);

/// \brief Sign of the sealer is valid or not
const char* const NODE_TYPE = "type";
const char* const NODE_TYPE_SEALER = "sealer";
const char* const NODE_TYPE_OBSERVER = "observer";
// working sealer
const char* const NODE_TYPE_WORKING_SEALER = "wSealer";
const char* const NODE_KEY_NODEID = "node_id";
const char* const NODE_KEY_ENABLENUM = "enable_num";
const char* const PRI_COLUMN = "name";
const char* const PRI_KEY = "node";

const char* const SYSTEM_CONFIG_KEY = "key";
const char* const SYSTEM_CONFIG_VALUE = "value";
const char* const SYSTEM_CONFIG_ENABLENUM = "enable_num";
const char* const SYSTEM_KEY_TX_COUNT_LIMIT = "tx_count_limit";
const char* const SYSTEM_INIT_VALUE_TX_COUNT_LIMIT = "1000";
const char* const SYSTEM_KEY_TX_GAS_LIMIT = "tx_gas_limit";

// system configuration for RPBFT
const char* const SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM = "rpbft_epoch_sealer_num";
const char* const SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM = "rpbft_epoch_block_num";

const char* const SYSTEM_INIT_VALUE_TX_GAS_LIMIT = "300000000";

const int TX_COUNT_LIMIT_MIN = 1;
const int TX_GAS_LIMIT_MIN = 100000;

const int RPBFT_EPOCH_SEALER_NUM_MIN = 1;
const int RPBFT_EPOCH_BLOCK_NUM_MIN = 1;
}  // namespace precompiled
}  // namespace dev
