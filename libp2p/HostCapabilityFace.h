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
#include "SessionFace.h"
namespace dev
{
namespace p2p
{
class HostCapabilityFace
{
public:
    virtual ~HostCapabilityFace() = default;

    virtual bool isConnected(h512 const& _id) = 0;

    virtual std::string name() const = 0;
    virtual u256 version() const = 0;
    virtual unsigned messageCount() const = 0;
    virtual std::shared_ptr<Capability> newPeerCapability(std::shared_ptr<SessionFace> const& _s,
        unsigned _idOffset, CapDesc const& _cap, uint16_t _capID) = 0;

    virtual void onStarting() = 0;
    virtual void onStopping() = 0;
};
}  // namespace p2p

}  // namespace dev
