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
 * @file ContractABIType.h
 * @author: octopus
 * @date 2022-05-23
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/value.h>
#include <json/writer.h>
#include <boost/throw_exception.hpp>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bcos
{
namespace cppsdk
{
namespace abi
{

#define MAX_BYTE_LENGTH (32)

// the maximum length of fixed bytes(bytes1~bytes32)
#define MAX_FIXED_BYTES_LENGTH (32)
// the length of topic
#define CONTRACT_ABI_TOPIC_LENGTH (32)

// type value def
enum class Type
{
    BOOL,           // bool
    UINT,           // uint<M>
    INT,            // int<M>
    FIXED_BYTES,    // byteN
    ADDRESS,        // address
    STRING,         // string
    DYNAMIC_BYTES,  // bytes
    FIXED,          // fixed<M>x<N>, unsupported type
    UFIXED,         // ufixed<M>x<N>, unsupported type
    DYNAMIC_LIST,   // T[]
    FIXED_LIST,     // T[N]
    STRUCT          // struct
};

// base type class with value def
class AbstractType
{
public:
    using Ptr = std::shared_ptr<AbstractType>;
    using UniquePtr = std::unique_ptr<AbstractType>;
    using ConstPtr = std::shared_ptr<const AbstractType>;

public:
    AbstractType(Type _type) : m_type(_type) {}
    virtual ~AbstractType() {}

public:
    Type type() const { return m_type; }
    // void setType(Type _type) { m_type = _type; }

    std::string name() const { return m_name; }
    void setName(std::string _name) { m_name = _name; }

public:
    // if the type is dynamic type, for solidity codec
    virtual bool dynamicType() const = 0;
    // the base offset of the type, for solidity codec
    virtual unsigned offsetAsBytes() const = 0;

    virtual AbstractType::UniquePtr clone() const = 0;
    virtual void clear() = 0;
    virtual Json::Value toJson() const = 0;
    virtual bool isEqual(const AbstractType& _abstractType) = 0;

    inline std::string toJsonString()
    {
        Json::FastWriter writer;
        writer.omitEndingLineFeed();
        auto jsonStr = writer.write(toJson());
        return jsonStr;
    }

private:
    Type m_type;
    std::string m_name;
};

class Uint : public AbstractType
{
public:
    using Ptr = std::shared_ptr<Uint>;
    using UniquePtr = std::unique_ptr<Uint>;

public:
    Uint() : AbstractType(Type::UINT) { m_extent = 256; }

    Uint(std::size_t _extent) : AbstractType(Type::UINT)
    {
        m_extent = _extent;
        verifyExtent();
    }
    Uint(std::size_t _extent, u256 _u) : AbstractType(Type::UINT)
    {
        m_extent = _extent;
        m_u = std::move(_u);
        verifyExtent();
    }

    void verifyExtent()
    {
        if (m_extent < 1 || m_extent > 256 || (m_extent % 8 != 0))
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid uintN, it should be in format as "
                                   "uint8,uint16 ... uint248,uint256, now extent is " +
                                   std::to_string(m_extent)));
        }
    }

public:
    static Uint::UniquePtr newValue() { return std::make_unique<Uint>(); }
    static Uint::UniquePtr newValue(u256 _u) { return std::make_unique<Uint>(256, _u); }
    static Uint::UniquePtr newValue(std::size_t _extent, u256 _u)
    {
        auto ret = std::make_unique<Uint>(_extent, std::move(_u));
        return ret;
    }

public:
    const u256& value() const { return m_u; }
    void setValue(u256 _u) { m_u = std::move(_u); }

    std::size_t extent() const { return m_extent; }

public:
    virtual bool dynamicType() const { return false; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::stringValue);
        jRet = m_u.str(10);
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = Uint::newValue(this->extent(), this->value());
        ret->setName(name());
        return ret;
    }

    virtual void clear() { m_u = 0; }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const Uint&>(_abstractType).value() == m_u;
        }
        return false;
    }

private:
    u256 m_u;
    std::size_t m_extent;
};

class Int : public AbstractType
{
public:
    using Ptr = std::shared_ptr<Int>;
    using UniquePtr = std::unique_ptr<Int>;

public:
    Int() : AbstractType(Type::INT) { m_extent = 256; }
    Int(std::size_t _extent) : AbstractType(Type::INT)
    {
        m_extent = _extent;
        verifyExtent();
    }
    Int(std::size_t _extent, s256 _i) : AbstractType(Type::INT)
    {
        m_extent = _extent;
        m_i = std::move(_i);
        verifyExtent();
    }

    const s256& value() const { return m_i; }
    void setValue(s256 _i) { m_i = std::move(_i); }

    std::size_t extent() const { return m_extent; }

