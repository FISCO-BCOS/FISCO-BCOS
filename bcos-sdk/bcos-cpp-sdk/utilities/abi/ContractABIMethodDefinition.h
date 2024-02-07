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
 * @file ContractABIDefinition.h
 * @author: octopus
 * @date 2022-05-19
 */
#pragma once

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include <bcos-utilities/Common.h>
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bcos
{
namespace cppsdk
{
namespace abi
{

// object for ABI type
class NamedType
{
public:
    using Ptr = std::shared_ptr<NamedType>;
    using ConstPtr = std::shared_ptr<const NamedType>;

public:
    NamedType(std::string _name, std::string _type, bool _indexed)
      : m_name(std::move(_name)), m_type(std::move(_type)), m_indexed(_indexed)
    {}

    NamedType(std::string _name, std::string _type) : NamedType(_name, _type, false) {}

    NamedType(std::string _name, std::string _type, bool _indexed,
        std::vector<NamedType::Ptr> _components)
      : NamedType(_name, _type, _indexed)
    {
        m_components = std::move(_components);
    }

private:
    std::string m_name;
    std::string m_type;
    bool m_indexed;
    std::vector<NamedType::Ptr> m_components;

public:
    const std::string& name() const { return m_name; }
    const std::string& type() const { return m_type; }
    bool indexed() const { return m_indexed; }
    std::vector<NamedType::Ptr>& components() { return m_components; }
    std::vector<NamedType::Ptr> const& components() const { return m_components; }

public:
    std::string getTupleRawTypeAsString()
    {
        std::string result;
        std::for_each(
            m_components.begin(), m_components.end(), [&result](NamedType::Ptr _nameType) {
                result += _nameType->getTypeAsString();
                result += ",";
            });

        if (!result.empty() && (result[result.size() - 1] == ','))
        {
            result.pop_back();
        }

        return result;
    }

    std::string getTypeAsString()
    {
        // not tuple, return
        if (!boost::algorithm::starts_with(m_type, "tuple"))
        {
            return m_type;
        }

        std::string result = m_type;
        std::string tupleRawString = getTupleRawTypeAsString();
        boost::algorithm::replace_all(result, "tuple", "(" + tupleRawString + ")");
        return result;
    }
};

class NamedTypeHelper
{
public:
    using Ptr = std::shared_ptr<NamedTypeHelper>;
    using ConstPtr = std::shared_ptr<const NamedTypeHelper>;

public:
    NamedTypeHelper(const std::string& _str) { reset(_str); }

public:
    void reset(const std::string& _str);

public:
    bool isFixedList() const
    {
        if ((extentsSize() > 0) && extent(extentsSize()) > 0)
        {
            return true;
        }

        return false;
    }

    bool isDynamicList() const
    {
        if ((extentsSize() > 0) && extent(extentsSize()) == 0)
        {
            return true;
        }

        return false;
    }

    bool isTuple() const { return m_baseType.find("tuple") != std::string::npos; }

    // the number of dimensions of T
    std::size_t extentsSize() const { return m_extents.size(); }

    // reduce the array dimension
    bool removeExtent();

    // obtains the size of an array type along a specified dimension
    std::size_t extent(std::size_t _index) const
    {
        assert(_index >= 1);
        return _index > extentsSize() ? 0 : m_extents[_index - 1];
    }


    const std::string& type() const { return m_type; }
    const std::string& baseType() const { return m_baseType; }

private:
    // the string type
    std::string m_type;
    // the base string type,
    // eg : the type of int[10][][2] is int[10][][2] and the baseType of it is int
    std::string m_baseType;
    // the dimension of the array, eg:
    std::vector<std::size_t> m_extents;
};

class ContractABIMethodDefinition
{
public:
    using Ptr = std::shared_ptr<ContractABIMethodDefinition>;
    using ConstPtr = std::shared_ptr<const ContractABIMethodDefinition>;
    using UniquePtr = std::unique_ptr<ContractABIMethodDefinition>;

public:
    static const std::string CONSTRUCTOR_TYPE;
    static const std::string FUNCTION_TYPE;
    static const std::string EVENT_TYPE;
    static const std::string FALLBACK_TYPE;
    static const std::string RECEIVE_TYPE;

private:
    // name field
    std::string m_name;
    // type field
    std::string m_type;
    // const field
    bool m_constant;
    // payable field
    bool m_payable;
    // anonymous field
    bool m_anonymous;
    // internalType
    std::string m_internalType;
    // stateMutability field
    std::string m_stateMutability;
    // input list
    std::vector<NamedType::Ptr> m_inputs;
    // output list
    std::vector<NamedType::Ptr> m_outputs;

public:
    bool isEvent() const { return m_type == ContractABIMethodDefinition::EVENT_TYPE; }

    // name field
    const std::string& name() const { return m_name; }
    void setName(std::string _name) { m_name = std::move(_name); }

    // type field
    const std::string& type() const { return m_type; }
    void setType(std::string _type) { m_type = std::move(_type); }

    // const field
    bool constant() const { return m_constant || (m_stateMutability == "view"); }
    void setConstant(bool _constant) { m_constant = _constant; }

    // payable field
    bool payable() const { return m_payable || (m_stateMutability == "payable"); }
    void setPayable(bool _payable) { m_payable = _payable; }

    // anonymous field
    bool anonymous() const { return m_anonymous; }
    void setAnonymous(bool _anonymous) { m_anonymous = _anonymous; }

    const std::string& internalType() { return m_internalType; }
    void setInternalType(std::string _internalType) { m_internalType = std::move(_internalType); }

    // stateMutability field
    const std::string& stateMutability() { return m_stateMutability; }
    void setStateMutability(std::string _stateMutability)
    {
        m_stateMutability = std::move(_stateMutability);
    }

    // input list
    const std::vector<NamedType::Ptr>& inputs() const { return m_inputs; }
    std::vector<NamedType::Ptr>& inputs() { return m_inputs; }

    void setInputs(std::vector<NamedType::Ptr> _inputs) { m_inputs = std::move(_inputs); }

    // output list
    const std::vector<NamedType::Ptr>& outputs() const { return m_outputs; }
    std::vector<NamedType::Ptr>& outputs() { return m_outputs; }

    void setOutputs(std::vector<NamedType::Ptr> _outputs) { m_outputs = std::move(_outputs); }

public:
    std::string getMethodSignatureAsString() const
    {
        std::string result{m_name + "("};

        std::for_each(m_inputs.begin(), m_inputs.end(), [&](const NamedType::Ptr& _namedType) {
            result += _namedType->getTypeAsString();
            result += ",";
        });

        if (!result.empty() && (result[result.size() - 1] == ','))
        {
            result.pop_back();
        }

        result.push_back(')');

        return result;
    }

    bcos::bytes getMethodID(bcos::crypto::Hash::Ptr _hashImpl) const
    {
        auto methodSig = getMethodSignatureAsString();
        auto hashBytes =
            _hashImpl->hash(bcos::bytesConstRef((bcos::byte*)methodSig.data(), methodSig.size()))
                .asBytes();
        hashBytes.resize(4);
        return hashBytes;
    }

    std::string getMethodIDAsString(bcos::crypto::Hash::Ptr _hashImpl) const
    {
        auto methodSig = getMethodSignatureAsString();
        return _hashImpl->hash(bcos::bytesConstRef((bcos::byte*)methodSig.data(), methodSig.size()))
            .hex()
            .substr(0, 8);
    }

    std::string getEventTopicAsString(bcos::crypto::Hash::Ptr _hashImpl) const
    {
        auto eventSig = getMethodSignatureAsString();
        return _hashImpl->hash(bcos::bytesConstRef((bcos::byte*)eventSig.data(), eventSig.size()))
            .hex()
            .substr(0, 8);
    }
};

}  // namespace abi
}  // namespace cppsdk
}  // namespace bcos