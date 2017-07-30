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

#include <libp2p/Session.h>
#include <libp2p/Host.h>
#include "PBFTPeer.h"
#include "PBFTHost.h"
using namespace std;
using namespace dev;
using namespace eth;
using namespace p2p;

PBFTPeer::PBFTPeer(std::shared_ptr<SessionFace> _s, HostCapabilityFace* _h, unsigned _i, CapDesc const& _cap, uint16_t _capID):
	Capability(_s, _h, _i, _capID),
	m_peerCapabilityVersion(_cap.second)
{
	//session()->addNote("manners", isRude() ? "RUDE" : "nice");
}

PBFTPeer::~PBFTPeer()
{
}

bool PBFTPeer::interpret(unsigned _id, RLP const& _r)
{
	//clog(PBFTTrace) << "PBFTPeer::interpret id=" << _id;
	dynamic_cast<PBFTHost&>(*hostCapability()).onMsg(_id, dynamic_pointer_cast<PBFTPeer>(shared_from_this()), _r);
	return true;
}