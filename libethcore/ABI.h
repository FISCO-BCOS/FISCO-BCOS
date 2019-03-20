/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ABI.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <boost/algorithm/string.hpp>

namespace dev
{
namespace eth
{
namespace abi
{
static const int MAX_BIT_LENGTH = (256);
static const int MAX_BYTE_LENGTH = (MAX_BYTE_LENGTH / 8);

template <class... Args>
struct Length;

template <>
struct Length<>
{
    enum
    {
        value = 0
    };
};

template <class Last>
struct Length<Last>
{
    enum
    {
        value = 1
    };
};

template <class Array, int N>
struct Length<Array[N]>
{
    enum
    {
        value = N * Length<Array>::value
    };
};

template <class First, class... Rest>
struct Length<First, Rest...>
{
    enum
    {
        value = Length<First>::value + Length<Rest...>::value
    };
};

struct AbiTool
{
public:
    // unsigned integer type of M bits, 0 < M <= 256, M % 8 == 0. e.g. uint32, uint8, uint256.
    template <std::size_t N>
    bytes serialise(const uintN<N>& _u)
    {
        static_assert((N > 0) && (N <= 256) && (N % 8 == 0),
            "Parameter sizes must in (0, 256] and divided by 8.");
        return h256(_u).asBytes();
    }

    // two’s complement signed integer type of M bits, 0 < M <= 256, M % 8 == 0.
    template <std::size_t N>
    bytes serialise(const intN<N>& _i)
    {
        static_assert((N > 0) && (N <= 256) && (N % 8 == 0),
            "Parameter sizes must in (0, 256] and divided by 8.");
        return h256((uintN<N>)_i).asBytes();
    }

    // equivalent to uint8 restricted to the values 0 and 1. For computing the function selector,
    // bool is used.
    bytes serialise(bool _b) { return h256(u256(_b ? 1 : 0)).asBytes(); }

    // equivalent to uint160, except for the assumed interpretation and language typing. For
    // computing the function selector, address is used.
    bytes serialise(const Address& _addr) { return bytes(12, 0) + _addr.asBytes(); }

    // binary type of M bytes, 0 < M <= 32.
    template <std::size_t N>
    bytes serialise(const stringN<N>& _s)
    {
    static_assert( (N > 0) && (N <= 32) ), "Parameter sizes must in (0, 32].");
    bytes ret(32, 0);
    bytesConstRef((byte const*)_s.data(), 32).populate(bytesRef(&ret));
    return ret;
    }

    // dynamic sized unicode string assumed to be UTF-8 encoded.
    bytes serialise(const std::string& _s)
    {
        bytes ret;
        ret = h256(u256(_s.size())).asBytes();
        ret.resize(ret.size() + (_s.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
        bytesConstRef(&_s).populate(bytesRef(&ret).cropped(32));
        return ret;
    }

    // a fixed-length array of M elements, M >= 0, of the given type.
    template <class T, std::size_t N>
    bytes serialise(const T (&_t)[N])
    {
        bytes ret;
        for (std::size_t index = 0; index < N; index++)
        {
            ret += serialise(_t[index]);
        }
        return ret;
    }

    // a variable-length array of elements of the given type.
    template <class T>
    bytes serialise(const std::vector<T>& _vt)
    {
        bytes ret;
        ret += serialise(u256(_vt.size()));
        for (const auto& _v : _vt)
        {
            ret += serialise(_v);
        }
        return ret;
    }
};

}  // namespace abi

class TypeMeta
{
public:
    enum class TYPE
    {
        INVALID,  // invalid
        BOOL,     // bool
        INT,      // int8  ~ int256
        UINT,     // uint8 ~ uint256
        ADDR,     // address
        BYTES,    // bytes
        STRING,   // string
        FIXED,    // fixed, unsupport now
        UNFIXED   // unfixed, unsupport now
    };

public:
    static void trim(std::string& _str)
    {
        _str.erase(0, _str.find_first_not_of(" "));
        _str.erase(_str.find_last_not_of(" ") + 1);
    }

