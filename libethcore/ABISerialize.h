#pragma once

#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libdevcrypto/Hash.h>
#include <boost/algorithm/string.hpp>


namespace dev
{
namespace eth
{
namespace abi
{
class Meta
{
public:
    enum class TYPE
    {
        INVALID,  // invalid
        BOOL,     // bool
        INT,      // int8  ~ int256
        UINT,     // uint8 ~ uint256
        BYTESN,   // bytesN
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
            type = TYPE::BYTESN;
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

class ABIType
{
public:
    ABIType(const std::string& _str);

public:
    // the number of dimensions of T or zero
    std::size_t rank() { return extents.size(); }
    // obtains the size of an array type along a specified dimension
    std::size_t extent(std::size_t index) { return index > rank() ? 0 : extents[index - 1]; }
    bool removeExtent();
    bool dynamic();
    inline bool valid() { return type != Meta::TYPE::INVALID; }
    inline std::string getType() const { return strType; }

private:
    std::vector<std::size_t> extents;
    Meta::TYPE type{Meta::TYPE::INVALID};
    std::string strMetaType;
    std::string strType;
};

class ABIFunc
{
private:
    std::string strName;
    std::string strFuncSignature;
    std::vector<ABIType> allParamsTypes;

public:
    bool parser(const std::string& _sig);

public:
    // std::string getSelector();
    std::vector<std::string> getParamsTypes() const;
    inline std::string getSignature() const { return strFuncSignature; }
    inline std::string getFuncName() const { return strName; }
};


// check if T type of uint256, int256, bool, string , bytes32
template <class T>
struct Element : std::false_type
{
};

// string
template <>
struct Element<std::string> : std::true_type
{
};

// uint256
template <>
struct Element<u256> : std::true_type
{
};

// int256
template <>
struct Element<s256> : std::true_type
{
};

// bool
template <>
struct Element<bool> : std::true_type
{
};

// byte32
template <>
struct Element<string32> : std::true_type
{
};

// a fixed-length array of N elements of type T.
template <class T, std::size_t N>
struct Element<std::array<T, N>> : std::false_type
{
};

// a variable-length array of elements of type T.
template <class T>
struct Element<std::vector<T>> : std::false_type
{
};

// check if T type of string
template <class T>
struct String : std::false_type
{
};

template <>
struct String<std::string> : std::true_type
{
};

// check if type of static array
template <class T>
struct StaticArray : std::false_type
{
};

// a fixed-length array of N elements of type T.
template <class T, std::size_t N>
struct StaticArray<std::array<T, N>> : std::true_type
{
};

// check if type of dynamic array
template <class T>
struct DynamicArray : std::false_type
{
};

// a fixed-length array of N elements of type T.
template <class T>
struct DynamicArray<std::vector<T>> : std::true_type
{
};

// check if type of dynamic
template <class T, class Enable = void>
struct Dynamic : std::false_type
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
struct Dynamic<T,
    typename std::enable_if<String<typename remove_dimension<T>::type>::value ||
                            DynamicArray<typename remove_dimension<T>::type>::value>::type>
  : std::true_type
{
};

template <class T, class Enable = void>
struct Length
{
    enum
    {
        value = 1
    };
};

template <class T>
struct Length<T, typename std::enable_if<StaticArray<T>::value && !Dynamic<T>::value>::type>
{
    enum
    {
        value = std::tuple_size<T>::value * Length<typename std::tuple_element<0, T>::type>::value
    };
};

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

template <class T>
struct Offset<T>
{
    enum
    {
        value = Length<T>::value
    };
};

class ABISerialize
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
            ss << " deserialise failed, invalid offset , offset is " << _offset
               << " , data length is " << data.size();

            throw std::length_error(ss.str().c_str());
        }
    }

public:
    template <class T>
    void deserialise(const T& _t, std::size_t _offset)
    {  // unsupport type
        (void)_t;
        (void)_offset;
        static_assert(Element<T>::value, "ABI not support type.");
    }

