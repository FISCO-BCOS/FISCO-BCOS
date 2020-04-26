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

#pragma once

#include "libdevcrypto/CryptoInterface.h"
#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <boost/algorithm/string.hpp>

namespace dev
{
namespace eth
{
namespace abi
{
// check if T type of uint256, int256, bool, string, bytes32
template <class T>
struct ABIElementType : std::false_type
{
};

// string
template <>
struct ABIElementType<std::string> : std::true_type
{
};

// uint256
template <>
struct ABIElementType<u256> : std::true_type
{
};

// int256
template <>
struct ABIElementType<s256> : std::true_type
{
};

// bool
template <>
struct ABIElementType<bool> : std::true_type
{
};

// byte32
template <>
struct ABIElementType<string32> : std::true_type
{
};

// check if T type of string
template <class T>
struct ABIStringType : std::false_type
{
};

template <>
struct ABIStringType<std::string> : std::true_type
{
};

// check if type of static array
template <class T>
struct ABIStaticArray : std::false_type
{
};

// stringN type => bytesN
template <std::size_t N>
struct ABIStaticArray<std::array<char, N>> : std::false_type
{
};

// a fixed-length array of N elements of type T.
template <class T, std::size_t N>
struct ABIStaticArray<std::array<T, N>> : std::true_type
{
};

// check if type of dynamic array
template <class T>
struct ABIDynamicArray : std::false_type
{
};

// a fixed-length array of N elements of type T.
template <class T>
struct ABIDynamicArray<std::vector<T>> : std::true_type
{
};

// Definition: The following types are called “dynamic”:
// bytes
// string
// T[] for any T
// T[k] for any dynamic T and any k >= 0
// (T1,...,Tk) if Ti is dynamic for some 1 <= i <= k
template <class T, class Enable = void>
struct ABIDynamicType : std::false_type
{
};

template <class T>
struct remove_dimension
{
    typedef T type;
};

template <class T, std::size_t N>
struct remove_dimension<std::array<T, N>>
{
    typedef typename remove_dimension<T>::type type;
};

template <class T>
struct ABIDynamicType<T,
    typename std::enable_if<ABIStringType<typename remove_dimension<T>::type>::value ||
                            ABIDynamicArray<typename remove_dimension<T>::type>::value>::type>
  : std::true_type
{
};

// fixed length of type, default 1 except static array type
template <class T, class Enable = void>
struct Length
{
    enum
    {
        value = 1
    };
};

// length of static array type
template <class T>
struct Length<T,
    typename std::enable_if<ABIStaticArray<T>::value && !ABIDynamicType<T>::value>::type>
{
    enum
    {
        value = std::tuple_size<T>::value * Length<typename std::tuple_element<0, T>::type>::value
    };
};

// static offset for types
template <class... Args>
struct Offset;

template <class T, class... List>
struct Offset<T, List...>
{
    enum
    {
        value = Offset<T>::value + Offset<List...>::value
    };
};

template <>
struct Offset<>
{
    enum
    {
        value = 0
    };
};

template <class T>
struct Offset<T>
{
    enum
    {
        value = Length<T>::value
    };
};

/**
 * @brief Class for Solidity ABI
 * @by octopuswang
 *
 * Class for serialise and deserialise c++ object in Solidity ABI format.
 * @ref https://solidity.readthedocs.io/en/develop/abi-spec.html
 */
class ContractABI
{
private:
    static const int MAX_BYTE_LENGTH = 32;
    // encode or decode offset
    std::size_t offset{0};
    // encode temp bytes
    bytes fixed;
    bytes dynamic;

    // decode data
    bytesConstRef data;

    size_t getOffset() { return offset; }
    // check if offset valid and std::length_error will be throw
    void validOffset(std::size_t _offset)
    {
        if (_offset >= data.size())
        {
            std::stringstream ss;
            ss << " deserialise failed, invalid offset , offset is " << _offset << " , length is "
               << data.size() << " , data is " << toHex(data);

            throw std::length_error(ss.str().c_str());
        }
    }

    template <class T>
    std::string toString(const T& _t)
    {
        std::stringstream ss;
        ss << _t;
        return ss.str();
    }

public:
    template <class T>
    bytes serialise(const T& _t)
    {  // unsupport type
        (void)_t;
        static_assert(ABIElementType<T>::value, "ABI not support type.");
        return bytes{};
    }
    // unsigned integer type int.
    bytes serialise(const int& _in);

    // unsigned integer type uint256.
    bytes serialise(const u256& _in);

    // two’s complement signed integer type int256.
    bytes serialise(const s256& _in);

    // equivalent to uint8 restricted to the values 0 and 1. For computing the function selector,
    // bool is used
    bytes serialise(const bool& _in);

    // equivalent to uint160, except for the assumed interpretation and language typing. For
    // computing the function selector, address is used.
    // bool is used.
    bytes serialise(const Address& _in);

    // binary type of 32 bytes
    bytes serialise(const string32& _in);

    // dynamic sized unicode string assumed to be UTF-8 encoded.
    bytes serialise(const std::string& _in);

    // static array
    template <class T, std::size_t N>
    bytes serialise(const std::array<T, N>& _in);
    // dynamic array
    template <class T>
    bytes serialise(const std::vector<T>& _in);

    template <class T>
    void deserialise(const T& _t, std::size_t _offset)
    {  // unsupport type
        (void)_t;
        (void)_offset;
        static_assert(ABIElementType<T>::value, "ABI not support type.");
    }

    void deserialise(s256& out, std::size_t _offset);

    void deserialise(u256& _out, std::size_t _offset);

    void deserialise(bool& _out, std::size_t _offset);

