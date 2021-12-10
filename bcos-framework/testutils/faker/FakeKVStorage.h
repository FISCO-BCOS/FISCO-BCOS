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
 * @brief fake KVStorage
 * @file FakeKVStorage.h
 * @author: yujiechen
 * @date 2021-09-07
 */
#pragma once
#include "../../interfaces/protocol/CommonError.h"
#include "../../interfaces/storage/StorageInterface.h"
#include "../../libutilities/Error.h"
#include "../../libutilities/KVStorageHelper.h"
#include <atomic>
#include <memory>

using namespace bcos;
using namespace bcos::storage;
namespace bcos
{
namespace test
{
class FakeKVStorage : public bcos::storage::KVStorageHelper
{
};

// class FakeKVStorage : public KVStorageHelper
// {
// public:
//     using Ptr = std::shared_ptr<FakeKVStorage>;
//     FakeKVStorage() = default;
//     ~FakeKVStorage() override {}

//     void asyncGet(const std::string_view& _columnFamily, const std::string_view& _key,
//         std::function<void(const Error::Ptr&, const std::string& value)> _callback) override
//     {
//         auto key = getKey(_columnFamily, _key);
//         if (!m_key2Data.count(key))
//         {
//             _callback(std::make_shared<Error>(
//                           (int32_t)bcos::protocol::StorageErrorCode::NotFound, "key NotFound"),
//                 "");
//             return;
//         }
//         _callback(nullptr, m_key2Data[key]);
//     }

//     void asyncGetBatch(const std::string_view& _columnFamily,
//         const std::shared_ptr<std::vector<std::string>>& _keys,
//         std::function<void(const Error::Ptr&, const std::shared_ptr<std::vector<std::string>>&)>
//             callback) override
//     {
//         auto result = std::make_shared<std::vector<std::string>>();
//         for (auto const& _key : *_keys)
//         {
//             auto key = getKey(_columnFamily, _key);
//             if (!m_key2Data.count(key))
//             {
//                 result->push_back("");
//                 continue;
//             }
//             result->push_back(m_key2Data[key]);
//         }
//         callback(nullptr, result);
//     }

//     void asyncPut(const std::string_view& _columnFamily, const std::string_view& _key,
//         const std::string_view& _value, std::function<void(const Error::Ptr&)> _callback)
//         override
//     {
//         auto key = getKey(_columnFamily, _key);
//         m_key2Data[key] = _value;
//         _callback(nullptr);
//     }
//     void asyncRemove(const std::string_view& _columnFamily, const std::string_view& _key,
//         std::function<void(const Error::Ptr&)> _callback) override
//     {
//         auto key = getKey(_columnFamily, _key);
//         if (!m_key2Data.count(key))
//         {
//             _callback(std::make_shared<Error>(
//                 (int32_t)bcos::protocol::StorageErrorCode::NotFound, "key NotFound"));
//             return;
//         }
//         m_key2Data.erase(key);
//         _callback(nullptr);
//     }

// protected:
//     std::string getKey(const std::string_view& _columnFamily, const std::string_view& _key)
//     {
//         std::string columnFamily(_columnFamily.data(), _columnFamily.size());
//         std::string key(_key.data(), _key.size());
//         return columnFamily + "_" + key;
//     }
//     std::map<std::string, std::string> m_key2Data;
// };
}  // namespace test
}  // namespace bcos