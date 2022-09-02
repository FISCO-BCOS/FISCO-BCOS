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
 * @brief Contract ABI serialize and deserialize tool.
 * @author: octopuswang
 * @date: 2019-04-01
 */

#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>

using namespace std;
using namespace bcos;
using namespace bcos::codec::abi;

const int ContractABICodec::MAX_BYTE_LENGTH;

bool ContractABICodec::abiOutByFuncSelector(
    bytesConstRef _data, const std::vector<std::string>& _allTypes, std::vector<std::string>& _out)
{
    data = _data;
    offset = 0;

    for (const std::string& type : _allTypes)
    {
        if ("int" == type || "int256" == type)
        {
            s256 s;
            deserialize(s, offset);
            _out.push_back(toString(s));
        }
        else if ("uint" == type || "uint256" == type)
        {
            u256 u;
            deserialize(u, offset);
            _out.push_back(toString(u));
        }
        else if ("address" == type)
        {
            Address addr;
            deserialize(addr, offset);
            _out.push_back(addr.hex());
        }
        else if ("string" == type)
        {
            u256 stringOffset;
            deserialize(stringOffset, offset);

            std::string str;
            deserialize(str, static_cast<std::size_t>(stringOffset));
            _out.push_back(str);
        }
        else
        {  // unsupported type
            return false;
        }

        offset += MAX_BYTE_LENGTH;
    }

    return true;
}

// unsigned integer type uint256.
bytes ContractABICodec::serialise(const int& _in)
{
    return serialise((s256)_in);
}

// unsigned integer type uint256.
bytes ContractABICodec::serialise(const u256& _in)
{
    return h256(_in).asBytes();
}

// two’s complement signed integer type int256.
bytes ContractABICodec::serialise(const s256& _in)
{
    return h256(_in.convert_to<u256>()).asBytes();
}

// equivalent to uint8 restricted to the values 0 and 1. For computing the function selector,
// bool is used
bytes ContractABICodec::serialise(const bool& _in)
{
    return h256(u256(_in ? 1 : 0)).asBytes();
}

// equivalent to uint160, except for the assumed interpretation and language typing. For
// computing the function selector, address is used.
// bool is used.
bytes ContractABICodec::serialise(const Address& _in)
{
    return bytes(12, 0) + _in.asBytes();
}

// binary type of 32 bytes
bytes ContractABICodec::serialise(const string32& _in)
{
    bytes ret(32, 0);
    bytesConstRef((byte const*)_in.data(), 32).populate(bytesRef(&ret));
    return ret;
}

bytes ContractABICodec::serialise(const bytes& _in)
{
    bytes ret;
    ret = h256(u256(_in.size())).asBytes();
    ret.resize(ret.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&ret).getCroppedData(32));
    return ret;
}

// dynamic sized unicode string assumed to be UTF-8 encoded.
bytes ContractABICodec::serialise(const std::string& _in)
{
    bytes ret;
    ret = h256(u256(_in.size())).asBytes();
    ret.resize(ret.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&ret).getCroppedData(32));
    return ret;
}

void ContractABICodec::deserialize(s256& out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 u = fromBigEndian<u256>(data.getCroppedData(_offset, MAX_BYTE_LENGTH));
    if (u > u256("0x8fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"))
    {
        auto r =
            (bcos::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") - u) +
            1;
        out = s256("-" + r.str());
    }
    else
    {
        out = u.convert_to<s256>();
    }
}

void ContractABICodec::deserialize(u256& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    _out = fromBigEndian<u256>(data.getCroppedData(_offset, MAX_BYTE_LENGTH));
}

void ContractABICodec::deserialize(bool& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 ret = fromBigEndian<u256>(data.getCroppedData(_offset, MAX_BYTE_LENGTH));
    _out = ret > 0 ? true : false;
}

void ContractABICodec::deserialize(Address& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    data.getCroppedData(_offset + MAX_BYTE_LENGTH - 20, 20).populate(_out.ref());
}

void ContractABICodec::deserialize(string32& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    data.getCroppedData(_offset, MAX_BYTE_LENGTH)
        .populate(bytesRef((byte*)_out.data(), MAX_BYTE_LENGTH));
}

void ContractABICodec::deserialize(std::string& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 len = fromBigEndian<u256>(data.getCroppedData(_offset, MAX_BYTE_LENGTH));
    validOffset(_offset + MAX_BYTE_LENGTH + (std::size_t)len - 1);
    auto result = data.getCroppedData(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len));
    _out.assign((const char*)result.data(), result.size());
}

void ContractABICodec::deserialize(bytes& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 len = fromBigEndian<u256>(data.getCroppedData(_offset, MAX_BYTE_LENGTH));
    validOffset(_offset + MAX_BYTE_LENGTH + (std::size_t)len - 1);
    _out = data.getCroppedData(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len)).toBytes();
}