    void verifyExtent()
    {
        if (m_extent < 1 || m_extent > 256 || (m_extent % 8 != 0))
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid intN, it should be in format as "
                                   "int8,int16 ... int248,int256, now extent is " +
                                   std::to_string(m_extent)));
        }
    }

public:
    static Int::UniquePtr newValue() { return std::make_unique<Int>(); }
    static Int::UniquePtr newValue(s256 _i) { return std::make_unique<Int>(256, _i); }
    static Int::UniquePtr newValue(std::size_t _extent, s256 _i)
    {
        auto ret = std::make_unique<Int>(_extent, std::move(_i));
        return ret;
    }

public:
    virtual bool dynamicType() const { return false; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::stringValue);
        jRet = m_i.str(10);
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = Int::newValue(this->extent(), this->value());
        ret->setName(name());
        return ret;
    }

    virtual void clear() { m_i = 0; }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const Int&>(_abstractType).value() == m_i;
        }
        return false;
    }

private:
    s256 m_i;
    std::size_t m_extent;
};

class Boolean : public AbstractType
{
public:
    using Ptr = std::shared_ptr<Boolean>;
    using UniquePtr = std::unique_ptr<Boolean>;

public:
    Boolean() : AbstractType(Type::BOOL) {}
    Boolean(bool _value) : AbstractType(Type::BOOL) { m_value = _value; }

public:
    bool value() const { return m_value; }
    void setValue(bool _value) { m_value = _value; }

public:
    static Boolean::UniquePtr newValue(bool _b) { return std::make_unique<Boolean>(_b); }
    static Boolean::UniquePtr newValue() { return std::make_unique<Boolean>(); }


public:
    virtual bool dynamicType() const { return false; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::booleanValue);
        jRet = m_value;
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = Boolean::newValue(this->value());
        ret->setName(name());
        return ret;
    }

    virtual void clear() { m_value = false; }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const Boolean&>(_abstractType).value() == m_value;
        }
        return false;
    }

private:
    bool m_value;
};

class DynamicBytes : public AbstractType
{
public:
    using Ptr = std::shared_ptr<DynamicBytes>;
    using UniquePtr = std::unique_ptr<DynamicBytes>;

public:
    DynamicBytes() : AbstractType(Type::DYNAMIC_BYTES) {}

    DynamicBytes(bcos::bytes _value) : AbstractType(Type::DYNAMIC_BYTES)
    {
        m_value = std::move(_value);
    }

    const bcos::bytes& value() const { return m_value; }
    void setValue(bcos::bytes _value) { m_value = std::move(_value); }

public:
    static DynamicBytes::UniquePtr newValue() { return std::make_unique<DynamicBytes>(); }

    static DynamicBytes::UniquePtr newValue(bcos::bytes _bs)
    {
        return std::make_unique<DynamicBytes>(std::move(_bs));
    }

public:
    virtual bool dynamicType() const { return true; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::stringValue);
        jRet = "hex://" + *toHexString(m_value);
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = DynamicBytes::newValue(this->value());
        ret->setName(name());
        return ret;
    }

    virtual void clear() { m_value.clear(); }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const DynamicBytes&>(_abstractType).value() == m_value;
        }
        return false;
    }

private:
    bcos::bytes m_value;
};

class FixedBytes : public AbstractType
{
public:
    using Ptr = std::shared_ptr<FixedBytes>;
    using UniquePtr = std::unique_ptr<FixedBytes>;

public:
    FixedBytes(std::size_t _fixedN) : AbstractType(Type::FIXED_BYTES)
    {
        m_fixedN = _fixedN;
        verify(false);
    }

    FixedBytes(std::size_t _fixedN, bcos::bytes _value) : AbstractType(Type::FIXED_BYTES)
    {
        m_fixedN = _fixedN;
        m_value = std::move(_value);
        verify(true);
    }

    const bcos::bytes& value() const { return m_value; }
    bcos::bytes& value() { return m_value; }
    void setValue(bcos::bytes _value)
    {
        m_value = std::move(_value);
        verify(true);
    }

    void verify(bool _value) const
    {
        if ((0 == m_fixedN) || (m_fixedN > MAX_FIXED_BYTES_LENGTH))
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "bytesN length must be must be in range 0 < M <= 32, the real length is: " +
                std::to_string(m_value.size())));
        }

        // check m_fixN vs m_value.size()
        if (_value && m_value.size() != m_fixedN)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "the bytes" + std::to_string(m_fixedN) +
                " data length does not match, data length is " + std::to_string(m_value.size())));
        }
    }

