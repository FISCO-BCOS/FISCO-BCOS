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
    std::string getSelector();
    std::vector<std::string> getParamsTypes() const;
    inline std::string getSignature() const { return strFuncSignature; }
    inline std::string getFuncName() const { return strName; }
};

namespace abi
{
// check if T type of uint256, int256, bool, string , bytes32
template <class T>
struct Element : std::true_type
{
};

// a fixed-length array of N elements of type T.
template <class T, std::size_t N>
struct Element<T[N]> : std::false_type
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
struct StaticArray<T[N]> : std::true_type
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
struct Dynamic<T,
    typename std::enable_if<String<typename std::remove_all_extents<T>::type>::value ||
                            DynamicArray<typename std::remove_all_extents<T>::type>::value>::type>
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
        value = std::extent<T>::value * Length<typename std::remove_extent<T>::type>::value
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

struct ABITool
{
    static const int MAX_BIT_LENGTH = (256);
    static const int MAX_BYTE_LENGTH = (MAX_BIT_LENGTH / 8);

    // unsigned integer type uint256.
    static bytes serialise(const u256& _u) { return h256(_u).asBytes(); }

    // two’s complement signed integer type int256.
    static bytes serialise(const s256& _i) { return h256(_i.convert_to<u256>()).asBytes(); }

    // equivalent to uint8 restricted to the values 0 and 1. For computing the function selector,
    // bool is used.
    static bytes serialise(bool _b) { return h256(u256(_b ? 1 : 0)).asBytes(); }

    // equivalent to uint160, except for the assumed interpretation and language typing. For
    // computing the function selector, address is used.
    static bytes serialise(const Address& _addr) { return bytes(12, 0) + _addr.asBytes(); }

    // binary type of 32 bytes
    static bytes serialise(const string32& _s)
    {
        bytes ret(32, 0);
        bytesConstRef((byte const*)_s.data(), 32).populate(bytesRef(&ret));
        return ret;
    }

    // dynamic sized unicode string assumed to be UTF-8 encoded.
    static bytes serialise(const std::string& _s)
    {
        bytes ret;
        ret = h256(u256(_s.size())).asBytes();
        ret.resize(ret.size() + (_s.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
        bytesConstRef(&_s).populate(bytesRef(&ret).cropped(32));
        return ret;
    }

    // a fixed-length array of M elements, M >= 0, of the given type.
    template <class T, std::size_t N>
    static bytes serialise(const T (&_t)[N])
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
    static bytes serialise(const std::vector<T>& _vt)
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

class ABISerialize
{
public:
    template <class... T>
    std::pair<bool, bytes> abiIn(std::string _sig, T const&... _t);

    template <class... T>
    bool abiOut(bytesConstRef _data, T&... _t);

    bool abiOut(bytesConstRef _data, const std::string& _signature, std::vector<std::string>& _out);
};

}  // namespace eth
}  // namespace dev
