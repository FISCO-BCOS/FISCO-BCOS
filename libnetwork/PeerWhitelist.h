/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file PeerWhitelist.h
 * Whitelist of peer connection
 * @author jimmyshi
 * @date: 2019-08-06
 */
#pragma once
#include <libdevcore/FixedHash.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

using NodeID = dev::h512;
using NodeIDs = std::vector<NodeID>;

namespace dev
{
class PeerWhitelist : public std::enable_shared_from_this<PeerWhitelist>
{
public:
    using Ptr = std::shared_ptr<PeerWhitelist>;
    PeerWhitelist(std::vector<std::string> _strList, bool _enable = false);
    PeerWhitelist(dev::h512s const& _nodeList, bool _enable);

    bool has(NodeID _peer) const;
    bool has(const std::string& _peer) const;
    void setEnable(bool _enable) { m_enable = _enable; }
    bool enable() const { return m_enable; }
    std::string dump(bool _isAbridged = false);
    size_t size() { return m_whitelist.size(); }

private:
    bool m_enable = false;
    std::set<NodeID> m_whitelist;
};
}  // namespace dev