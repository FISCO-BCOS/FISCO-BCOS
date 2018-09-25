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
 * @author Gav Wood <i@gavwood.com>, jimmyshi
 * @date 2018
 */

#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/SHA3.h>
#include <iostream>

#include <libdevcore/easylog.h>

namespace dev
{
namespace eth
{
class ContractABI
{
public:
    void serialise(u256 const& _t) { fixedItems.push_back(h256(_t).asBytes()); }

    void serialise(byte const& _t)
    {
        fixedItems.push_back(bytes(31, 0) + bytes(1, _t));
    }  // Fix bug here, declare size of bytes

    void serialise(u160 const& _t) { fixedItems.push_back(bytes(12, 0) + h160(_t).asBytes()); }

    void serialise(string32 const& _t)
    {
        bytes ret(32, 0);  // Fix bugs, declare size here
        bytesConstRef((byte const*)_t.data(), 32).populate(bytesRef(&ret));

        fixedItems.push_back(ret);
    }


    void serialise(string64 const& _t)
    {
        bytes ret(64);
        bytesConstRef((byte const*)_t.data(), 64).populate(bytesRef(&ret));

        fixedItems.push_back(ret);
    }

    void serialise(std::string const& _t)
    {
        bytes ret = h256(u256(_t.size())).asBytes();
        ret.resize(ret.size() + (_t.size() + 31) / 32 * 32);
        bytesConstRef(&_t).populate(bytesRef(&ret).cropped(32));

        DynamicItem item;
        item.data = ret;
        item.offset = fixedItems.size();

        serialise(u256(0));  // Fill in a zero before filling the offset

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
            fixedItems[it->offset] = h256(offset).asBytes();  // Fill the offset

            offset += it->data.size();
        }

        bytes fixed;
        for (auto it = fixedItems.begin(); it != fixedItems.end(); ++it)
            fixed += *it;

        bytes dynamic;
        for (auto it = dynamicItems.begin(); it != dynamicItems.end(); ++it)
            dynamic += it->data;

        return sha3(_id).ref().cropped(0, 4).toBytes() + fixed + dynamic;
    }

    template <unsigned N>
    void deserialise(FixedHash<N>& out)
    {
        static_assert(N <= 32, "Parameter sizes must be at most 32 bytes.");
        data.cropped(getOffset() + 32 - N, N).populate(out.ref());
    }

    void deserialise(u256& out) { out = fromBigEndian<u256>(data.cropped(getOffset(), 32)); }

    void deserialise(u160& out) { out = fromBigEndian<u160>(data.cropped(getOffset() + 12, 20)); }

    void deserialise(byte& out) { out = fromBigEndian<byte>(data.cropped(getOffset() + 31, 1)); }

    void deserialise(string32& out)
    {
        // fix bug here, starting offset modify from 0 to getOffset()
        data.cropped(getOffset(), 32).populate(bytesRef((byte*)out.data(), 32));
    }

    void deserialise(bool& out)
    {
        u256 ret = fromBigEndian<u256>(data.cropped(0, 32));
        out = ret > 0 ? true : false;
    }

    void deserialise(std::string& out)
    {
        u256 offset = fromBigEndian<u256>(data.cropped(getOffset(), 32));
        u256 len = fromBigEndian<u256>(data.cropped(static_cast<size_t>(offset), 32));

        auto stringData = data.cropped(static_cast<size_t>(offset) + 32, static_cast<size_t>(len));

        out.assign((const char*)stringData.data(), stringData.size());
    }

    inline void abiOutAux() { return; }

    template <class T, class... U>
    void abiOutAux(T& _t, U&... _u)
    {
        deserialise(_t);
        ++decodeOffset;

        abiOutAux(_u...);
    }

    template <class... T>
    void abiOut(bytesConstRef _data, T&... _t)
    {
        data = _data;
        decodeOffset = 0;

        abiOutAux(_t...);
    }

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
