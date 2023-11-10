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
 * @file ContractABITypeCodec.h
 * @author: octopus
 * @date 2022-05-19
 */
#pragma once

#include "bcos-cpp-sdk/utilities/abi/ContractABIType.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <memory>
namespace bcos
{
namespace cppsdk
{
namespace abi
{

// encode or decode for abi base type
class ContractABITypeCodecInterface
{
public:
    using Ptr = std::shared_ptr<ContractABITypeCodecInterface>;
    using ConstPtr = std::shared_ptr<const ContractABITypeCodecInterface>;

public:
    virtual ~ContractABITypeCodecInterface() {}

public:
    //--------------- serialize begin ----------------------
    // uint256
    virtual void serialize(const u256& _in, std::size_t _extent, bcos::bytes& _buffer) = 0;

    // int256
    virtual void serialize(const s256& _in, std::size_t _extent, bcos::bytes& _buffer) = 0;

    // bool
    virtual void serialize(bool _in, bcos::bytes& _buffer) = 0;

    // address
    virtual void serialize(const Address& _in, bcos::bytes& _buffer) = 0;

    // bytesN(1~32)
    virtual void serialize(bytes _in, int, bcos::bytes& _buffer) = 0;

    // bytes
    virtual void serialize(bytes _in, bcos::bytes& _buffer) = 0;

    // string
    virtual void serialize(const std::string& _in, bcos::bytes& _buffer) = 0;

    // AbstractType
    virtual void serialize(const AbstractType& _in, bcos::bytes& _buffer) = 0;

    //--------------- serialize end ----------------------


    //--------------- deserialize begin ----------------------
    // uint256
    virtual void deserialize(u256& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    // int256
    virtual void deserialize(s256& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    // bool
    virtual void deserialize(bool& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    // address
    virtual void deserialize(Address& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    // bytesN(1~32)
    virtual void deserialize(
        bytes& _out, const bcos::bytes& _buffer, std::size_t _offset, std::size_t _fixedN) = 0;

    // bytes
    virtual void deserialize(bytes& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    // std::string
    virtual void deserialize(
        std::string& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    // AbstractType
    virtual void deserialize(
        AbstractType& _out, const bcos::bytes& _buffer, std::size_t _offset) = 0;

    //--------------- deserialize end ----------------------
};

// encode or decode for solidity abi base type
class ContractABITypeCodecSolImpl : public ContractABITypeCodecInterface
{
public:
    //--------------- serialize begin ----------------------
    // uint256
    virtual void serialize(const u256& _in, std::size_t _extent, bcos::bytes& _buffer);

    // int256
    virtual void serialize(const s256& _in, std::size_t _extent, bcos::bytes& _buffer);

    // bool
    virtual void serialize(bool _in, bcos::bytes& _buffer);

    // address
    virtual void serialize(const Address& _in, bcos::bytes& _buffer);

    // bytesN(1~32)
    virtual void serialize(bytes _in, int, bcos::bytes& _buffer);

    // bytes
    virtual void serialize(bytes _in, bcos::bytes& _buffer);

    // string
    virtual void serialize(const std::string& _in, bcos::bytes& _buffer);

    // AbstractType
    virtual void serialize(const AbstractType& _in, bcos::bytes& _buffer);
    //--------------- serialize end ----------------------


    //--------------- deserialize begin ----------------------
    // uint8~256
    virtual void deserialize(u256& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // int8~256
    virtual void deserialize(s256& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // bool
    virtual void deserialize(bool& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // address
    virtual void deserialize(Address& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // bytesN(1~32)
    virtual void deserialize(
        bytes& _out, const bcos::bytes& _buffer, std::size_t _offset, std::size_t _fixedN);

    // bytes
    virtual void deserialize(bytes& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // std::string
    virtual void deserialize(std::string& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // AbstractType
    virtual void deserialize(AbstractType& _out, const bcos::bytes& _buffer, std::size_t _offset);

    //--------------- deserialize end ----------------------
};

// encode or decode for liquid abi base type
class ContractABITypeCodecLiquidImpl : public ContractABITypeCodecInterface
{
public:
    //--------------- serialize begin ----------------------
    // uint256
    virtual void serialize(const u256& _in, std::size_t _extent, bcos::bytes& _buffer);

    // int256
    virtual void serialize(const s256& _in, std::size_t _extent, bcos::bytes& _buffer);

    // bool
    virtual void serialize(bool _in, bcos::bytes& _buffer);

    // address
    virtual void serialize(const Address& _in, bcos::bytes& _buffer);

    // bytesN(1~32)
    virtual void serialize(bytes _in, int, bcos::bytes& _buffer);

    // bytes
    virtual void serialize(bytes _in, bcos::bytes& _buffer);

    // string
    virtual void serialize(const std::string& _in, bcos::bytes& _buffer);

    // AbstractType
    virtual void serialize(const AbstractType& _in, bcos::bytes& _buffer);
    //--------------- serialize end ----------------------


    //--------------- deserialize begin ----------------------
    // uint256
    virtual void deserialize(u256& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // int256
    virtual void deserialize(s256& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // bool
    virtual void deserialize(bool& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // address
    virtual void deserialize(Address& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // bytesN(1~32)
    virtual void deserialize(
        bytes& _out, const bcos::bytes& _buffer, std::size_t _offset, std::size_t _fixedN);

    // bytes
    virtual void deserialize(bytes& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // std::string
    virtual void deserialize(std::string& _out, const bcos::bytes& _buffer, std::size_t _offset);

    // AbstractType
    virtual void deserialize(AbstractType& _out, const bcos::bytes& _buffer, std::size_t _offset);

    //--------------- deserialize end ----------------------
};

}  // namespace abi
}  // namespace cppsdk
}  // namespace bcos