public:
    static FixedBytes::UniquePtr newValue(std::size_t _fixedN)
    {
        return std::make_unique<FixedBytes>(_fixedN);
    }

    static FixedBytes::UniquePtr newValue(std::size_t _fixedN, bcos::bytes _value)
    {
        return std::make_unique<FixedBytes>(_fixedN, std::move(_value));
    }

public:
    std::size_t fixedN() const { return m_fixedN; }

    virtual bool dynamicType() const { return false; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::stringValue);
        jRet = "hex://" + *toHexString(m_value);
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = FixedBytes::newValue(this->m_fixedN, this->value());
        ret->setName(name());
        return ret;
    }

    virtual void clear() { m_value.clear(); }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const FixedBytes&>(_abstractType).value() == m_value;
        }
        return false;
    }

private:
    std::size_t m_fixedN;
    bcos::bytes m_value;
};

class Addr : public AbstractType
{
public:
    using Ptr = std::shared_ptr<Addr>;
    using UniquePtr = std::unique_ptr<Addr>;

public:
    Addr() : AbstractType(Type::ADDRESS) {}

    Addr(std::string _value) : AbstractType(Type::ADDRESS) { m_value = std::move(_value); }

    const std::string& value() const { return m_value; }
    void setValue(std::string _value) { m_value = std::move(_value); }

public:
    static Addr::UniquePtr newValue() { return std::make_unique<Addr>(); }

    static Addr::UniquePtr newValue(std::string _addr)
    {
        return std::make_unique<Addr>(std::move(_addr));
    }

public:
    virtual bool dynamicType() const { return false; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::stringValue);
        jRet = m_value;
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = Addr::newValue(this->value());
        ret->setName(name());
        return ret;
    }
    virtual void clear() { m_value.clear(); }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const Addr&>(_abstractType).value() == m_value;
        }
        return false;
    }

private:
    std::string m_value;
};

class String : public AbstractType
{
public:
    using Ptr = std::shared_ptr<String>;
    using UniquePtr = std::unique_ptr<String>;

public:
    String() : AbstractType(Type::STRING) {}

    String(std::string _value) : AbstractType(Type::STRING) { m_value = std::move(_value); }

    const std::string& value() const { return m_value; }
    void setValue(std::string _value) { m_value = std::move(_value); }

public:
    static String::UniquePtr newValue() { return std::make_unique<String>(); }

    static String::UniquePtr newValue(std::string _str)
    {
        return std::make_unique<String>(std::move(_str));
    }

public:
    virtual bool dynamicType() const { return true; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::stringValue);
        jRet = m_value;
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = String::newValue(this->value());
        ret->setName(name());
        return ret;
    }

    virtual void clear() { m_value.clear(); }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() == type())
        {
            return static_cast<const String&>(_abstractType).value() == m_value;
        }
        return false;
    }

private:
    std::string m_value;
};

/*
// unsupported type: fixed、unfixed
class Fixed : public AbstractType
{
public:
    Fixed() : AbstractType(Type::FIXED) {}
};

class Ufixed : public AbstractType
{
public:
    Ufixed() : AbstractType(Type::UFIXED) {}
};
*/

class FixedList : public AbstractType
{
public:
    using Ptr = std::shared_ptr<FixedList>;
    using UniquePtr = std::unique_ptr<FixedList>;

public:
    FixedList(unsigned _dimension) : AbstractType(Type::FIXED_LIST) { m_dimension = _dimension; }

private:
    std::vector<AbstractType::Ptr> m_value;
    unsigned m_dimension{0};

public:
    void verify() const
    {
        // check m_dimension with m_fixedList.length, when encode fixed list
        if (m_dimension <= 0)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid fixed array T[0], with dimension is zero"));
        }

        // check if array elements enough
        if (m_dimension != m_value.size())
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("insufficient elements for fixed length array with dimension: " +
                                   std::to_string(m_dimension) +
                                   " ,but has elements: " + std::to_string(m_value.size())));
        }
    }

    virtual bool dynamicType() const
    {
        verify();
        return m_value[0]->dynamicType();
    }

    virtual unsigned offsetAsBytes() const
    {
        if (dynamicType())
        {
            return MAX_BYTE_LENGTH;
        }

        return m_value.size() * m_value[0]->offsetAsBytes();
    }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::arrayValue);
        for (std::size_t index = 0; index < m_value.size(); index++)
        {
            jRet.append(m_value[index]->toJson());
        }
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = FixedList::newValue(this->dimension());
        for (const auto& v : m_value)
        {
            ret->addMember(v->clone());
        }
        ret->setName(name());
        return ret;
    }

    virtual void clear()
    {
        for (const auto& v : m_value)
        {
            v->clear();
        }
    }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() != type())
        {
            return false;
        }


        const auto& value = static_cast<const FixedList&>(_abstractType).value();

        if (static_cast<const FixedList&>(_abstractType).dimension() != m_dimension)
        {
            return false;
        }

        if (value.size() != m_value.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < m_value.size(); i++)
        {
            if (!value[i]->isEqual(*m_value[i]))
            {
                return false;
            }
        }

        return true;
    }

