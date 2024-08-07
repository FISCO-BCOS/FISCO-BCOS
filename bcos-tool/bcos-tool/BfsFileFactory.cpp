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
 * @file BfsFileFactory.cpp
 * @author: kyonGuo
 * @date 2022/10/18
 */

#include "BfsFileFactory.h"
#include "bcos-executor/src/Common.h"
#include <bcos-framework/storage/Table.h>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <future>

using namespace bcos::tool;
using namespace bcos::storage;

std::optional<Table> BfsFileFactory::createDir(
    const bcos::storage::StorageInterface::Ptr& _storage, std::string _table)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
    _storage->asyncCreateTable(std::move(_table), std::string(FS_DIR_FIELDS),
        [&createPromise](auto&& error, std::optional<Table>&& _table) {
            createPromise.set_value({std::forward<decltype(error)>(error), std::move(_table)});
        });
    auto [createError, table] = createPromise.get_future().get();
    if (createError)
    {
        BOOST_THROW_EXCEPTION(*createError);
    }
    return table;
}

bool BfsFileFactory::buildDir(Table& _table)
{
    return false;
}

void BfsFileFactory::buildDirEntry(
    storage::Entry& _mutableEntry, std::variant<FileType, std::string, std::string_view> fileType)
{
    std::string_view type;
    std::visit(
        [&type](const auto& _type) {
            using T = std::decay_t<decltype(_type)>;
            if constexpr (std::is_convertible_v<T, std::string_view>)
            {
                type = _type;
            }
            else if constexpr (std::is_same_v<T, FileType>)
            {
                if (_type == FileType::DIRECTOR)
                    type = FS_TYPE_DIR;
                else if (_type == FileType::LINK)
                    type = FS_TYPE_LINK;
                else
                    type = FS_TYPE_CONTRACT;
            }
        },
        fileType);
    _mutableEntry.setObject<std::vector<std::string>>({type.data(), "0", "0", "", "", ""});
}

bool BfsFileFactory::buildLink(Table& _table, const std::string& _address, const std::string& _abi,
    uint32_t blockVersion, const std::string& name)
{
    Entry tEntry;
    tEntry.importFields({std::string(FS_TYPE_LINK)});
    _table.setRow(FS_KEY_TYPE, std::move(tEntry));

    Entry linkEntry;
    linkEntry.importFields({_address});
    _table.setRow(FS_LINK_ADDRESS, std::move(linkEntry));

    // in block version 3.0, save abi in link even if it is empty
    if (!_abi.empty() || blockVersion == (uint32_t)protocol::BlockVersion::V3_0_VERSION)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("BFS") << "buildLink with abi"
                        << LOG_KV("abiSize", _abi.size());
        Entry abiEntry;
        abiEntry.importFields({_abi});
        _table.setRow(FS_LINK_ABI, std::move(abiEntry));
    }

    if (!name.empty() || blockVersion == (uint32_t)protocol::BlockVersion::V3_0_VERSION)
    {
        Entry nameEntry;
        nameEntry.importFields({name});
        _table.setRow(FS_KEY_NAME, std::move(nameEntry));
    }

    return true;
}
bool BfsFileFactory::buildAuth(Table& _table, const std::string& _admin)
{
    Entry adminEntry;
    adminEntry.importFields({_admin});
    _table.setRow(executor::ADMIN_FIELD, std::move(adminEntry));

    Entry statusEntry;
    statusEntry.importFields({"normal"});
    _table.setRow(executor::STATUS_FIELD, std::move(statusEntry));

    Entry emptyType;
    emptyType.importFields({""});
    _table.setRow(executor::METHOD_AUTH_TYPE, std::move(emptyType));

    Entry emptyWhite;
    emptyWhite.importFields({""});
    _table.setRow(executor::METHOD_AUTH_WHITE, std::move(emptyWhite));

    Entry emptyBlack;
    emptyBlack.importFields({""});
    _table.setRow(executor::METHOD_AUTH_BLACK, std::move(emptyBlack));
    return true;
}
bool BfsFileFactory::buildContract(Table& _table)
{
    return false;
}
