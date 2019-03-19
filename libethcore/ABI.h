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
class AbiFunction
{
private:
    std::string strFuncsignature;
    std::string strSelector;
    std::string strName;
    std::vector<std::string> allParamsTypes;

public:
    inline bool doParser(const std::string& _strSig = "")
    {
        std::string tempSig = _strSig;
        if (tempSig.empty())
        {
            tempSig = strFuncsignature;
        }
        auto i0 = tempSig.find("(");
        auto i1 = tempSig.find(")");
        // eg: function set(string)
        if ((i0 != std::string::npos) && (i1 != std::string::npos) && (i1 > i0))
        {
            allParamsTypes.clear();
            std::string strFuncName = tempSig.substr(0, i0);
            std::string strParams = tempSig.substr(i0 + 1, i1 - i0 - 1);

            std::vector<std::string> allParams;
            boost::split(allParams, strParams, boost::is_any_of(","));

            strName = strFuncName;
            // allParamsTypes = allParams;

            tempSig = strName + "(";

            std::for_each(allParams.begin(), allParams.end(), [&tempSig, this](std::string& type) {
                type.erase(0, type.find_first_not_of(" "));
                type.erase(type.find_last_not_of(" ") + 1);
                if (!type.empty())
                {
                    this->allParamsTypes.push_back(type);
                    tempSig += type;
                    tempSig += ",";
                }
            });

            if (',' == tempSig.back())
            {
                tempSig.back() = ')';
            }
            else
            {
                tempSig += ")";
            }

            strFuncsignature = tempSig;
            strSelector = "0x" + dev::sha3(strFuncsignature).hex().substr(0, 8);

            return true;
        }

        return false;
    }

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
