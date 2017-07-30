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


#include "PBFTHost.h"
using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

void PBFTHost::foreachPeer(std::function<bool(std::shared_ptr<PBFTPeer>)> const& _f) const
{
	//order peers by protocol, rating, connection age
	auto sessions = peerSessions();
	auto sessionLess = [](std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>> const & _left, std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>> const & _right)
	{ return _left.first->rating() == _right.first->rating() ? _left.first->connectionTime() < _right.first->connectionTime() : _left.first->rating() > _right.first->rating(); };

	std::sort(sessions.begin(), sessions.end(), sessionLess);
	for (auto s : sessions)
		if (!_f(capabilityFromSession<PBFTPeer>(*s.first)))
			return;

	static const unsigned c_oldProtocolVersion = 62;
	sessions = peerSessions(c_oldProtocolVersion); //TODO: remove once v61+ is common
	std::sort(sessions.begin(), sessions.end(), sessionLess);
	for (auto s : sessions)
		if (!_f(capabilityFromSession<PBFTPeer>(*s.first, c_oldProtocolVersion)))
			return;
}


