/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file BfsFileFactory.h
 * @author: kyonGuo
 * @date 2022/10/18
 */

#pragma once
#include <bcos-framework/Common.h>
#include <bcos-framework/storage/Common.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-utilities/Ranges.h>
#include <boost/algorithm/string/join.hpp>

namespace bcos::tool
{
/// BFS base dir
constexpr static const std::string_view FS_ROOT{"/"};
constexpr static const std::string_view FS_APPS{"/apps"};
constexpr static const std::string_view FS_USER{"/usr"};
constexpr static const std::string_view FS_SYS_BIN{"/sys"};
constexpr static const std::string_view FS_USER_TABLE{"/tables"};
constexpr static const uint8_t FS_ROOT_SUB_COUNT = 5;
constexpr static const std::array<std::string_view, FS_ROOT_SUB_COUNT> FS_ROOT_SUBS = {
    FS_ROOT, FS_APPS, FS_USER, FS_USER_TABLE, FS_SYS_BIN};

// not use in version > 3.1.0
constexpr static const std::string_view FS_KEY_SUB{"sub"};
constexpr static const std::string_view FS_KEY_NAME{"name"};
constexpr static const std::string_view FS_KEY_TYPE{"type"};
constexpr static const std::string_view FS_KEY_STAT{"status"};
constexpr static const std::string_view FS_ACL_TYPE{"acl_type"};
constexpr static const std::string_view FS_ACL_WHITE{"acl_white"};
constexpr static const std::string_view FS_ACL_BLACK{"acl_black"};
constexpr static const std::string_view FS_KEY_EXTRA{"extra"};
constexpr static const std::array<std::string_view, 6> FS_FIELDS = {
    FS_KEY_TYPE, FS_KEY_STAT, FS_ACL_TYPE, FS_ACL_WHITE, FS_ACL_BLACK, FS_KEY_EXTRA};
// static const auto FS_DIR_FIELDS = boost::join(
//     FS_FIELDS | RANGES::views::transform([](auto& str) -> std::string { return std::string(str);
//     }),
//     ",");
constexpr static const std::string_view FS_DIR_FIELDS{
    "type,status,acl_type,acl_white,acl_black,extra"};

/// BFS file type
constexpr static const std::string_view FS_TYPE_DIR{"directory"};
constexpr static const std::string_view FS_TYPE_CONTRACT{"contract"};
constexpr static const std::string_view FS_TYPE_LINK{"link"};

/// BFS link type
constexpr static const std::string_view FS_LINK_ADDRESS{"link_address"};
constexpr static const std::string_view FS_LINK_ABI{"link_abi"};

/// auth
constexpr static const std::string_view CONTRACT_SUFFIX{"_accessAuth"};
constexpr static const std::string_view ADMIN_FIELD{"admin"};
constexpr static const std::string_view STATUS_FIELD{"status"};
constexpr static const std::string_view METHOD_AUTH_TYPE{"method_auth_type"};
constexpr static const std::string_view METHOD_AUTH_WHITE{"method_auth_white"};
constexpr static const std::string_view METHOD_AUTH_BLACK{"method_auth_black"};

enum FileType : uint16_t
{
    DIRECTOR = 0,
    LINK = 1,
    AUTH = 2,
    CONTRACT = 3,
};
class BfsFileFactory
{
public:
    BfsFileFactory() = delete;
    ~BfsFileFactory() = default;
    BfsFileFactory(const BfsFileFactory&) = delete;
    BfsFileFactory(BfsFileFactory&&) noexcept = delete;
    BfsFileFactory& operator=(const BfsFileFactory&) = delete;
    BfsFileFactory& operator=(BfsFileFactory&&) noexcept = delete;
    static void buildAsync(bcos::storage::StorageInterface::Ptr const& _storage,
        std::string_view fileName, FileType fileType,
        std::function<void(Error::UniquePtr&&)> _callback);

    static bool buildDir(storage::Table& _table);
    // sync create dir
    static std::optional<storage::Table> createDir(
        bcos::storage::StorageInterface::Ptr const& _storage, std::string _table);
    static void buildDirEntry(
        storage::Entry& _mutableEntry, std::variant<FileType, std::string> fileType);
    static bool buildLink(storage::Table& _table, const std::string& _address,
        const std::string& _abi, const std::string& name = "");
    static bool buildAuth(storage::Table& _table, const std::string& _admin);
    static bool buildContract(storage::Table& _table);
};
}  // namespace bcos::tool
