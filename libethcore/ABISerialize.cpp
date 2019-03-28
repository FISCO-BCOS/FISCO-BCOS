#include "ABISerialize.h"
#include <libdevcore/FixedHash.h>
#include <boost/regex.hpp>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::eth::abi;

const int ABISerialize::MAX_BYTE_LENGTH;

const std::string Meta::strBool{"bool"};
const std::string Meta::strBytes{"bytes"};
const std::string Meta::strString{"string"};
const std::string Meta::strAddr{"address"};

const std::set<std::string> Meta::setUint{"uint", "uint8", "uint16", "uint24", "uint32", "uint40",
    "uint48", "uint56", "uint64", "uint72", "uint80", "uint88", "uint96", "uint108", "uint116",
    "uint124", "uint132", "uint140", "uint148", "uint156", "uint164", "uint172", "uint180",
    "uint188", "uint196", "uint204", "uint212", "uint220", "uint228", "uint236", "uint244",
    "uint252"};

const std::set<std::string> Meta::setInt{"int", "int8", "int16", "int24", "int32", "int40", "int48",
    "int56", "int64", "int72", "int80", "int88", "int96", "int108", "int116", "int124", "int132",
    "int140", "int148", "int156", "int164", "int172", "int180", "int188", "int196", "int204",
    "int212", "int220", "int228", "int236", "int244", "int252"};

const std::set<std::string> Meta::setByteN{"bytes1", "bytes2", "bytes3", "bytes4", "bytes5",
    "bytes6", "bytes7", "bytes8", "bytes9", "bytes10", "bytes11", "bytes12", "bytes13", "bytes14",
    "bytes15", "bytes16", "bytes17", "bytes18", "bytes19", "bytes20", "bytes21", "bytes22",
    "bytes23", "bytes24", "bytes25", "bytes26", "bytes27", "bytes28", "bytes29", "bytes30",
    "bytes31", "bytes32"};

ABIType::ABIType(const std::string& _str)
{
    std::string _strType = _str;
    // eg: int[1][2][][3]
    // trim blank character
    Meta::trim(_strType);
    // int
    std::string _strMetaType = _strType.substr(0, _strType.find('['));
    Meta::TYPE t = Meta::getTypeByStr(_strMetaType);
    // invalid solidity abi string
    if (t != Meta::TYPE::INVALID)
    {
        std::vector<std::size_t> result;

        boost::regex regex("\\[[0-9]*\\]");
        boost::sregex_token_iterator iter(_strType.begin(), _strType.end(), regex, 0);
        boost::sregex_token_iterator end;

        for (; iter != end; ++iter)
        {
            boost::regex reg("\\[[0-9]{1,}\\]");
            std::string temp = *iter;
            std::size_t size = strtoul(temp.c_str() + 1, NULL, 10);
            result.push_back(size);
        }

        type = t;
        extents = result;
        strType = _strType;
        strMetaType = _strMetaType;
    }
}

bool ABIType::dynamic()
{
    // string or bytes
    if (type == Meta::TYPE::STRING || type == Meta::TYPE::BYTES)
    {
        return true;
    }

    // dynamic array
    auto length = rank();
    for (std::size_t i = 0; i < length; ++i)
    {
        if (extent(i) == 0)
        {
            return true;
        }
    }

    return false;
}

//
bool ABIType::removeExtent()
{
    auto length = rank();
    if (length > 1)
    {
        extents.resize(length - 1);
        return true;
    }

    return false;
}
/*
bytes ABIFunc::getSelector()
{
    return dev::sha3(strFuncSignature).toHex().substr(0, 8);
}*/

std::vector<std::string> ABIFunc::getParamsTypes() const
{
    std::vector<std::string> result;
    for (auto it = allParamsTypes.begin(); it != allParamsTypes.end(); ++it)
    {
        result.push_back(it->getType());
    }

    return result;
}

bool ABIFunc::parser(const std::string& _sig)
{
    auto i0 = _sig.find("(");
    auto i1 = _sig.find(")");
    // eg: function set(string)
    if ((i0 != std::string::npos) && (i1 != std::string::npos) && (i1 > i0))
    {
        std::string strFuncName = _sig.substr(0, i0);
        Meta::trim(strFuncName);
        std::string strType = _sig.substr(i0 + 1, i1 - i0 - 1);

        std::string sig = strFuncName + "(";

        std::vector<std::string> types;
        boost::split(types, strType, boost::is_any_of(","));

        for (std::string& type : types)
        {
            Meta::trim(type);
            if (!type.empty())
            {
                sig += type;
                sig += ",";

                ABIType at(type);
                auto Ok = at.valid();
                if (Ok)
                {
                    allParamsTypes.push_back(at);
                    continue;
                }
                // invalid format
                return false;
            }
        }

        if (',' == sig.back())
        {
            sig.back() = ')';
        }
        else
        {
            sig += ")";
        }

        strName = strFuncName;

        strFuncSignature = sig;

        return true;
    }

    return false;
}

// unsigned integer type uint256.
bytes ABISerialize::serialise(const int& _in)
{
    return serialise((s256)_in);
}

// unsigned integer type uint256.
bytes ABISerialize::serialise(const u256& _in)
{
    return h256(_in).asBytes();
}

// two’s complement signed integer type int256.
bytes ABISerialize::serialise(const s256& _in)
{
    return h256(_in.convert_to<u256>()).asBytes();
}

// equivalent to uint8 restricted to the values 0 and 1. For computing the function selector,
// bool is used
bytes ABISerialize::serialise(const bool& _in)
{
    return h256(u256(_in ? 1 : 0)).asBytes();
}

// equivalent to uint160, except for the assumed interpretation and language typing. For
// computing the function selector, address is used.
// bool is used.
bytes ABISerialize::serialise(const Address& _in)
{
    return bytes(12, 0) + _in.asBytes();
}

// binary type of 32 bytes
bytes ABISerialize::serialise(const string32& _in)
{
    bytes ret(32, 0);
    bytesConstRef((byte const*)_in.data(), 32).populate(bytesRef(&ret));
    return ret;
}

// dynamic sized unicode string assumed to be UTF-8 encoded.
bytes ABISerialize::serialise(const std::string& _in)
{
    bytes ret;
    ret = h256(u256(_in.size())).asBytes();
    ret.resize(ret.size() + (_in.size() + 31) / MAX_BYTE_LENGTH * MAX_BYTE_LENGTH);
    bytesConstRef(&_in).populate(bytesRef(&ret).cropped(32));
    return ret;
}

void ABISerialize::deserialise(s256& out, std::size_t _offset)
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

void ABISerialize::deserialise(u256& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);
    _out = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
}

void ABISerialize::deserialise(bool& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    u256 ret = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
    _out = ret > 0 ? true : false;
}

void ABISerialize::deserialise(Address& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    data.cropped(_offset + MAX_BYTE_LENGTH - 20, 20).populate(_out.ref());
}

void ABISerialize::deserialise(string32& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    data.cropped(_offset, MAX_BYTE_LENGTH).populate(bytesRef((byte*)_out.data(), MAX_BYTE_LENGTH));
}

void ABISerialize::deserialise(std::string& _out, std::size_t _offset)
{
    validOffset(_offset + MAX_BYTE_LENGTH - 1);

    // u256 offset = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));
    u256 len = fromBigEndian<u256>(data.cropped(_offset, MAX_BYTE_LENGTH));

    auto result = data.cropped(_offset + MAX_BYTE_LENGTH, static_cast<size_t>(len));

    _out.assign((const char*)result.data(), result.size());
}
