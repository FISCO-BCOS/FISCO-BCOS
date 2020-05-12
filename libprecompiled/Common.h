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

enum PrecompiledError : int
{
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

const int SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH = 64;
const int SYS_TABLE_VALUE_FIELD_MAX_LENGTH = 1024;
const int CNS_VERSION_MAX_LENGTH = 128;
const int USER_TABLE_KEY_VALUE_MAX_LENGTH = 255;
const int USER_TABLE_FIELD_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH_S = 50;
const int USER_TABLE_FIELD_VALUE_MAX_LENGTH = 16 * 1024 * 1024 - 1;

void logError(const std::string& precompiled_name, const std::string& op, const std::string& msg);

void logError(const std::string& precompiled_name, const std::string& op, const std::string& key,
    const std::string& value);

void logError(const std::string& precompiled_name, const std::string& op, const std::string& key1,
    const std::string& value1, const std::string& key2, const std::string& value2);

void throwException(const std::string& msg);

char* stringToChar(std::string& params);

}  // namespace precompiled
}  // namespace dev