    static TYPE getTypeByStr(const std::string& _strType)
    {
        TYPE type = TYPE::INVALID;
        if (_strType == strBool)
        {
            type = TYPE::BOOL;
        }
        else if (_strType == strAddr)
        {
            type = TYPE::ADDR;
        }
        else if (_strType == strString)
        {
            type = TYPE::STRING;
        }
        else if (_strType == strBytes)
        {
            type = TYPE::BYTES;
        }
        else if (setUint.find(_strType) != setUint.end())
        {
            type = TYPE::UINT;
        }
        else if (setInt.find(_strType) != setInt.end())
        {
            type = TYPE::INT;
        }
        else if (setByteN.find(_strType) != setByteN.end())
        {
            type = TYPE::BYTES;
        }

        return type;
    }

private:
    // uint<M>: unsigned integer type of M bits, 0 < M <= 256, M % 8 == 0. e.g. uint32, uint8,
    // uint256.
    static const std::set<std::string> setUint;

    // int<M>: two’s complement signed integer type of M bits, 0 < M <= 256, M % 8 == 0.
    static const std::set<std::string> setInt;

    // bytes<M>: binary type of M bytes, 0 < M <= 32.
    static const std::set<std::string> setByteN;

    // bool: equivalent to uint8 restricted to the values 0 and 1. For computing the function
    // selector, bool is used.
    static const std::string strBool;
    // bytes: dynamic sized byte sequence.
    static const std::string strBytes;
    // bytes: dynamic sized byte sequence.
    static const std::string strString;
    // address: equivalent to uint160, except for the assumed interpretation and language typing.
    // For computing the function selector, address is used.
    static const std::string strAddr;
};

class AbiType
{
public:
    AbiType(const std::string _str) : strType(_str) {}

public:
    // the number of dimensions of T or zero
    std::size_t rank() { return extents.size(); }
    // obtains the size of an array type along a specified dimension
    std::size_t extent(std::size_t index) { return index > rank() ? 0 : extents[index - 1]; }
    // parser strType
    bool doParser();

private:
    bool valid() { return type != TypeMeta::TYPE::INVALID; }
    void setType(TypeMeta::TYPE _type) { type = _type; }
    void setExtents(std::vector<std::size_t> _extents) { _extents = extents; }

    std::vector<std::size_t> extents;
    TypeMeta::TYPE type{TypeMeta::TYPE::INVALID};
    std::string strType;
};

class AbiFunction
{
private:
    std::string strFuncsignature;
    std::string strSelector;
    std::string strName;
    std::vector<std::string> allParamsTypes;
    std::vector<AbiType> allInputTypes;

public:
    bool doParser(const std::string& _strSig = "");

public:
    inline std::string getSignature() const { return strFuncsignature; }
    inline std::vector<std::string> getParamsTypes() const { return allParamsTypes; }
    inline std::string getSelector() const { return strSelector; }
    inline std::string getFuncName() const { return strName; }
    inline void setSignature(const std::string& _sig) { strFuncsignature = _sig; }
};

class ContractABI
{
public:
    void serialise(s256 const& _t) { fixedItems.push_back(h256(_t.convert_to<u256>()).asBytes()); }
    void serialise(u256 const& _t) { fixedItems.push_back(h256(_t).asBytes()); }

    void serialise(byte const& _t)
    {
        fixedItems.push_back(bytes(31, 0) + bytes(1, _t));
    }  // Fix bug here, declare size of bytes

    void serialise(u160 const& _t) { fixedItems.push_back(bytes(12, 0) + h160(_t).asBytes()); }

    void serialise(string32 const& _t)
    {
        bytes ret(32, 0);
        bytesConstRef((byte const*)_t.data(), 32).populate(bytesRef(&ret));

        fixedItems.push_back(ret);
    }

    void serialise(string64 const& _t)
    {
        bytes ret1(32, 0);
        bytesConstRef((byte const*)_t.data(), 32).populate(bytesRef(&ret1));

        bytes ret2(32, 0);
        bytesConstRef((byte const*)_t.data() + 32, 32).populate(bytesRef(&ret2));

        fixedItems.push_back(ret1);
        fixedItems.push_back(ret2);
    }

    void serialise(std::string const& _t)
    {
        bytes ret = h256(u256(_t.size())).asBytes();
        ret.resize(ret.size() + (_t.size() + 31) / 32 * 32);
        bytesConstRef(&_t).populate(bytesRef(&ret).cropped(32));

        DynamicItem item;
        item.data = ret;
        item.offset = fixedItems.size();

        serialise(u256(0));

        dynamicItems.push_back(item);
    }

