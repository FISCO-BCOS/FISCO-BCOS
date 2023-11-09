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
 * @file ContractABITypeCodec.cpp
 * @author: octopus
 * @date 2022-05-19
 */

#include "bcos-cpp-sdk/utilities/Common.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIType.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <climits>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;

#define verifyBufferOffset(_buffer, _offset)                                                      \
    do                                                                                            \
    {                                                                                             \
        if (_offset >= _buffer.size())                                                            \
        {                                                                                         \
            std::stringstream ss;                                                                 \
            ss << "abi encode exception for offset overflow, offset: " << _offset                 \
               << " ,buffer length: " << _buffer.size() << " ,buffer: " << *toHexString(_buffer); \
            BOOST_THROW_EXCEPTION(std::length_error(ss.str().c_str()));                           \
        }                                                                                         \
    } while (0);

//--------------- ContractABITypeCodecSolImpl::serialize begin ----------------------
// uint8~256
void ContractABITypeCodecSolImpl::serialize(
    const u256& _in, std::size_t _extent, bcos::bytes& _buffer)
{
    std::ignore = _extent;
    auto temp = h256(_in).asBytes();
    _buffer.insert(_buffer.end(), temp.begin(), temp.end());
}

// int8~256
void ContractABITypeCodecSolImpl::serialize(
    const s256& _in, std::size_t _extent, bcos::bytes& _buffer)
{
    std::ignore = _extent;
    auto temp = h256(s2u(_in)).asBytes();
    _buffer.insert(_buffer.end(), temp.begin(), temp.end());
}

// bool
void ContractABITypeCodecSolImpl::serialize(bool _in, bcos::bytes& _buffer)
{
    serialize(u256(_in ? 1 : 0), 256, _buffer);
}

// address
void ContractABITypeCodecSolImpl::serialize(const Address& _in, bcos::bytes& _buffer)
{
    auto temp = h256(bytes(12, 0) + _in.asBytes()).asBytes();
    _buffer.insert(_buffer.end(), temp.begin(), temp.end());
}

// bytesN(1~32)
void ContractABITypeCodecSolImpl::serialize(bytes _in, int, bcos::bytes& _buffer)
{
    if (_in.size() > 32)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "bytesN length must be must be in range 0 < M <= 32, the real length is: " +
            std::to_string(_in.size())));
    }

    auto temp = h256(_in + bytes(32 - _in.size(), 0)).asBytes();
    _buffer.insert(_buffer.end(), temp.begin(), temp.end());
}

// bytes
void ContractABITypeCodecSolImpl::serialize(bytes _in, bcos::bytes& _buffer)
{
    auto temp = h256(u256(_in.size())).asBytes();
    temp.resize(temp.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&temp).getCroppedData(32));

    _buffer.insert(_buffer.end(), temp.begin(), temp.end());
}

// string
void ContractABITypeCodecSolImpl::serialize(const std::string& _in, bcos::bytes& _buffer)
{
    auto temp = h256(u256(_in.size())).asBytes();
    temp.resize(temp.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&temp).getCroppedData(32));

    _buffer.insert(_buffer.end(), temp.begin(), temp.end());
}


