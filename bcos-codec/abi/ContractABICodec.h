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

#pragma once
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/string.hpp>
#include <utility>
#include <vector>

namespace bcos
{
namespace codec
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

template <>
struct ABIElementType<std::uint8_t> : std::true_type
{
};

template <>
struct ABIElementType<std::uint32_t> : std::true_type
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

template <typename T1, typename T2>
struct ABIElementType<std::pair<T1, T2>>
{
    static bool constexpr value = ABIElementType<T1>::value && ABIElementType<T2>::value;
};

template <typename T>
struct ABIElementType<std::vector<T>>
{
    static bool constexpr value = ABIElementType<T>::value;
};

template <typename T, typename... U>
struct ABIElementType<std::tuple<T, U...>>
{
    static bool constexpr value =
        ABIElementType<T>::value && ABIElementType<std::tuple<U...>>::value;
};

template <typename T>
struct ABIElementType<std::tuple<T>>
{
    static bool constexpr value = ABIElementType<T>::value;
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

template <class T>
struct ABIDynamicTuple : std::false_type
{
};

template <class... T>
struct ABIDynamicTuple<std::tuple<T...>> : std::true_type
{
};

template <class T>
struct ABIDynamicTuple<std::tuple<T>> : std::true_type
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
                            ABIDynamicArray<typename remove_dimension<T>::type>::value ||
                            ABIDynamicTuple<typename remove_dimension<T>::type>::value>::type>
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
 * Class for serialise and deserialize c++ object in Solidity ABI format.
 * @ref https://solidity.readthedocs.io/en/develop/abi-spec.html
 */
class ContractABICodec
{
public:
    explicit ContractABICodec(bcos::crypto::Hash::Ptr _hashImpl) : m_hashImpl(_hashImpl) {}

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

    bytes serialise(const bytes& _in);

    // dynamic sized unicode string assumed to be UTF-8 encoded.
    bytes serialise(const std::string& _in);

    // static array
    template <class T, std::size_t N>
    bytes serialise(const std::array<T, N>& _in);
    // dynamic array
    template <class T>
    bytes serialise(const std::vector<T>& _in);

    // dynamic tuple
    template <class... T>
    bytes serialise(const std::tuple<T...>& _in);

    template <class T>
    void deserialize(const T& _t, std::size_t _offset)
    {  // unsupport type
        (void)_t;
        (void)_offset;
        static_assert(ABIElementType<T>::value, "ABI not support type.");
    }

    void deserialize(s256& out, std::size_t _offset);

    void deserialize(u256& _out, std::size_t _offset);

    void deserialize(bool& _out, std::size_t _offset);

    void deserialize(Address& _out, std::size_t _offset);

    void deserialize(string32& _out, std::size_t _offset);

    void deserialize(std::string& _out, std::size_t _offset);
    void deserialize(bytes& _out, std::size_t _offset);

    // static array
    template <class T, std::size_t N>
    void deserialize(std::array<T, N>& _out, std::size_t _offset);
    // dynamic array
    template <class T>
    void deserialize(std::vector<T>& _out, std::size_t _offset);

    template <class... T>
    void deserialize(std::tuple<T...>& out, std::size_t _offset);

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
    static const int MAX_BYTE_LENGTH = 32;
    // encode or decode offset
    std::size_t offset{0};
    // encode temp bytes
    bytes fixed;
    bytes dynamic;

    // decode data
    bytesConstRef data;

private:
    size_t getOffset() { return offset; }
    // check if offset valid and std::length_error will be throw
    void validOffset(std::size_t _offset)
    {
        if (_offset >= data.size())
        {
            std::stringstream ss;
            ss << " deserialize failed, invalid offset , offset is " << _offset << " , length is "
               << data.size() << " , data is " << *toHexString(data);

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
            deserialize(dynamicOffset, offset);
            _offset = static_cast<std::size_t>(dynamicOffset);
        }

        deserialize(_t, _offset);
        // update offset
        offset = offset + Offset<T>::value * MAX_BYTE_LENGTH;
        // decode next element
        abiOutAux(_u...);
    }

    template <class F, class... Ts, std::size_t... Is>
    void traverseTuple(std::tuple<Ts...>& tuple, F func, std::index_sequence<Is...>)
    {
        return (void(func(std::get<Is>(tuple))), ...);
    }

    template <class F, class... Ts>
    void traverseTuple(std::tuple<Ts...>& tuple, F func)
    {
        traverseTuple(tuple, func, std::make_index_sequence<sizeof...(Ts)>());
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
        auto dataFromHex = *fromHexString(_data);
        return abiOut(bytesConstRef(&dataFromHex), _t...);
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

        return _sig.empty() ?
                   fixed + dynamic :
                   m_hashImpl->hash(_sig).ref().getCroppedData(0, 4).toBytes() + fixed + dynamic;
    }

