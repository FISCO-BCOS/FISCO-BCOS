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

#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/SHA3.h>



namespace dev
{
namespace eth
{

class ContractABI {
public:
	/*
	template <unsigned N>
	void serialise(FixedHash<N> const& _t) {
		static_assert(N <= 32, "Cannot serialise hash > 32 bytes.");
		static_assert(N > 0, "Cannot serialise zero-length hash.");

		fixedItems.push_back(bytes(32 - N, 0) + _t.asBytes());
	}
	*/

	void serialise(u256 const& _t) {
		fixedItems.push_back(h256(_t).asBytes());
	}
	
	void serialise(byte const& _t){
		fixedItems.push_back(bytes(31, 0) + bytes(_t));
	}

	void serialise(u160 const& _t) {
		fixedItems.push_back(bytes(12, 0) + h160(_t).asBytes());
	}

	void serialise(string32 const& _t) {
		bytes ret;
		bytesConstRef((byte const*) _t.data(), 32).populate(bytesRef(&ret));

		fixedItems.push_back(ret);
	}

	void serialise(std::string const& _t) {
		bytes ret = h256(u256(_t.size())).asBytes();
		ret.resize(ret.size() + (_t.size() + 31) / 32 * 32);
		bytesConstRef(&_t).populate(bytesRef(&ret).cropped(32));

		DynamicItem item;
		item.data = ret;
		item.offset = fixedItems.size();

		serialise(u256(0));

		dynamicItems.push_back(item);
	}

	inline void abiInAux() {
		return;
	}

	template<class T, class ... U> void abiInAux(T const& _t, U const& ... _u) {
		serialise(_t);
		abiInAux(_u ...);
	}

	template<class ... T> bytes abiIn(std::string _id, T const& ... _t) {
		fixedItems.clear();
		dynamicItems.clear();

		abiInAux(_t ...);

		size_t offset = fixedItems.size() * 32;

		for(auto it = dynamicItems.begin(); it != dynamicItems.end(); ++it) {
			fixedItems[it->offset] = h256(offset).asBytes();

			offset += it->data.size();
		}

		bytes fixed;
		for(auto it = fixedItems.begin(); it != fixedItems.end(); ++it) fixed += *it;

		bytes dynamic;
		for(auto it = dynamicItems.begin(); it != dynamicItems.end(); ++it) dynamic += it->data;

		return sha3(_id).ref().cropped(0, 4).toBytes() + fixed + dynamic;
	}

	template<unsigned N>
	void deserialise(FixedHash<N>& out) {
		static_assert(N <= 32, "Parameter sizes must be at most 32 bytes.");
		data.cropped(getOffset() + 32 - N, N).populate(out.ref());
	}

	void deserialise(u256& out) {

		out = fromBigEndian<u256>(data.cropped(getOffset(), 32));
	}

	void deserialise(u160& out) {
		out = fromBigEndian<u160>(data.cropped(getOffset() + 12, 20));
	}

	void deserialise(byte& out) {
		out = fromBigEndian<byte>(data.cropped(getOffset() + 31, 1));
	}

	void deserialise(string32& out) {
		data.cropped(0, 32).populate(bytesRef((byte*) out.data(), 32));
	}

	void deserialise(bool& out) {
		u256 ret = fromBigEndian<u256>(data.cropped(0, 32));
		out = ret > 0 ? true : false;
	}

	void deserialise(std::string& out) {
		u256 offset = fromBigEndian<u256>(data.cropped(getOffset(), 32));
		u256 len = fromBigEndian<u256>(data.cropped(static_cast<size_t>(offset), 32));

		auto stringData = data.cropped(static_cast<size_t>(offset) + 32, static_cast<size_t>(len));

		out.assign((const char*)stringData.data(), stringData.size());

		
	}

	inline void abiOutAux() {
		return;
	}

	template<class T, class ... U> void abiOutAux(T& _t, U& ... _u) {
		deserialise(_t);
		++decodeOffset;

		abiOutAux(_u ...);
	}

	template<class ... T> void abiOut(bytesConstRef _data, T& ... _t) {
		data = _data;
		decodeOffset = 0;

		abiOutAux(_t ...);
	}

private:
	struct DynamicItem {
		bytes data;
		size_t offset;
	};

