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
 * @date 2021-04-13
 */
#pragma once
#include <bcos-framework//consensus/ConsensusNodeInterface.h>
#include <bcos-framework//protocol/Block.h>
#include <tbb/concurrent_unordered_map.h>
#include <map>

#define LEDGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER")

namespace bcos::ledger
{
// parent=>children
using Parent2ChildListMap = std::map<std::string, std::vector<std::string>>;
// child=>parent
using Child2ParentMap = tbb::concurrent_unordered_map<std::string, std::string>;

static const char* const SYS_VALUE = "value";
static const char* const SYS_CONFIG_ENABLE_BLOCK_NUMBER = "enable_number";

// FileSystem paths
static const char* const FS_ROOT = "/";
static const char* const FS_APPS = "/apps";
static const char* const FS_USER = "/usr";
static const char* const FS_SYS_BIN = "/sys";
static const char* const FS_USER_TABLE = "/tables";

// FileSystem keys
static const char* const FS_KEY_NAME = "name";
static const char* const FS_KEY_TYPE = "type";
static const char* const FS_KEY_SUB = "sub";
static const char* const FS_ACL_TYPE = "acl_type";
static const char* const FS_ACL_WHITE = "acl_white";
static const char* const FS_ACL_BLACK = "acl_black";
static const char* const FS_KEY_EXTRA = "extra";

// FileSystem file type
static const char* const FS_TYPE_DIR = "directory";
static const char* const FS_TYPE_CONTRACT = "contract";

enum LedgerError : int32_t
{
    SUCCESS = 0,
    OpenTableFailed = 3001,
    CallbackError = 3002,
    ErrorArgument = 3003,
    DecodeError = 3004,
    ErrorCommitBlock = 3005,
    CollectAsyncCallbackError = 3006,
    LedgerLockError = 3007,
    GetStorageError = 3008,
    EmptyEntry = 3009,
    UnknownError = 3010,
};

}  // namespace bcos::ledger