    void deserialise(s256& out, std::size_t _offset);

    void deserialise(u256& _out, std::size_t _offset);

    void deserialise(bool& _out, std::size_t _offset);

    void deserialise(Address& _out, std::size_t _offset);

    void deserialise(string32& _out, std::size_t _offset);

    void deserialise(std::string& _out, std::size_t _offset);

    template <class T, std::size_t N>
    void deserialise(std::array<T, N>& _out, std::size_t _offset);
    template <class T>
    void deserialise(std::vector<T>& _out, std::size_t _offset);

    void abiOutAux() { return; }

    template <class T, class... U>
    void abiOutAux(T& _t, U&... _u)
    {
        std::size_t _offset = offset;
        // dynamic type, offset position
        if (Dynamic<T>::value)
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
        abiOut(bytesConstRef(&data), _t...);
    }

public:
    template <class T>
    bytes serialise(const T& _t)
    {  // unsupport type
        (void)_t;
        static_assert(Element<T>::value, "ABI not support type.");
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

    template <class T, std::size_t N>
    bytes serialise(const std::array<T, N>& _in);
    template <class T>
    bytes serialise(const std::vector<T>& _in);

    inline void abiInAux() { return; }

    template <class T, class... U>
    void abiInAux(T const& _t, U const&... _u)
    {
        bytes out = serialise(_t);

        if (Dynamic<T>::value)
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

public:
    template <class... T>
    bytes abiIn(const std::string& _sig, T const&... _t)
    {
        offset = Offset<T...>::value * MAX_BYTE_LENGTH;
        fixed.clear();
        dynamic.clear();

        abiInAux(_t...);

        return _sig.empty() ? fixed + dynamic :
                              sha3(_sig).ref().cropped(0, 4).toBytes() + fixed + dynamic;
    }

    template <class... T>
    std::string abiInHex(const std::string& _sig, T const&... _t)
    {
        return toHex(abiIn(_sig, _t...));
    }
};

// a fixed-length array of elements of the given type.
template <class T, std::size_t N>
bytes ABISerialize::serialise(const std::array<T, N>& _in)
{
    bytes offset_bytes;
    bytes content;

    auto length = N * MAX_BYTE_LENGTH;

    for (const auto& e : _in)
    {
        bytes out = serialise(e);
        content += out;
        if (Dynamic<T>::value)
        {  // dynamic
            offset_bytes += serialise(static_cast<u256>(length));
            length += out.size();
        }
    }

    return offset_bytes + content;
}

// a variable-length array of elements of the given type.
template <class T>
bytes ABISerialize::serialise(const std::vector<T>& _in)
{
    bytes offset_bytes;
    bytes content;

    auto length = _in.size() * MAX_BYTE_LENGTH;

    offset_bytes += serialise(static_cast<u256>(_in.size()));
    for (const auto& t : _in)
    {
        bytes out = serialise(t);
        content += out;
        if (Dynamic<T>::value)
        {  // dynamic
            offset_bytes += serialise(static_cast<u256>(length));
            length += out.size();
        }
    }

    return offset_bytes + content;
}

template <class T, std::size_t N>
void ABISerialize::deserialise(std::array<T, N>& _out, std::size_t _offset)
{
    for (std::size_t u = 0; u < N; ++u)
    {
        auto thisOffset = _offset;

        if (Dynamic<T>::value)
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
void ABISerialize::deserialise(std::vector<T>& _out, std::size_t _offset)
{
    u256 length;
    // vector length
    deserialise(length, _offset);
    _offset += MAX_BYTE_LENGTH;
    _out.resize(static_cast<std::size_t>(length));

    for (std::size_t u = 0; u < static_cast<std::size_t>(length); ++u)
    {
        std::size_t thisOffset = _offset;

        if (Dynamic<T>::value)
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
}  // namespace eth
}  // namespace dev