// AbstractType
void ContractABITypeCodecSolImpl::serialize(const AbstractType& _in, bcos::bytes& _buffer)
{
    auto type = _in.type();
    switch (type)
    {
    case abi::Type::BOOL:
    {
        auto& b = static_cast<const abi::Boolean&>(_in);
        serialize(b.value(), _buffer);
        break;
    }  // bool
    case abi::Type::UINT:
    {
        auto& u = static_cast<const abi::Uint&>(_in);
        serialize(u.value(), 256, _buffer);
        break;
    }  // uint<M>
    case abi::Type::INT:
    {
        auto& i = static_cast<const abi::Int&>(_in);
        serialize(i.value(), 256, _buffer);
        break;
    }  // int<M>
    case abi::Type::FIXED_BYTES:
    {
        auto& bs = static_cast<const abi::FixedBytes&>(_in);
        serialize(bs.value(), true, _buffer);
        break;
    }  // byteN
    case abi::Type::ADDRESS:
    {
        try
        {
            auto& addr = static_cast<const abi::Addr&>(_in);
            serialize(bcos::Address(addr.value()), _buffer);
        }
        catch (std::exception& e)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "invalid solidity address: " + static_cast<const abi::Addr&>(_in).value()));
        }

        break;
    }  // address
    case abi::Type::STRING:
    {
        auto& str = static_cast<const abi::String&>(_in);
        serialize(str.value(), _buffer);
        break;
    }  // string
    case abi::Type::DYNAMIC_BYTES:
    {
        auto& bs = static_cast<const abi::DynamicBytes&>(_in);
        serialize(bs.value(), _buffer);
        break;
    }  // bytes
    case abi::Type::FIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported fixed type"));
        break;
    }  // fixed<M>x<N>, unsupported type
    case abi::Type::UFIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported unfixed type"));
        break;
    }  // ufixed<M>x<N>, unsupported type
    case abi::Type::DYNAMIC_LIST:
    {
        auto& dynamicList = static_cast<const abi::DynamicList&>(_in);

        std::size_t offset = dynamicList.value().size() * MAX_BYTE_LENGTH;

        // list size
        serialize(u256(dynamicList.value().size()), 256, _buffer);

        bcos::bytes tempBuffer;
        bcos::bytes offsetBuffer;
        bcos::bytes valueBuffer;
        for (const auto& t : dynamicList.value())
        {
            tempBuffer.clear();
            serialize(*t, tempBuffer);
            valueBuffer.insert(valueBuffer.end(), tempBuffer.begin(), tempBuffer.end());
            if (t->dynamicType())
            {
                serialize(u256(offset), 256, offsetBuffer);
                offset += tempBuffer.size();
            }
        }

        _buffer.insert(_buffer.end(), offsetBuffer.begin(), offsetBuffer.end());
        _buffer.insert(_buffer.end(), valueBuffer.begin(), valueBuffer.end());

        break;
    }  // T[]
    case abi::Type::FIXED_LIST:
    {
        auto& fixedList = static_cast<const abi::FixedList&>(_in);
        std::size_t offset = fixedList.value().size() * MAX_BYTE_LENGTH;

        bcos::bytes tempBuffer;
        bcos::bytes offsetBuffer;
        bcos::bytes valueBuffer;
        for (const auto& t : fixedList.value())
        {
            tempBuffer.clear();
            serialize(*t, tempBuffer);
            valueBuffer.insert(valueBuffer.end(), tempBuffer.begin(), tempBuffer.end());
            if (t->dynamicType())
            {
                serialize(u256(offset), 256, offsetBuffer);
                offset += tempBuffer.size();
            }
        }

        _buffer.insert(_buffer.end(), offsetBuffer.begin(), offsetBuffer.end());
        _buffer.insert(_buffer.end(), valueBuffer.begin(), valueBuffer.end());
        break;
    }  // T[N]
    case abi::Type::STRUCT:
    {
        auto structValue = static_cast<const abi::Struct&>(_in);

        std::size_t dynamicOffset = 0;
        bcos::bytes fixedBuffer;
        bcos::bytes dynamicBuffer;

        // calc the initial value of the dynamic offset
        for (const auto& t : structValue.value())
        {
            dynamicOffset += t->offsetAsBytes();
        }

        bcos::bytes tempBuffer;
        // encode struct
        for (const auto& t : structValue.value())
        {
            tempBuffer.clear();
            serialize(*t, tempBuffer);

            if (t->dynamicType())
            {
                serialize(u256(dynamicOffset), 256, fixedBuffer);
                dynamicBuffer.insert(dynamicBuffer.end(), tempBuffer.begin(), tempBuffer.end());
                dynamicOffset += tempBuffer.size();
            }
            else
            {
                fixedBuffer.insert(fixedBuffer.end(), tempBuffer.begin(), tempBuffer.end());
            }
        }

        _buffer.insert(_buffer.end(), fixedBuffer.begin(), fixedBuffer.end());
        _buffer.insert(_buffer.end(), dynamicBuffer.begin(), dynamicBuffer.end());
        break;
    }  // struct

    default:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unrecognized type, solidity codec impl"));
    }
    }
}

//--------------- ContractABITypeCodecSolImpl::serialize end ----------------------


//--------------- ContractABITypeCodecSolImpl::deserialize begin ----------------------

