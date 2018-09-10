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

#include "Common.h"
#include "Host.h"
#include "HostCapabilityFace.h"
#include "Peer.h"
#include "Session.h"
#include <memory>

namespace dev
{
namespace p2p
{
template <class PeerCap>
class HostCapability : public HostCapabilityFace
{
public:
    explicit HostCapability(Host const& _host) : m_host(_host) {}
    virtual ~HostCapability() {}

    static std::string staticName() { return PeerCap::name(); }
    static u256 staticVersion() { return PeerCap::version(); }
    static unsigned staticMessageCount() { return PeerCap::messageCount(); }
    virtual std::string name() const { return PeerCap::name(); }
    virtual u256 version() const { return PeerCap::version(); }
    virtual unsigned messageCount() const { return PeerCap::messageCount(); }

    virtual std::shared_ptr<Capability> newPeerCapability(std::shared_ptr<SessionFace> const& _s,
        unsigned _idOffset, CapDesc const& _cap, uint16_t _capID)
    {
        auto p = std::make_shared<PeerCap>(_s, this, _idOffset, _cap, _capID);
        _s->registerCapability(_cap, p);
        return p;
    }

protected:
    std::vector<std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>>> peerSessions() const
    {
        return peerSessions(version());
    }

    std::vector<std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>>> peerSessions(
        u256 const& _version) const
    {
        RecursiveGuard l(m_host.x_sessions);
        std::vector<std::pair<std::shared_ptr<SessionFace>, std::shared_ptr<Peer>>> ret;
        for (auto const& i : m_host.m_sessions)
            if (std::shared_ptr<SessionFace> s = i.second.lock())
                if (s->capabilities().count(std::make_pair(name(), _version)))
                    ret.push_back(make_pair(s, s->peer()));
        return ret;
    }

    CapDesc capDesc() const { return std::make_pair(name(), version()); }
    bool isConnected(h512 const& _id)
    {
        RecursiveGuard l(m_host.x_sessions);
        for (auto const& i : m_host.m_sessions)
        {
            if (std::shared_ptr<SessionFace> s = i.second.lock())
            {
                if (s->id() == _id && s->isConnected())
                {
                    return true;
                }
            }
        }
        return false;
    }

private:
    Host const& m_host;
};

}  // namespace p2p

}  // namespace dev
