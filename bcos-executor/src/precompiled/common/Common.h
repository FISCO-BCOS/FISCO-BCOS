/**
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
 * @file Common.h
 * @author: kyonRay
 * @date 2021-05-25
 */

#pragma once

#include "bcos-framework/Common.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/Entry.h"
#include <memory>
#include <string>

namespace bcos
{
namespace precompiled
{
#define PRECOMPILED_LOG(LEVEL) BCOS_LOG(LEVEL) << "[PRECOMPILED]"

using TableInfoTuple = std::tuple<std::string, std::vector<std::string>>;
using ConditionTuple = std::tuple<uint8_t, std::string>;
using LimitTuple = std::tuple<uint32_t, uint32_t>;
using UpdateFieldTuple = std::tuple<std::string, std::string>;
using EntryTuple = std::tuple<std::string, std::vector<std::string>>;
using BfsTuple = std::tuple<std::string, std::string, std::vector<std::string>>;

/// Precompiled reserved code field
static constexpr const char* const PRECOMPILED_CODE_FIELD = "[PRECOMPILED]";
static constexpr const int PRECOMPILED_CODE_FIELD_SIZE = 13;

/// SYS_CONFIG table fields
static constexpr size_t SYS_VALUE = 0;
static constexpr const char* SYS_VALUE_FIELDS = "value";

/// FileSystem path limit
static const size_t FS_PATH_MAX_LENGTH = 56;
static const size_t FS_PATH_MAX_LEVEL = 6;

const int SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH = 64;
const int SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH = 1024;
// field
const int USER_TABLE_FIELD_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH_S = 50;
// value
const int USER_TABLE_KEY_VALUE_MAX_LENGTH = 255;
const int USER_TABLE_FIELD_VALUE_MAX_LENGTH = 16 * 1024 * 1024 - 1;
const int USER_TABLE_MAX_LIMIT_COUNT = 500;

const int CODE_NO_AUTHORIZED = -50000;
const int CODE_TABLE_NAME_ALREADY_EXIST = -50001;
const int CODE_TABLE_NAME_LENGTH_OVERFLOW = -50002;
const int CODE_TABLE_FIELD_LENGTH_OVERFLOW = -50003;
const int CODE_TABLE_FIELD_TOTAL_LENGTH_OVERFLOW = -50004;
const int CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW = -50005;
const int CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW = -50006;
const int CODE_TABLE_DUPLICATE_FIELD = -50007;
const int CODE_TABLE_INVALIDATE_FIELD = -50008;

const int TX_COUNT_LIMIT_MIN = 1;
const int TX_GAS_LIMIT_MIN = 100000;

enum PrecompiledErrorCode : int
{
    // FileSystemPrecompiled -53099 ~ -53000
    CODE_ADDRESS_OR_VERSION_ERROR = -51202,
    CODE_FILE_INVALID_TYPE = -53006,
    CODE_FILE_INVALID_PATH = -53005,
    CODE_FILE_BUILD_DIR_FAILED = -53003,
    CODE_FILE_ALREADY_EXIST = -53002,
    CODE_FILE_NOT_EXIST = -53001,

    // ContractLifeCyclePrecompiled -51999 ~ -51900
    CODE_INVALID_REVOKE_LAST_AUTHORIZATION = -51907,
    CODE_INVALID_NON_EXIST_AUTHORIZATION = -51906,
    CODE_INVALID_NO_AUTHORIZED = -51905,
    CODE_INVALID_TABLE_NOT_EXIST = -51904,
    CODE_INVALID_CONTRACT_ADDRESS = -51903,
    CODE_INVALID_CONTRACT_REPEAT_AUTHORIZATION = -51902,
    CODE_INVALID_CONTRACT_AVAILABLE = -51901,
    CODE_INVALID_CONTRACT_FROZEN = -51900,

    // RingSigPrecompiled -51899 ~ -51800
    VERIFY_RING_SIG_FAILED = -51800,

    // GroupSigPrecompiled -51799 ~ -51700
    VERIFY_GROUP_SIG_FAILED = -51700,

    // PaillierPrecompiled -51699 ~ -51600
    CODE_INVALID_CIPHERS = -51600,

    // CRUDPrecompiled -51599 ~ -51500
    CODE_REMOVE_KEY_NOT_EXIST = -51508,
    CODE_UPDATE_KEY_NOT_EXIST = -51507,
    CODE_INSERT_KEY_EXIST = -51506,
    CODE_KEY_NOT_EXIST_IN_ENTRY = -51504,
    CODE_INVALID_UPDATE_TABLE_KEY = -51503,
    CODE_CONDITION_OPERATION_UNDEFINED = -51502,
    CODE_PARSE_CONDITION_ERROR = -51501,
    CODE_PARSE_ENTRY_ERROR = -51500,

    // DagTransferPrecompiled -51499 ~ -51400
    CODE_INVALID_OPENTABLE_FAILED = -51406,
    CODE_INVALID_BALANCE_OVERFLOW = -51405,
    CODE_INVALID_INSUFFICIENT_BALANCE = -51404,
    CODE_INVALID_USER_ALREADY_EXIST = -51403,
    CODE_INVALID_USER_NOT_EXIST = -51402,
    CODE_INVALID_AMOUNT = -51401,
    CODE_INVALID_USER_NAME = -51400,

    // SystemConfigPrecompiled -51399 ~ -51300

    // ConsensusPrecompiled -51199 ~ -51100
    CODE_INVALID_NODE_ID = -51100,
    CODE_LAST_SEALER = -51101,
    CODE_INVALID_WEIGHT = -51102,
    CODE_NODE_NOT_EXIST = -51103,

    // AuthPrecompiledTest -51099 ~ -51000
    CODE_TABLE_AUTH_TYPE_DECODE_ERROR = -51004,
    CODE_TABLE_ERROR_AUTH_TYPE = -51003,
    CODE_TABLE_AUTH_TYPE_NOT_EXIST = -51002,
    CODE_TABLE_AUTH_ROW_NOT_EXIST = -51001,
    CODE_TABLE_AGENT_ROW_NOT_EXIST = -51000,

    // Common error code among all precompiled contracts -50199 ~ -50100
    CODE_TABLE_OPEN_ERROR = -50105,
    CODE_TABLE_CREATE_ERROR = -50104,
    CODE_TABLE_SET_ROW_ERROR = -50103,
    CODE_ADDRESS_INVALID = -50102,
    CODE_UNKNOW_FUNCTION_CALL = -50101,
    CODE_TABLE_NOT_EXIST = -50100,

    // correct return: code great or equal 0
    CODE_SUCCESS = 0
};

enum ContractStatus
{
    Available,
    Frozen,
    AddressNonExistent,
    NotContractAddress,
    Count
};
}  // namespace precompiled
}  // namespace bcos