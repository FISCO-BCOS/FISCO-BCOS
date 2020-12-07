/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief Contract ABI serialize and deserialize tool.
 * @author: octopuswang
 * @date: 2019-04-01
 */

#include "ABI.h"
#include <libdevcore/FixedHash.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::eth::abi;

const int ContractABI::MAX_BYTE_LENGTH;

bool ContractABI::abiOutByFuncSelector(
    bytesConstRef _data, const std::vector<std::string>& _allTypes, std::vector<std::string>& _out)
{
    data = _data;
    offset = 0;

    for (const std::string& type : _allTypes)
    {
        if ("int" == type || "int256" == type)
        {
            s256 s;
            deserialise(s, offset);
            _out.push_back(toString(s));
        }
        else if ("uint" == type || "uint256" == type)
        {
            u256 u;
            deserialise(u, offset);
            _out.push_back(toString(u));
        }
        else if ("address" == type)
        {
            Address addr;
            deserialise(addr, offset);
            _out.push_back(addr.hex());
        }
        else if ("string" == type)
        {
            u256 stringOffset;
            deserialise(stringOffset, offset);

            std::string str;
            deserialise(str, static_cast<std::size_t>(stringOffset));
            _out.push_back(str);
        }
        else
        {  // unsupport type
            return false;
        }

        offset += MAX_BYTE_LENGTH;
    }

    return true;
}

// unsigned integer type uint256.
bytes ContractABI::serialise(const int& _in)
{
    return serialise((s256)_in);
}

// unsigned integer type uint256.
bytes ContractABI::serialise(const u256& _in)
{
    return h256(_in).asBytes();
}

// two’s complement signed integer type int256.
bytes ContractABI::serialise(const s256& _in)
{
    return h256(_in.convert_to<u256>()).asBytes();
}

// equivalent to uint8 restricted to the values 0 and 1. For computing the function selector,
// bool is used
bytes ContractABI::serialise(const bool& _in)
{
    return h256(u256(_in ? 1 : 0)).asBytes();
}

// equivalent to uint160, except for the assumed interpretation and language typing. For
// computing the function selector, address is used.
// bool is used.
bytes ContractABI::serialise(const Address& _in)
{
    return bytes(12, 0) + _in.asBytes();
}

// binary type of 32 bytes
bytes ContractABI::serialise(const string32& _in)
{
    bytes ret(32, 0);
    bytesConstRef((byte const*)_in.data(), 32).populate(bytesRef(&ret));
    return ret;
}

bytes ContractABI::serialise(const bytes& _in)
{
    bytes ret;
    ret = h256(u256(_in.size())).asBytes();
    ret.resize(ret.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&ret).cropped(32));
    return ret;
}

// dynamic sized unicode string assumed to be UTF-8 encoded.
bytes ContractABI::serialise(const std::string& _in)
{
    bytes ret;
    ret = h256(u256(_in.size())).asBytes();
    ret.resize(ret.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&ret).cropped(32));
    return ret;
}

void ContractABI::deserialise(s256& out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 u = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
    if (u > u256("0x8fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"))
    {
        auto r =
            (dev::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") - u) +
            1;
        out = s256("-" + r.str());
    }
    else
    {
        out = u.convert_to<s256>();
    }
}

void ContractABI::deserialise(u256& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    _out = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
}

void ContractABI::deserialise(bool& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 ret = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
    _out = ret > 0 ? true : false;
}

void ContractABI::deserialise(Address& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    data.cropped(_offset + MAX_BYTE_LENGTH - 20, 20).populate(_out.ref());
}

void ContractABI::deserialise(string32& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    data.cropped(_offset, MAX_BYTE_LENGTH).populate(bytesRef((byte*)_out.data(), MAX_BYTE_LENGTH));
}

void ContractABI::deserialise(std::string& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 len = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
    validOffset(_offset + MAX_BYTE_LENGTH + (std::size_t)len - 1);
    auto result = data.cropped(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len));
    _out.assign((const char*)result.data(), result.size());
}

void ContractABI::deserialise(bytes& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 len = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
    validOffset(_offset + MAX_BYTE_LENGTH + (std::size_t)len - 1);
    _out = data.cropped(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len)).toBytes();
}