    template <class... T>
    std::string abiInHex(const std::string& _sig, T const&... _t)
    {
        return *toHexString(abiIn(_sig, _t...));
    }
};

// a fixed-length array of elements of the given type.
template <class T, std::size_t N>
bytes ContractABICodec::serialise(const std::array<T, N>& _in)
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
bytes ContractABICodec::serialise(const std::vector<T>& _in)
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

template <class... T>
bytes ContractABICodec::serialise(const std::tuple<T...>& _in)
{
    bytes offsetBytes;
    bytes dynamicContent;
    auto tupleSize = std::tuple_size<typename std::remove_reference<decltype(_in)>::type>::value;
    auto length = tupleSize * MAX_BYTE_LENGTH;

    traverseTuple(const_cast<std::tuple<T...>&>(_in), [&](auto& _tupleItem) {
        bytes out = serialise(_tupleItem);

        if (ABIDynamicType<typename std::remove_const<
                typename std::remove_reference<decltype(_tupleItem)>::type>::type>::value)
        {
            // dynamic
            dynamicContent += out;
            offsetBytes += serialise(static_cast<u256>(length));
            length += out.size();
        }
        else
        {
            // static
            offsetBytes += out;
        }
    });
    return offsetBytes + dynamicContent;
}

template <class T, std::size_t N>
void ContractABICodec::deserialize(std::array<T, N>& _out, std::size_t _offset)
{
    for (std::size_t u = 0; u < N; ++u)
    {
        auto thisOffset = _offset;

        if (ABIDynamicType<T>::value)
        {  // dynamic type
            // N element offset
            u256 length;
            deserialize(length, _offset + u * Offset<T>::value * MAX_BYTE_LENGTH);
            thisOffset = thisOffset + static_cast<std::size_t>(length);
        }
        else
        {
            thisOffset = _offset + u * Offset<T>::value * MAX_BYTE_LENGTH;
        }
        deserialize(_out[u], thisOffset);
    }
}

template <class T>
void ContractABICodec::deserialize(std::vector<T>& _out, std::size_t _offset)
{
    u256 length;
    // vector length
    deserialize(length, _offset);
    _offset += MAX_BYTE_LENGTH;
    _out.resize(static_cast<std::size_t>(length));

    for (std::size_t u = 0; u < static_cast<std::size_t>(length); ++u)
    {
        std::size_t thisOffset = _offset;

        if (ABIDynamicType<T>::value)
        {  // dynamic type
            // N element offset
            u256 thisEleOffset;
            deserialize(thisEleOffset, _offset + u * Offset<T>::value * MAX_BYTE_LENGTH);
            thisOffset += static_cast<std::size_t>(thisEleOffset);
        }
        else
        {
            thisOffset = _offset + u * Offset<T>::value * MAX_BYTE_LENGTH;
        }
        deserialize(_out[u], thisOffset);
    }
}

template <class... T>
void ContractABICodec::deserialize(std::tuple<T...>& _out, std::size_t _offset)
{
    std::size_t localOffset = _offset;
    std::size_t tupleOffset = 0;
    traverseTuple(_out, [&](auto& _tupleItem) {
        if (ABIDynamicType<typename std::remove_const<
                typename std::remove_reference<decltype(_tupleItem)>::type>::type>::value)
        {
            // dynamic
            u256 dynamicOffset;
            deserialize(dynamicOffset, _offset + tupleOffset);
            localOffset = _offset + static_cast<std::size_t>(dynamicOffset);
            deserialize(_tupleItem, localOffset);
        }
        else
        {
            // static
            deserialize(_tupleItem, _offset + tupleOffset);
        }
        tupleOffset +=
            Offset<typename std::remove_const<
                typename std::remove_reference<decltype(_tupleItem)>::type>::type>::value *
            MAX_BYTE_LENGTH;
    });
}
}  // namespace abi

inline string32 toString32(std::string const& _s)
{
    string32 ret;
    for (unsigned i = 0; i < 32; ++i)
        ret[i] = i < _s.size() ? _s[i] : 0;
    return ret;
}

inline string32 toString32(bcos::h256 const& _hashData)
{
    string32 ret;
    for (unsigned i = 0; i < 32; i++)
    {
        ret[i] = _hashData[i];
    }
    return ret;
}

inline bcos::h256 fromString32(string32 const& _str)
{
    bcos::h256 hashData;
    for (unsigned i = 0; i < 32; i++)
    {
        hashData[i] = _str[i];
    }
    return hashData;
}
}  // namespace codec
}  // namespace bcos
