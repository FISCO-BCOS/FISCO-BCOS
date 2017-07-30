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

#include <libdevcore/RLP.h>
#include <libdevcore/Guards.h>
#include <libethcore/Common.h>
#include <libp2p/Capability.h>
#include <libp2p/HostCapability.h>
#include "Common.h"

namespace dev
{

namespace eth
{
class PBFTPeer : public p2p::Capability
{
	friend class PBFT;
public:
	PBFTPeer(std::shared_ptr<p2p::SessionFace> _s, p2p::HostCapabilityFace* _h, unsigned _i, p2p::CapDesc const& _cap, uint16_t _capID);

	/// Basic destructor.
	virtual ~PBFTPeer();

	/// What is our name?
	static std::string name() { return "pbft"; }
	/// What is our version?
	static u256 version() { return c_protocolVersion; }
	/// How many message types do we have?
	static unsigned messageCount() { return PBFTPacketCount; }

	p2p::NodeID id() const { return session()->id(); }

protected:
	virtual bool interpret(unsigned _id, RLP const& _r);

private:
	u256 const m_peerCapabilityVersion;

	Mutex x_knownPrepare;
	QueueSet<std::string> m_knownPrepare;
	Mutex x_knownSign;
	QueueSet<std::string> m_knownSign;
	Mutex x_knownViewChange;
	QueueSet<std::string> m_knownViewChange;
};

}
}