public:
    static FixedList::UniquePtr newValue(unsigned _dimension)
    {
        return std::make_unique<FixedList>(_dimension);
    }

public:
    void addMember(AbstractType::UniquePtr _tv)
    {
        auto sPtr = AbstractType::Ptr(std::move(_tv));

        m_value.push_back(sPtr);
    }

    const std::vector<AbstractType::Ptr>& value() const { return m_value; }

    unsigned dimension() const { return m_dimension; }
};

class DynamicList : public AbstractType
{
public:
    using Ptr = std::shared_ptr<DynamicList>;
    using UniquePtr = std::unique_ptr<DynamicList>;

public:
    DynamicList() : AbstractType(Type::DYNAMIC_LIST) {}

private:
    std::vector<AbstractType::Ptr> m_value;

public:
    virtual bool dynamicType() const { return true; }
    virtual unsigned offsetAsBytes() const { return MAX_BYTE_LENGTH; }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::arrayValue);
        for (std::size_t index = 0; index < m_value.size(); index++)
        {
            jRet.append(m_value[index]->toJson());
        }
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = DynamicList::newValue();
        for (const auto& v : m_value)
        {
            ret->addMember(v->clone());
        }
        ret->setName(name());
        return ret;
    }

    virtual void clear()
    {
        for (const auto& v : m_value)
        {
            v->clear();
        }
    }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() != type())
        {
            return false;
        }

        const auto& value = static_cast<const DynamicList&>(_abstractType).value();
        if (value.size() != m_value.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < m_value.size(); i++)
        {
            if (!value[i]->isEqual(*m_value[i]))
            {
                return false;
            }
        }

        return true;
    }

public:
    static DynamicList::UniquePtr newValue() { return std::make_unique<DynamicList>(); }

public:
    void addMember(AbstractType::Ptr _tv) { m_value.push_back(_tv); }

    void setEmpty() { m_value.clear(); }

    const std::vector<AbstractType::Ptr>& value() const { return m_value; }
};

class Struct : public AbstractType
{
public:
    using Ptr = std::shared_ptr<Struct>;
    using UniquePtr = std::unique_ptr<Struct>;

public:
    Struct() : AbstractType(Type::STRUCT) {}

private:
    std::vector<AbstractType::Ptr> m_value;
    std::unordered_map<std::string, AbstractType::Ptr> m_kvValue;

public:
    static Struct::UniquePtr newValue() { return std::make_unique<Struct>(); }

public:
    virtual bool dynamicType() const
    {
        for (auto& type : m_value)
        {
            if (type->dynamicType())
            {
                return true;
            }
        }

        return false;
    }

    virtual unsigned offsetAsBytes() const
    {
        if (dynamicType())
        {
            return MAX_BYTE_LENGTH;
        }

        unsigned offset = 0;
        for (auto& type : m_value)
        {
            offset += type->offsetAsBytes();
        }

        return offset;
    }

    virtual Json::Value toJson() const
    {
        Json::Value jRet(Json::arrayValue);
        for (std::size_t index = 0; index < m_value.size(); index++)
        {
            jRet.append(m_value[index]->toJson());
        }
        return jRet;
    }

    virtual AbstractType::UniquePtr clone() const
    {
        auto ret = Struct::newValue();
        for (const auto& v : m_value)
        {
            ret->addMember(v->clone());
        }
        ret->setName(name());
        return ret;
    }

    virtual void clear()
    {
        for (const auto& v : m_value)
        {
            v->clear();
        }
    }

    virtual bool isEqual(const AbstractType& _abstractType)
    {
        if (_abstractType.type() != type())
        {
            return false;
        }

        const auto& value = static_cast<const Struct&>(_abstractType).value();
        if (value.size() != m_value.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < m_value.size(); i++)
        {
            if (!value[i]->isEqual(*m_value[i]))
            {
                return false;
            }
        }

        return true;
    }

public:
    const std::vector<AbstractType::Ptr>& value() const { return m_value; }
    std::vector<AbstractType::Ptr>& value() { return m_value; }

    const auto& kvValue() const { return m_kvValue; }

    unsigned memberSize() const { return m_value.size(); }

public:
    void addMember(AbstractType::Ptr _tv)
    {
        m_value.push_back(_tv);
        if (!_tv->name().empty())
        {
            m_kvValue[_tv->name()] = _tv;
        }
    }
};

}  // namespace abi
}  // namespace cppsdk
}  // namespace bcos