    void deserialise(Address& _out, std::size_t _offset);

    void deserialise(string32& _out, std::size_t _offset);

    void deserialise(std::string& _out, std::size_t _offset);

    // static array
    template <class T, std::size_t N>
    void deserialise(std::array<T, N>& _out, std::size_t _offset);
    // dynamic array
    template <class T>
    void deserialise(std::vector<T>& _out, std::size_t _offset);

private:
    inline void abiInAux() { return; }

    template <class T, class... U>
    void abiInAux(T const& _t, U const&... _u)
    {
        bytes out = serialise(_t);

        if (ABIDynamicType<T>::value)
        {  // dynamic type
            dynamic += out;
            fixed += serialise((u256)offset);
            offset += out.size();
        }
        else
        {  // static type
            fixed += out;
        }

        abiInAux(_u...);
    }

    void abiOutAux() { return; }

    template <class T, class... U>
    void abiOutAux(T& _t, U&... _u)
    {
        std::size_t _offset = offset;
        // dynamic type, offset position
        if (ABIDynamicType<T>::value)
        {
            u256 dynamicOffset;
            deserialise(dynamicOffset, offset);
            _offset = static_cast<std::size_t>(dynamicOffset);
        }

        deserialise(_t, _offset);
        // update offset
        offset = offset + Offset<T>::value * MAX_BYTE_LENGTH;
        // decode next element
        abiOutAux(_u...);
    }

public:
    template <class... T>
    bool abiOut(bytesConstRef _data, T&... _t)
    {
        data = _data;
        offset = 0;
        try
        {
            abiOutAux(_t...);
            return true;
        }
        catch (...)
        {  // error occur
            return false;
        }
    }

    template <class... T>
    bool abiOutHex(const std::string& _data, T&... _t)
    {
        auto data = fromHex(_data);
        return abiOut(bytesConstRef(&data), _t...);
    }

    bool abiOutByFuncSelector(bytesConstRef _data, const std::vector<std::string>& _allTypes,
        std::vector<std::string>& _out);

    template <class... T>
    bytes abiIn(const std::string& _sig, T const&... _t)
    {
        offset = Offset<T...>::value * MAX_BYTE_LENGTH;
        fixed.clear();
        dynamic.clear();

        abiInAux(_t...);

        return _sig.empty() ? fixed + dynamic :
                              crypto::Hash(_sig).ref().cropped(0, 4).toBytes() + fixed + dynamic;
    }

    template <class... T>
    std::string abiInHex(const std::string& _sig, T const&... _t)
    {
        return toHex(abiIn(_sig, _t...));
    }
};

// a fixed-length array of elements of the given type.
template <class T, std::size_t N>
bytes ContractABI::serialise(const std::array<T, N>& _in)
{
    bytes offset_bytes;
    bytes content;

    auto length = N * MAX_BYTE_LENGTH;

    for (const auto& e : _in)
    {
        bytes out = serialise(e);
        content += out;
        if (ABIDynamicType<T>::value)
        {  // dynamic
            offset_bytes += serialise(static_cast<u256>(length));
            length += out.size();
        }
    }

    return offset_bytes + content;
}

// a variable-length array of elements of the given type.
template <class T>
bytes ContractABI::serialise(const std::vector<T>& _in)
{
    bytes offset_bytes;
    bytes content;

    auto length = _in.size() * MAX_BYTE_LENGTH;

    offset_bytes += serialise(static_cast<u256>(_in.size()));
    for (const auto& t : _in)
    {
        bytes out = serialise(t);
        content += out;
        if (ABIDynamicType<T>::value)
        {  // dynamic
            offset_bytes += serialise(static_cast<u256>(length));
            length += out.size();
        }
    }

    return offset_bytes + content;
}

template <class T, std::size_t N>
void ContractABI::deserialise(std::array<T, N>& _out, std::size_t _offset)
{
    for (std::size_t u = 0; u < N; ++u)
    {
        auto thisOffset = _offset;

        if (ABIDynamicType<T>::value)
        {  // dynamic type
            // N element offset
            u256 length;
            deserialise(length, _offset + u * Offset<T>::value * MAX_BYTE_LENGTH);
            thisOffset = thisOffset + static_cast<std::size_t>(length);
        }
        else
        {
            thisOffset = _offset + u * Offset<T>::value * MAX_BYTE_LENGTH;
        }
        deserialise(_out[u], thisOffset);
    }
}

template <class T>
void ContractABI::deserialise(std::vector<T>& _out, std::size_t _offset)
{
    u256 length;
    // vector length
    deserialise(length, _offset);
    _offset += MAX_BYTE_LENGTH;
    _out.resize(static_cast<std::size_t>(length));

    for (std::size_t u = 0; u < static_cast<std::size_t>(length); ++u)
    {
        std::size_t thisOffset = _offset;

        if (ABIDynamicType<T>::value)
        {  // dynamic type
            // N element offset
            u256 thisEleOffset;
            deserialise(thisEleOffset, _offset + u * Offset<T>::value * MAX_BYTE_LENGTH);
            thisOffset += static_cast<std::size_t>(thisEleOffset);
        }
        else
        {
            thisOffset = _offset + u * Offset<T>::value * MAX_BYTE_LENGTH;
        }
        deserialise(_out[u], thisOffset);
    }
}

}  // namespace abi

using ContractABI = abi::ContractABI;

inline string32 toString32(std::string const& _s)
{
    string32 ret;
    for (unsigned i = 0; i < 32; ++i)
        ret[i] = i < _s.size() ? _s[i] : 0;
    return ret;
}

}  // namespace eth
}  // namespace dev