// uint256
void ContractABITypeCodecSolImpl::deserialize(
    u256& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    verifyBufferOffset(_buffer, _offset + MAX_BYTE_LENGTH - 1);

    _out = fromBigEndian<u256>(bcos::bytesConstRef((bcos::byte*)_buffer.data(), _buffer.size())
                                   .getCroppedData(_offset, MAX_BYTE_LENGTH));
}

// int256
void ContractABITypeCodecSolImpl::deserialize(
    s256& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    u256 value;
    deserialize(value, _buffer, _offset);
    _out = u2s(value);
}

// bool
void ContractABITypeCodecSolImpl::deserialize(
    bool& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    u256 value;
    deserialize(value, _buffer, _offset);
    _out = value > 0 ? true : false;
}

// address
void ContractABITypeCodecSolImpl::deserialize(
    Address& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    verifyBufferOffset(_buffer, _offset + MAX_BYTE_LENGTH - 1);
    bcos::bytesConstRef((bcos::byte*)_buffer.data(), _buffer.size())
        .getCroppedData(_offset + MAX_BYTE_LENGTH - 20, 20)
        .populate(_out.ref());
}

// bytesN(1~32)
void ContractABITypeCodecSolImpl::deserialize(
    bytes& _out, const bcos::bytes& _buffer, std::size_t _offset, std::size_t _fixedN)
{
    if (_fixedN > 32)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "bytesN length must be must be in range 0 < M <= 32, the real length is: " +
            std::to_string(_fixedN)));
    }

    verifyBufferOffset(_buffer, _offset + MAX_BYTE_LENGTH - 1);
    _out = bcos::bytesConstRef((bcos::byte*)_buffer.data(), _buffer.size())
               .getCroppedData(_offset, _fixedN)
               .toBytes();
}

// bytes
void ContractABITypeCodecSolImpl::deserialize(
    bytes& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    // length
    u256 len;
    deserialize(len, _buffer, _offset);

    // bytes content
    verifyBufferOffset(_buffer, _offset + MAX_BYTE_LENGTH + (std::size_t)len - 1);
    _out = bcos::bytesConstRef((bcos::byte*)_buffer.data(), _buffer.size())
               .getCroppedData(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len))
               .toBytes();
}

// std::string
void ContractABITypeCodecSolImpl::deserialize(
    std::string& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    // length
    u256 len;
    deserialize(len, _buffer, _offset);

    // bytes content
    verifyBufferOffset(_buffer, _offset + MAX_BYTE_LENGTH + (std::size_t)len - 1);
    auto bytesValue = bcos::bytesConstRef((bcos::byte*)_buffer.data(), _buffer.size())
                          .getCroppedData(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len))
                          .toBytes();

    _out.assign((const char*)bytesValue.data(), bytesValue.size());
}

