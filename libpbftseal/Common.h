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


#pragma once

#include <libdevcore/concurrent_queue.h>
#include <libdevcore/Log.h>
#include <libdevcore/RLP.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Exceptions.h>

namespace dev
{

namespace eth
{

enum PBFTPacketType : byte
{
	PrepareReqPacket = 0x00,
	SignReqPacket = 0x01,
	ViewChangeReqPacket = 0x02,

	PBFTPacketCount
};

// for pbft
struct PBFTMsgPacket {
	u256 node_idx;
	h512 node_id;
	unsigned packet_id;
	bytes data; // rlp data
	u256 timestamp;

	PBFTMsgPacket(): node_idx(h256(0)), node_id(h512(0)), packet_id(0), timestamp(utcTime()) {}
	PBFTMsgPacket(u256 _idx, h512 _id, unsigned _pid, bytesConstRef _data)
		: node_idx(_idx), node_id(_id), packet_id(_pid), data(_data.toBytes()), timestamp(utcTime()) {}
};
using PBFTMsgQueue = dev::concurrent_queue<PBFTMsgPacket>;

struct PBFTMsg {
	u256 height = Invalid256;
	u256 view = Invalid256;
	u256 idx = Invalid256;
	u256 timestamp = Invalid256;
	h256 block_hash;
	Signature sig;

	virtual void streamRLPFields(RLPStream& _s) const {
		_s << height << view << idx << timestamp << block_hash << sig.asBytes();
	}
	virtual void  populate(RLP const& _rlp) {
		int field = 0;
		try	{
			height = _rlp[field = 0].toInt<u256>();
			view = _rlp[field = 1].toInt<u256>();
			idx = _rlp[field = 2].toInt<u256>();
			timestamp = _rlp[field = 3].toInt<u256>();
			block_hash = _rlp[field = 4].toHash<h256>(RLP::VeryStrict);
			sig = dev::Signature(_rlp[field = 5].toBytesConstRef());
		} catch (Exception const& _e)	{
			_e << errinfo_name("invalid msg format") << BadFieldError(field, toHex(_rlp[field].data().toBytes()));
			throw;
		}
	}

	void clear() {
		height = Invalid256;
		view = Invalid256;
		idx = Invalid256;
		timestamp = Invalid256;
		block_hash = h256();
		sig = Signature();
	}
};

struct PrepareReq : public PBFTMsg {
	bytes block;
	virtual void streamRLPFields(RLPStream& _s) const {	PBFTMsg::streamRLPFields(_s); _s << block; }
	virtual void populate(RLP const& _rlp) {
		PBFTMsg::populate(_rlp);
		int field = 0;
		try	{
			block = _rlp[field = 6].toBytes();
		} catch (Exception const& _e)	{
			_e << errinfo_name("invalid msg format") << BadFieldError(field, toHex(_rlp[field].data().toBytes()));
			throw;
		}
	}
};
struct SignReq : public PBFTMsg {};
struct ViewChangeReq : public PBFTMsg {};

}
}