	std::vector<bytes> fixedItems;
	std::vector<DynamicItem> dynamicItems;

	size_t getOffset() {
		return decodeOffset * 32;
	}

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

template <class T> struct ABISerialiser {};
template <unsigned N> struct ABISerialiser<FixedHash<N>> { static bytes serialise(FixedHash<N> const& _t) { static_assert(N <= 32, "Cannot serialise hash > 32 bytes."); static_assert(N > 0, "Cannot serialise zero-length hash."); return bytes(32 - N, 0) + _t.asBytes(); } };
template <> struct ABISerialiser<u256> { static bytes serialise(u256 const& _t) { return h256(_t).asBytes(); } };
template <> struct ABISerialiser<u160> { static bytes serialise(u160 const& _t) { return bytes(12, 0) + h160(_t).asBytes(); } };
template <> struct ABISerialiser<string32> { static bytes serialise(string32 const& _t) { bytes ret; bytesConstRef((byte const*)_t.data(), 32).populate(bytesRef(&ret)); return ret; } };
template <> struct ABISerialiser<std::string>
{
	static bytes serialise(std::string const& _t)
	{
		bytes ret = h256(u256(32)).asBytes() + h256(u256(_t.size())).asBytes();
		ret.resize(ret.size() + (_t.size() + 31) / 32 * 32);
		bytesConstRef(&_t).populate(bytesRef(&ret).cropped(64));
		return ret;
	}
};
inline bytes abiInAux() { return {}; }
template <class T, class ... U> bytes abiInAux(T const& _t, U const& ... _u)
{
	return ABISerialiser<T>::serialise(_t) + abiInAux(_u ...);
}

template <class ... T> bytes abiIn(std::string _id, T const& ... _t)
{
	return sha3(_id).ref().cropped(0, 4).toBytes() + abiInAux(_t ...);
}

template <class T> struct ABIDeserialiser {};
template <unsigned N> struct ABIDeserialiser<FixedHash<N>> { static FixedHash<N> deserialise(bytesConstRef& io_t) { static_assert(N <= 32, "Parameter sizes must be at most 32 bytes."); FixedHash<N> ret; io_t.cropped(32 - N, N).populate(ret.ref()); io_t = io_t.cropped(32); return ret; } };
template <> struct ABIDeserialiser<u256> { static u256 deserialise(bytesConstRef& io_t) { u256 ret = fromBigEndian<u256>(io_t.cropped(0, 32)); io_t = io_t.cropped(32); return ret; } };
template <> struct ABIDeserialiser<u160> { static u160 deserialise(bytesConstRef& io_t) { u160 ret = fromBigEndian<u160>(io_t.cropped(12, 20)); io_t = io_t.cropped(32); return ret; } };
template <> struct ABIDeserialiser<string32> { static string32 deserialise(bytesConstRef& io_t) { string32 ret; io_t.cropped(0, 32).populate(bytesRef((byte*)ret.data(), 32)); io_t = io_t.cropped(32); return ret; } };

template <> struct ABIDeserialiser<bool> { 
	static bool deserialise(bytesConstRef& io_t) 
	{ 
		u256 ret = fromBigEndian<u256>(io_t.cropped(0, 32)); 
		io_t = io_t.cropped(32); 
		return ret>0?true:false; 
	} 
};

template <> struct ABIDeserialiser<std::string>
{
	static std::string deserialise(bytesConstRef& io_t)
	{
		unsigned o = (uint16_t)u256(h256(io_t.cropped(0, 32)));
		unsigned s = (uint16_t)u256(h256(io_t.cropped(o, 32)));
		std::string ret;
		ret.resize(s);
		io_t.cropped(o + 32, s).populate(bytesRef((byte*)ret.data(), s));
		io_t = io_t.cropped(32);
		return ret;
	}
};

template <class T> T abiOut(bytes const& _data)
{
	bytesConstRef o(&_data);
	return ABIDeserialiser<T>::deserialise(o);
}

template <class T> T abiOut(bytesConstRef& _data)
{
	return ABIDeserialiser<T>::deserialise(_data);
}

}
}