// AbstractType
void ContractABITypeCodecSolImpl::deserialize(
    AbstractType& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    auto type = _out.type();
    switch (type)
    {
    case abi::Type::BOOL:
    {
        bool value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::Boolean&>(_out).setValue(value);
        break;
    }  // bool
    case abi::Type::UINT:
    {
        u256 value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::Uint&>(_out).setValue(value);
        break;
    }  // uint<M>
    case abi::Type::INT:
    {
        s256 value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::Int&>(_out).setValue(value);
        break;
    }  // int<M>
    case abi::Type::FIXED_BYTES:
    {
        bcos::bytes value;
        deserialize(value, _buffer, _offset, static_cast<abi::FixedBytes&>(_out).fixedN());
        static_cast<abi::FixedBytes&>(_out).setValue(value);
        break;
    }  // byteN
    case abi::Type::ADDRESS:
    {
        Address addr;
        deserialize(addr, _buffer, _offset);
        static_cast<abi::Addr&>(_out).setValue(addr.hexPrefixed());
        break;
    }  // address
    case abi::Type::STRING:
    {
        std::string value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::String&>(_out).setValue(value);
        break;
    }  // string
    case abi::Type::DYNAMIC_BYTES:
    {
        bcos::bytes value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::DynamicBytes&>(_out).setValue(value);
        break;
    }  // bytes
    case abi::Type::FIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported fixed type"));
        break;
    }  // fixed<M>x<N>, unsupported type
    case abi::Type::UFIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported unfixed type"));
        break;
    }  // ufixed<M>x<N>, unsupported type
    case abi::Type::DYNAMIC_LIST:
    {
        // list length
        u256 listSize = 0;
        deserialize(listSize, _buffer, _offset);

        assert(listSize >= 0 && listSize < INT_MAX);

        _offset += MAX_BYTE_LENGTH;
        int currentOffset = _offset;

        auto& dyListRef = static_cast<DynamicList&>(_out);

        if (!dyListRef.value().empty())
        {
            auto templateTV = dyListRef.value()[0];
            dyListRef.setEmpty();
            for (std::size_t i = 0; i < listSize; i++)
            {
                auto tv = templateTV->clone();
                if (tv->dynamicType())
                {
                    u256 valueOffset;
                    deserialize(valueOffset, _buffer, currentOffset);

                    // UTILITIES_ABI_LOG(TRACE) << LOG_KV("valueOffset", valueOffset);
                    assert(valueOffset >= 0 && valueOffset < LONG_MAX);

                    deserialize(*tv, _buffer, _offset + (std::size_t)valueOffset);
                }
                else
                {
                    deserialize(*tv, _buffer, currentOffset);
                }

                currentOffset += tv->offsetAsBytes();

                dyListRef.addMember(std::move(tv));
            }
        }
        else
        {
            // empty dynamic list
            UTILITIES_ABI_LOG(INFO)
                << LOG_BADGE("deserialize::T[]") << LOG_DESC("empty dynamic list");
        }

        break;
    }  // T[]
    case abi::Type::FIXED_LIST:
    {
        auto& fixedListRef = static_cast<FixedList&>(_out);

        // list length
        u256 listSize = fixedListRef.dimension();
        assert(listSize == fixedListRef.value().size());

        int currentOffset = _offset;
        for (std::size_t i = 0; i < listSize; i++)
        {
            auto tv = fixedListRef.value()[i];
            if (tv->dynamicType())
            {
                u256 valueOffset;
                deserialize(valueOffset, _buffer, currentOffset);
                assert(valueOffset > 0 && valueOffset < LONG_MAX);

                deserialize(*tv, _buffer, _offset + (std::size_t)valueOffset);
            }
            else
            {
                deserialize(*tv, _buffer, currentOffset);
            }

            currentOffset += tv->offsetAsBytes();
        }

        break;
    }  // T[N]
    case abi::Type::STRUCT:
    {
        auto& structRef = static_cast<bcos::cppsdk::abi::Struct&>(_out);

        int currentOffset = _offset;
        for (std::size_t i = 0; i < structRef.value().size(); ++i)
        {
            auto tv = structRef.value()[i];
            if (tv->dynamicType())
            {
                u256 valueOffset = 0;
                deserialize(valueOffset, _buffer, currentOffset);
                deserialize(*tv, _buffer, _offset + (std::size_t)valueOffset);
            }
            else
            {
                deserialize(*tv, _buffer, currentOffset);
            }

            currentOffset += tv->offsetAsBytes();
        }
        break;
    }  // struct

    default:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unrecognized type, solidity codec impl"));
    }
    }
}

//--------------- liquid serialize begin ----------------------
// uint256
void ContractABITypeCodecLiquidImpl::serialize(
    const u256& _in, std::size_t _extent, bcos::bytes& _buffer)
{
    std::ignore = _in;
    std::ignore = _extent;
    std::ignore = _buffer;
    // TODO:
    /*
    bytes bigEndianData = toBigEndian(_in);
    _buffer.insert(_buffer.end(), bigEndianData.begin(), bigEndianData.end());
    */
}

// int256
void ContractABITypeCodecLiquidImpl::serialize(
    const s256& _in, std::size_t _extent, bcos::bytes& _buffer)
{
    std::ignore = _in;
    std::ignore = _extent;
    std::ignore = _buffer;
    // TODO:
}

// bool
void ContractABITypeCodecLiquidImpl::serialize(bool _in, bcos::bytes& _buffer)
{
    _buffer.push_back(_in ? 1u : 0u);
}

// address
void ContractABITypeCodecLiquidImpl::serialize(const Address& _in, bcos::bytes& _buffer)
{
    std::ignore = _in;
    std::ignore = _buffer;

    throw std::runtime_error(
        "the address type is not supported in liquid abi, the contract address type is string, "
        "please use "
        "string "
        "instead.");
}