    inline void abiInAux() { return; }

    template <class T, class... U>
    void abiInAux(T const& _t, U const&... _u)
    {
        serialise(_t);
        abiInAux(_u...);
    }

    template <class... T>
    bytes abiIn(std::string _id, T const&... _t)
    {
        fixedItems.clear();
        dynamicItems.clear();

        abiInAux(_t...);

        size_t offset = fixedItems.size() * 32;

        for (auto it = dynamicItems.begin(); it != dynamicItems.end(); ++it)
        {
            fixedItems[it->offset] = h256(offset).asBytes();

            offset += it->data.size();
        }

        bytes fixed;
        for (auto it = fixedItems.begin(); it != fixedItems.end(); ++it)
            fixed += *it;

        bytes dynamic;
        for (auto it = dynamicItems.begin(); it != dynamicItems.end(); ++it)
            dynamic += it->data;

        if (_id.empty())
        {
            return fixed + dynamic;
        }
        else
        {
            return sha3(_id).ref().cropped(0, 4).toBytes() + fixed + dynamic;
        }
    }

    template <unsigned N>
    size_t deserialise(FixedHash<N>& out)
    {
        static_assert(N <= 32, "Parameter sizes must be at most 32 bytes.");
        data.cropped(getOffset() + 32 - N, N).populate(out.ref());
        return 1;
    }

    size_t deserialise(u256& out)
    {
        out = fromBigEndian<u256>(data.cropped(getOffset(), 32));
        return 1;
    }

    size_t deserialise(s256& out)
    {
        u256 u = fromBigEndian<u256>(data.cropped(getOffset(), 32));
        if (u > u256("0x8fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"))
        {
            auto r =
                (dev::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") -
                    u) +
                1;
            out = s256("-" + r.str());
        }
        else
        {
            out = u.convert_to<s256>();
        }
        return 1;
    }

    size_t deserialise(u160& out)
    {
        out = fromBigEndian<u160>(data.cropped(getOffset() + 12, 20));
        return 1;
    }

    size_t deserialise(byte& out)
    {
        out = fromBigEndian<byte>(data.cropped(getOffset() + 31, 1));
        return 1;
    }

    size_t deserialise(string32& out)
    {
        data.cropped(getOffset(), 32).populate(bytesRef((byte*)out.data(), 32));
        return 1;
    }

    size_t deserialise(string64& out)
    {
        data.cropped(getOffset(), 64).populate(bytesRef((byte*)out.data(), 64));
        return 2;
    }

    size_t deserialise(bool& out)
    {
        u256 ret = fromBigEndian<u256>(data.cropped(0, 32));
        out = ret > 0 ? true : false;
        return 1;
    }

    size_t deserialise(std::string& out)
    {
        u256 offset = fromBigEndian<u256>(data.cropped(getOffset(), 32));
        u256 len = fromBigEndian<u256>(data.cropped(static_cast<size_t>(offset), 32));

        auto stringData = data.cropped(static_cast<size_t>(offset) + 32, static_cast<size_t>(len));

        out.assign((const char*)stringData.data(), stringData.size());
        return 1;
    }

    inline void abiOutAux() { return; }

    template <class T, class... U>
    void abiOutAux(T& _t, U&... _u)
    {
        size_t offset = deserialise(_t);
        decodeOffset += offset;

        abiOutAux(_u...);
    }

    template <class... T>
    void abiOut(bytesConstRef _data, T&... _t)
    {
        data = _data;
        decodeOffset = 0;

        abiOutAux(_t...);
    }

    bool abiOutByFuncSelector(bytesConstRef _data, const std::vector<std::string>& _allTypes,
        std::vector<std::string>& _out);

private:
    struct DynamicItem
    {
        bytes data;
        size_t offset;
    };

    std::vector<bytes> fixedItems;
    std::vector<DynamicItem> dynamicItems;

    size_t getOffset() { return decodeOffset * 32; }

    bytesConstRef data;
    size_t decodeOffset = 0;
};


inline string32 toString32(std::string const& _s)
{
    string32 ret;
    for (unsigned i = 0; i < 32; ++i)
        ret[i] = i < _s.size() ? _s[i] : 0;
    return ret;
}
}  // namespace eth
}  // namespace dev
