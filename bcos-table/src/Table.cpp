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
 * @brief interface of Table
 * @file Table.h
 * @author: xingqiangbai
 * @date: 2021-04-07
 */
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "tbb/concurrent_vector.h"
#include "tbb/enumerable_thread_specific.h"
#include "tbb/parallel_for.h"
#include "tbb/parallel_invoke.h"
#include <boost/throw_exception.hpp>
#include <sstream>

using namespace std;

namespace bcos
{
namespace storage
{
std::optional<Entry> Table::getRow(std::string_view _key)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Entry>>> promise;

    asyncGetRow(_key, [&promise](auto error, auto entry) {
        promise.set_value({std::move(error), std::move(entry)});
    });

    auto result = promise.get_future().get();

    if (std::get<0>(result))
    {
        BOOST_THROW_EXCEPTION(*(std::get<0>(result)));
    }

    return std::get<1>(result);
}

std::vector<std::optional<Entry>> Table::getRows(
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys)
{
    std::promise<std::tuple<Error::UniquePtr, std::vector<std::optional<Entry>>>> promise;
    asyncGetRows(_keys, [&promise](auto error, auto entries) {
        promise.set_value(std::tuple{std::move(error), std::move(entries)});
    });

    auto result = promise.get_future().get();

    if (std::get<0>(result))
    {
        BOOST_THROW_EXCEPTION(*(std::get<0>(result)));
    }

    return std::get<1>(result);
}

std::vector<std::string> Table::getPrimaryKeys(std::optional<const Condition> const& _condition)
{
    std::promise<std::tuple<Error::UniquePtr, std::vector<std::string>>> promise;
    asyncGetPrimaryKeys(_condition, [&promise](auto&& error, auto&& keys) {
        promise.set_value(std::tuple{std::move(error), std::move(keys)});
    });
    auto result = promise.get_future().get();

    if (std::get<0>(result))
    {
        BOOST_THROW_EXCEPTION(*(std::get<0>(result)));
    }

    return std::get<1>(result);
}

void Table::setRow(std::string_view _key, Entry _entry)
{
    std::promise<Error::UniquePtr> promise;
    m_storage->asyncSetRow(m_tableInfo->name(), _key, std::move(_entry),
        [&promise](auto&& error) { promise.set_value(std::move(error)); });
    auto result = promise.get_future().get();

    if (result)
    {
        BOOST_THROW_EXCEPTION(*result);
    }
}

void Table::asyncGetPrimaryKeys(std::optional<const Condition> const& _condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) noexcept
{
    m_storage->asyncGetPrimaryKeys(m_tableInfo->name(), _condition, _callback);
}

void Table::asyncGetRow(std::string_view _key,
    std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) noexcept
{
    m_storage->asyncGetRow(m_tableInfo->name(), _key,
        [callback = std::move(_callback)](Error::UniquePtr error, std::optional<Entry> entry) {
            callback(std::move(error), std::move(entry));
        });
}

void Table::asyncGetRows(
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) noexcept
{
    m_storage->asyncGetRows(m_tableInfo->name(), _keys,
        [callback = std::move(_callback)](
            Error::UniquePtr error, std::vector<std::optional<Entry>> entries) {
            callback(std::move(error), std::move(entries));
        });
}

void Table::asyncSetRow(
    std::string_view key, Entry entry, std::function<void(Error::UniquePtr)> callback) noexcept
{
    m_storage->asyncSetRow(m_tableInfo->name(), key, std::move(entry), callback);
}

}  // namespace storage
}  // namespace bcos
