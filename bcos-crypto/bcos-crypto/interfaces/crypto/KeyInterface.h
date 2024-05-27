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
 * @brief interface for Public/Secret key
 * @file KeyInterface.h
 * @author: yujiechen
 * @date 2021-04-02
 */
#pragma once
#include "bcos-utilities/Common.h"
#include <memory>
#include <set>

namespace bcos::crypto
{
class KeyInterface
{
public:
    using Ptr = std::shared_ptr<KeyInterface>;
    using UniquePtr = std::unique_ptr<KeyInterface>;
    KeyInterface() = default;
    virtual ~KeyInterface() = default;
    virtual const bytes& data() const = 0;
    virtual size_t size() const = 0;
    virtual char* mutableData() = 0;
    virtual const char* constData() const = 0;
    virtual std::shared_ptr<bytes> encode() const = 0;
    virtual void decode(bytesConstRef _data) = 0;
    virtual void decode(bytes&& _data) = 0;

    virtual std::string shortHex() = 0;
    virtual std::string hex() = 0;
};
using Public = KeyInterface;
using Secret = KeyInterface;
using PublicPtr = KeyInterface::Ptr;
using SecretPtr = KeyInterface::Ptr;
using NodeIDPtr = KeyInterface::Ptr;
using NodeID = KeyInterface;
using NodeIDs = std::vector<NodeIDPtr>;
using NodeIDListPtr = std::shared_ptr<NodeIDs>;

struct KeyCompare
{
public:
    bool operator()(KeyInterface::Ptr const& _first, KeyInterface::Ptr const& _second) const
    {
        // increase order
        return _first->data() < _second->data();
    }
};

struct KeyHasher
{
    size_t hash(KeyInterface::Ptr const& _key) const
    {
        size_t seed = hashString({_key->constData(), _key->size()});
        return seed;
    }

    bool equal(const KeyInterface::Ptr& lhs, const KeyInterface::Ptr& rhs) const
    {
        return lhs->data() == rhs->data();
    }
    std::hash<std::string_view> hashString;
};
using NodeIDSet = std::set<bcos::crypto::NodeIDPtr, KeyCompare>;
using NodeIDSetPtr = std::shared_ptr<NodeIDSet>;
}  // namespace bcos::crypto
