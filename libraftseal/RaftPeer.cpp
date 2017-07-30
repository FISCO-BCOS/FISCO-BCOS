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
/** @file RaftPeer.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <libdevcore/Log.h>
#include <libp2p/All.h>
#include "RaftHost.h"
#include "Common.h"

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;

RaftPeer::RaftPeer(std::shared_ptr<p2p::SessionFace> _s, p2p::HostCapabilityFace* _h, unsigned _i, p2p::CapDesc const& _cap, uint16_t _capID): 
	Capability(_s, _h, _i, _capID)
{
	RaftHost* host = (RaftHost*)_h;
	m_leader = host->Leader();
}

RaftPeer::~RaftPeer()
{
}

RaftHost* RaftPeer::host() const
{
	return static_cast<RaftHost*>(Capability::hostCapability());
}

bytes RaftPeer::toBytes(RLP const& _r)
{
	try{
		return _r.toBytes();
	}catch(...){
		return bytes();
	}

	return bytes();
}

bool RaftPeer::interpret(unsigned _id, RLP const& _r)
{
	bool ret = m_leader && m_leader->interpret(this, _id, _r);

	return ret;
}

bool RaftPeer::sendBytes(unsigned _t, const bytes &_b)
{
	RLPStream s;
	prep(s, _t, 1) << _b;

	sealAndSend(s);
	return true;
}

NodeID RaftPeer::id()
{
	std::shared_ptr<SessionFace> s = session();
	if(s)
		return s->id();

	return NodeID();
}