// bytesN(1~32)
void ContractABITypeCodecLiquidImpl::serialize(bytes _in, int, bcos::bytes& _buffer)
{
    if (_in.size() > 32)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "bytesN length must be must be in range 0 < M <= 32, the real length is: " +
            std::to_string(_in.size())));
    }

    _buffer.insert(_buffer.end(), _in.begin(), _in.end());
}

// bytes
void ContractABITypeCodecLiquidImpl::serialize(bytes _in, bcos::bytes& _buffer)
{
    serialize((u256)_in.size(), 256, _buffer);
    _buffer.insert(_buffer.end(), _in.begin(), _in.end());
}

// string
void ContractABITypeCodecLiquidImpl::serialize(const std::string& _in, bcos::bytes& _buffer)
{
    serialize((u256)_in.size(), 256, _buffer);
    _buffer.insert(_buffer.end(), _in.begin(), _in.end());
}

// AbstractType
void ContractABITypeCodecLiquidImpl::serialize(const AbstractType& _in, bcos::bytes& _buffer)
{
    auto type = _in.type();
    switch (type)
    {
    case abi::Type::BOOL:
    {
        auto& b = static_cast<const abi::Boolean&>(_in);
        serialize(b.value(), _buffer);
        break;
    }  // bool
    case abi::Type::UINT:
    {
        auto& u = static_cast<const abi::Uint&>(_in);
        serialize(u.value(), u.extent(), _buffer);
        break;
    }  // uint<M>
    case abi::Type::INT:
    {
        auto& i = static_cast<const abi::Int&>(_in);
        serialize(i.value(), i.extent(), _buffer);
        break;
    }  // int<M>
    case abi::Type::FIXED_BYTES:
    {
        auto& bs = static_cast<const abi::FixedBytes&>(_in);
        serialize(bs.value(), true, _buffer);
        break;
    }  // byteN
    case abi::Type::ADDRESS:
    {
        auto& addr = static_cast<const abi::Addr&>(_in);
        serialize(addr.value(), _buffer);
        break;
    }  // address
    case abi::Type::STRING:
    {
        auto& str = static_cast<const abi::String&>(_in);
        serialize(str.value(), _buffer);
        break;
    }  // string
    case abi::Type::DYNAMIC_BYTES:
    {
        auto& bs = static_cast<const abi::DynamicBytes&>(_in);
        serialize(bs.value(), _buffer);
        break;
    }  // bytes
    case abi::Type::FIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported fixed type"));
        break;
    }  // fixed<M>x<N>, unsupported type
    case abi::Type::UFIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported unfixed type"));
        break;
    }  // ufixed<M>x<N>, unsupported type
    case abi::Type::DYNAMIC_LIST:
    {
        auto& dynamicList = static_cast<const abi::DynamicList&>(_in);
        serialize((u256)dynamicList.value().size(), 256, _buffer);
        for (const auto& t : dynamicList.value())
        {
            serialize(*t, _buffer);
        }

        break;
    }  // T[]
    case abi::Type::FIXED_LIST:
    {
        auto& fixedList = static_cast<const abi::FixedList&>(_in);
        for (const auto& t : fixedList.value())
        {
            serialize(*t, _buffer);
        }
        break;
    }  // T[N]
    case abi::Type::STRUCT:
    {
        auto structValue = static_cast<const abi::Struct&>(_in);
        // encode struct
        for (const auto& t : structValue.value())
        {
            serialize(*t, _buffer);
        }

        break;
    }  // struct

    default:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unrecognized type, liquid codec impl"));
    }
    }
}
//--------------- liquid serialize end ----------------------

//--------------- liquid deserialize begin ----------------------
// uint256
void ContractABITypeCodecLiquidImpl::deserialize(
    u256& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    // TODO:
    std::ignore = _out;
    std::ignore = _buffer;
    std::ignore = _offset;
}

// int256
void ContractABITypeCodecLiquidImpl::deserialize(
    s256& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    // TODO:
    std::ignore = _out;
    std::ignore = _buffer;
    std::ignore = _offset;
}

// bool
void ContractABITypeCodecLiquidImpl::deserialize(
    bool& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    verifyBufferOffset(_buffer, _offset);
    _out = (_buffer[_offset] == 0 ? false : true);
}

// address
void ContractABITypeCodecLiquidImpl::deserialize(
    Address& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    std::ignore = _out;
    std::ignore = _buffer;
    std::ignore = _offset;

    throw std::runtime_error(
        "the address type is not supported in liquid abi, the contract address type is string, "
        "please use "
        "string "
        "instead.");
}

