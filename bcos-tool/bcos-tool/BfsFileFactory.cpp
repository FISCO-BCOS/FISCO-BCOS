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
#include <bcos-framework/storage/Table.h>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <future>

using namespace bcos::tool;
using namespace bcos::storage;

void BfsFileFactory::buildAsync(bcos::storage::StorageInterface::Ptr const& _storage,
    std::string_view fileName, FileType fileType, std::function<void(Error::UniquePtr&&)> _callback)
{
    _storage->asyncOpenTable(fileName,
        [_callback = std::move(_callback), fileType](auto&& _error, std::optional<Table>&& _table) {
            if (_error)
            {
                _callback(std::forward<decltype(_error)>(_error));
                return;
            }
            if (!_table.has_value())
            {
                _callback(BCOS_ERROR_UNIQUE_PTR(-1, "Table not exist"));
                return;
            }
            bool buildRet = false;
            switch (fileType)
            {
            case DIRECTOR:
                buildRet = buildDir(_table.value());
                break;
            case LINK:
                buildRet = buildLink(_table.value(), "", "");
                break;
            case AUTH:
                buildRet = buildAuth(_table.value(), "");
                break;
            case CONTRACT:
                buildRet = buildContract(_table.value());
                break;
            }
            _callback(buildRet ? nullptr : BCOS_ERROR_UNIQUE_PTR(-1, "Build BFS file error."));
        });
}

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
    storage::Entry& _mutableEntry, std::variant<FileType, std::string> fileType)
{
    std::string_view type;
    std::visit(
        [&type](const auto& _type) {
            using T = std::decay_t<decltype(_type)>;
            if constexpr (std::is_same_v<T, std::string>)
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

bool BfsFileFactory::buildLink(
    Table& _table, const std::string& _address, const std::string& _abi, const std::string& name)
{
    Entry tEntry;
    tEntry.importFields({std::string(FS_TYPE_LINK)});
    _table.setRow(FS_KEY_TYPE, std::move(tEntry));

    Entry linkEntry;
    linkEntry.importFields({_address});
    _table.setRow(FS_LINK_ADDRESS, std::move(linkEntry));

    if (!_abi.empty())
    {
        BCOS_LOG(TRACE) << LOG_BADGE("BFS") << "buildLink with abi"
                        << LOG_KV("abiSize", _abi.size());
        Entry abiEntry;
        abiEntry.importFields({_abi});
        _table.setRow(FS_LINK_ABI, std::move(abiEntry));
    }

    if (!name.empty())
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
    _table.setRow(ADMIN_FIELD, std::move(adminEntry));

    Entry statusEntry;
    statusEntry.importFields({"normal"});
    _table.setRow(STATUS_FIELD, std::move(statusEntry));

    Entry emptyType;
    emptyType.importFields({""});
    _table.setRow(METHOD_AUTH_TYPE, std::move(emptyType));

    Entry emptyWhite;
    emptyWhite.importFields({""});
    _table.setRow(METHOD_AUTH_WHITE, std::move(emptyWhite));

    Entry emptyBlack;
    emptyBlack.importFields({""});
    _table.setRow(METHOD_AUTH_BLACK, std::move(emptyBlack));
    return true;
}
bool BfsFileFactory::buildContract(Table& _table)
{
    return false;
}
