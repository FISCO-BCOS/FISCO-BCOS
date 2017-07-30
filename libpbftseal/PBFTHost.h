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
#include <libethcore/Common.h>
#include <libethereum/CommonNet.h>
#include <libp2p/Capability.h>
#include <libp2p/HostCapability.h>
#include "Common.h"
#include "PBFTPeer.h"

namespace dev
{
namespace eth
{

class PBFTHost: public p2p::HostCapability<PBFTPeer>
{
public:
	typedef std::function<void(unsigned, std::shared_ptr<p2p::Capability>, RLP const&)> MsgHandler;

	PBFTHost(MsgHandler h): m_msg_handler(h) {}
	virtual ~PBFTHost() {}

	void onMsg(unsigned _id, std::shared_ptr<p2p::Capability> _peer, RLP const& _r) {
		m_msg_handler(_id, _peer, _r);
	}

	void foreachPeer(std::function<bool(std::shared_ptr<PBFTPeer>)> const& _f) const;

private:
	MsgHandler m_msg_handler;
};

}
}