// bytesN(1~32)
void ContractABITypeCodecLiquidImpl::deserialize(
    bytes& _out, const bcos::bytes& _buffer, std::size_t _offset, std::size_t _fixedN)
{
    verifyBufferOffset(_buffer, _offset + _fixedN - 1);
    _out = bcos::bytesConstRef((bcos::byte*)_buffer.data(), _buffer.size())
               .getCroppedData(_offset, _fixedN)
               .toBytes();
}

// bytes
void ContractABITypeCodecLiquidImpl::deserialize(
    bytes& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    // TODO:
    std::ignore = _out;
    std::ignore = _buffer;
    std::ignore = _offset;
}

// std::string
void ContractABITypeCodecLiquidImpl::deserialize(
    std::string& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    // TODO:
    std::ignore = _out;
    std::ignore = _buffer;
    std::ignore = _offset;
}

// AbstractType
void ContractABITypeCodecLiquidImpl::deserialize(
    AbstractType& _out, const bcos::bytes& _buffer, std::size_t _offset)
{
    auto type = _out.type();
    switch (type)
    {
    case abi::Type::BOOL:
    {
        bool value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::Boolean&>(_out).setValue(value);
        break;
    }  // bool
    case abi::Type::UINT:
    {
        u256 value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::Uint&>(_out).setValue(value);
        break;
    }  // uint<M>
    case abi::Type::INT:
    {
        s256 value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::Int&>(_out).setValue(value);
        break;
    }  // int<M>
    case abi::Type::FIXED_BYTES:
    {
        bcos::bytes value;
        deserialize(value, _buffer, _offset, static_cast<abi::FixedBytes&>(_out).fixedN());
        static_cast<abi::FixedBytes&>(_out).setValue(value);
        break;
    }  // byteN
    case abi::Type::ADDRESS:
    {
        Address addr;

        deserialize(addr, _buffer, _offset);
        static_cast<abi::Addr&>(_out).setValue(addr.hexPrefixed());
        break;
    }  // address
    case abi::Type::STRING:
    {
        std::string value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::String&>(_out).setValue(value);
        break;
    }  // string
    case abi::Type::DYNAMIC_BYTES:
    {
        bcos::bytes value;
        deserialize(value, _buffer, _offset);
        static_cast<abi::DynamicBytes&>(_out).setValue(value);
        break;
    }  // bytes
    case abi::Type::FIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported fixed type"));
        break;
    }  // fixed<M>x<N>, unsupported type
    case abi::Type::UFIXED:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unsupported unfixed type"));
        break;
    }  // ufixed<M>x<N>, unsupported type
    case abi::Type::DYNAMIC_LIST:
    {
        // list length
        u256 listSize = 0;
        deserialize(listSize, _buffer, _offset);

        assert(listSize >= 0 && listSize < INT_MAX);

        _offset += MAX_BYTE_LENGTH;
        int currentOffset = _offset;

        auto& dyListRef = static_cast<DynamicList&>(_out);

        if (!dyListRef.value().empty())
        {
            auto templateTV = dyListRef.value()[0];
            dyListRef.setEmpty();
            for (std::size_t i = 0; i < listSize; i++)
            {}
        }
        else
        {
            // empty dynamic list
            UTILITIES_ABI_LOG(INFO)
                << LOG_BADGE("deserialize::T[]") << LOG_DESC("empty dynamic list");
        }

        break;
    }  // T[]
    case abi::Type::FIXED_LIST:
    {
        auto& fixedListRef = static_cast<FixedList&>(_out);

        // list length
        u256 listSize = fixedListRef.dimension();
        assert(listSize == fixedListRef.value().size());

        int currentOffset = _offset;
        for (std::size_t i = 0; i < listSize; i++)
        {
            auto tv = fixedListRef.value()[i];
        }

        break;
    }  // T[N]
    case abi::Type::STRUCT:
    {
        auto& structRef = static_cast<bcos::cppsdk::abi::Struct&>(_out);
        for (std::size_t i = 0; i < structRef.value().size(); ++i)
        {}
        break;
    }  // struct

    default:
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("unrecognized type, liquid codec impl"));
    }
    }
}
//--------------- liquid deserialize end ----------------------
