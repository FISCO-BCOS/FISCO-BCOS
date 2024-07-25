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
 * @brief vm common
 * @file Common.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "Common.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <bcos-utilities/Common.h>

using namespace bcos::protocol;

namespace bcos
{
bool hasWasmPreamble(const std::string_view& _input)
{
    return hasWasmPreamble(
        bytesConstRef(reinterpret_cast<const byte*>(_input.data()), _input.size()));
}
bool hasWasmPreamble(const bytesConstRef& _input)
{
    return _input.size() >= 8 && _input[0] == 0 && _input[1] == 'a' && _input[2] == 's' &&
           _input[3] == 'm';
}

bool hasWasmPreamble(const bytes& _input)
{
    return hasWasmPreamble(bytesConstRef(_input.data(), _input.size()));
}

bool hasPrecompiledPrefix(const std::string_view& _code)
{
    return _code.size() > precompiled::PRECOMPILED_CODE_FIELD_SIZE &&
           _code.substr(0, precompiled::PRECOMPILED_CODE_FIELD_SIZE) ==
               precompiled::PRECOMPILED_CODE_FIELD;
}

namespace executor
{
TransactionStatus toTransactionStatus(Exception const& _e)
{
    if (!!dynamic_cast<OutOfGasLimit const*>(&_e))
        return TransactionStatus::OutOfGasLimit;
    if (!!dynamic_cast<NotEnoughCash const*>(&_e))
        return TransactionStatus::NotEnoughCash;
    if (!!dynamic_cast<BadInstruction const*>(&_e))
        return TransactionStatus::BadInstruction;
    if (!!dynamic_cast<BadJumpDestination const*>(&_e))
        return TransactionStatus::BadJumpDestination;
    if (!!dynamic_cast<OutOfGas const*>(&_e))
        return TransactionStatus::OutOfGas;
    if (!!dynamic_cast<OutOfStack const*>(&_e))
        return TransactionStatus::OutOfStack;
    if (!!dynamic_cast<StackUnderflow const*>(&_e))
        return TransactionStatus::StackUnderflow;
    if (!!dynamic_cast<ContractAddressAlreadyUsed const*>(&_e))
        return TransactionStatus::ContractAddressAlreadyUsed;
    if (!!dynamic_cast<PrecompiledError const*>(&_e))
        return TransactionStatus::PrecompiledError;
    if (!!dynamic_cast<RevertInstruction const*>(&_e))
        return TransactionStatus::RevertInstruction;
    if (!!dynamic_cast<PermissionDenied const*>(&_e))
        return TransactionStatus::PermissionDenied;
    if (!!dynamic_cast<CallAddressError const*>(&_e))
        return TransactionStatus::CallAddressError;
    if (!!dynamic_cast<GasOverflow const*>(&_e))
        return TransactionStatus::GasOverflow;
    if (!!dynamic_cast<ContractFrozen const*>(&_e))
        return TransactionStatus::ContractFrozen;
    if (!!dynamic_cast<AccountFrozen const*>(&_e))
        return TransactionStatus::AccountFrozen;
    return TransactionStatus::Unknown;
}

}  // namespace executor

bytes getComponentBytes(size_t index, const std::string& typeName, const bytesConstRef& data)
{
    // the string length will never exceed uint64_t, so we use uint64_t to store the length
    constexpr int32_t slotSize = 32;
    constexpr int32_t offsetBegin = 24;
    size_t indexOffset = index * slotSize;
    if (data.size() < indexOffset + slotSize)
    {  // the input is invalid(not match the abi)
        return {};
    }
    if (typeName == "string" || typeName == "bytes")
    {
        uint64_t offset = 0;
        const uint8_t* dynamicArrayLenData = data.data() + indexOffset + offsetBegin;
        std::reverse_copy(
            dynamicArrayLenData, dynamicArrayLenData + sizeof(uint64_t), (uint8_t*)&offset);
        const uint8_t* dynamicArray = data.data() + offset;
        uint64_t dataLength = 0;
        // dataLength is the length of the string or bytes
        // dynamicArray = [32 bytes length, dataLength bytes data]
        std::reverse_copy(
            dynamicArray + offsetBegin, dynamicArray + slotSize, (uint8_t*)&dataLength);
        const uint8_t* rawData = dynamicArray + slotSize;
        return {rawData, rawData + dataLength};
    }
    return {data.begin() + indexOffset, data.begin() + indexOffset + slotSize};
}
evmc_address unhexAddress(std::string_view view)
{
    if (view.empty())
    {
        return {};
    }
    if (view.starts_with("0x"))
    {
        view = view.substr(2);
    }
    evmc_address address;
    if (view.empty())
    {
        std::uninitialized_fill(address.bytes, address.bytes + sizeof(address.bytes), 0);
    }
    else
    {
        boost::algorithm::unhex(view, address.bytes);
    }
    return address;
}
std::string addressBytesStr2HexString(std::string_view receiveAddressBytes)
{
    std::string strAddress;
    strAddress.reserve(receiveAddressBytes.size() * 2);
    boost::algorithm::hex_lower(
        receiveAddressBytes.begin(), receiveAddressBytes.end(), std::back_inserter(strAddress));
    return strAddress;
}
std::string address2HexString(const evmc_address& address)
{
    auto receiveAddressBytes = fromEvmC(address);
    return addressBytesStr2HexString(receiveAddressBytes);
}
std::array<char, sizeof(evmc_address) * 2> address2FixedArray(const evmc_address& address)
{
    std::array<char, sizeof(evmc_address) * 2> array;
    auto receiveAddressBytes = fromEvmC(address);
    boost::algorithm::hex_lower(receiveAddressBytes, array.data());
    return array;
}
}  // namespace bcos
