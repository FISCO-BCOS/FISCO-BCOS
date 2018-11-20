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
/** @file HostCapability.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#pragma once

#include <memory>
#include "Peer.h"
#include "Common.h"
#include "Session.h"

namespace dev
{

namespace p2p
{

class HostCapabilityFace
{
	friend class HostApi;
	friend class Host;
	friend class HostSSL;
	template <class T> friend class HostCapability;
	friend class Capability;
	friend class Session;

public:
	HostCapabilityFace() {}
	virtual ~HostCapabilityFace() {}

	HostApi* host() const { return m_host.get(); }

	std::vector<std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>>> peerSessions() const;
	std::vector<std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>>> peerSessions(u256 const& _version) const;
	bool isConnected(h512 const& _id);

protected:
	virtual std::string name() const = 0;
	virtual u256 version() const = 0;
	CapDesc capDesc() const { return std::make_pair(name(), version()); }
	virtual unsigned messageCount() const = 0;
	virtual std::shared_ptr<Capability> newPeerCapability(std::shared_ptr<SessionFace> const& _s, unsigned _idOffset, CapDesc const& _cap, uint16_t _capID) = 0;

	virtual void onStarting() {}
	virtual void onStopping() {}

private:
	std::shared_ptr<p2p::HostApi> m_host = nullptr;
};

template<class PeerCap>
class HostCapability: public HostCapabilityFace
{
public:
	HostCapability() {}
	virtual ~HostCapability() {}

	static std::string staticName() { return PeerCap::name(); }
	static u256 staticVersion() { return PeerCap::version(); }
	static unsigned staticMessageCount() { return PeerCap::messageCount(); }

protected:
	virtual std::string name() const { return PeerCap::name(); }
	virtual u256 version() const { return PeerCap::version(); }
	virtual unsigned messageCount() const { return PeerCap::messageCount(); }

	virtual std::shared_ptr<Capability> newPeerCapability(std::shared_ptr<SessionFace> const& _s, unsigned _idOffset, CapDesc const& _cap, uint16_t _capID)
	{
		_s->registerFraming(_capID);
		auto p = std::make_shared<PeerCap>(_s, this, _idOffset, _cap, _capID);
		_s->registerCapability(_cap, p);
		return p;
	}
};

}

}
