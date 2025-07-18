/** @file PeerBlackWhitelistInterface.h
 * PeerBlackWhitelistInterface of peer connection
 * @author jimmyshi
 * @date: 2019-08-06
 */
#pragma once

#include <bcos-utilities/FixedBytes.h>

#include <memory>
#include <set>
#include <string>

using P2PNodeID = bcos::h512;

namespace bcos::gateway
{

class PeerBlackWhitelistInterface
{
public:
    using Ptr = std::shared_ptr<PeerBlackWhitelistInterface>;

    PeerBlackWhitelistInterface(std::set<std::string> const& _strList, bool _enable = false);
    PeerBlackWhitelistInterface(std::set<P2PNodeID> const& _nodeList, bool _enable);
    virtual ~PeerBlackWhitelistInterface() = default;

    virtual bool has(P2PNodeID _peer) const;
    virtual bool has(const std::string& _peer) const;
    virtual bool hasValueWhenDisable() const = 0;
    virtual void setEnable(bool _enable) { m_enable = _enable; }
    virtual bool enable() const { return m_enable; }
    virtual std::string dump(bool _isAbridged = false);
    virtual size_t size() { return m_peerList.size(); }
    virtual void update(std::set<std::string> const& _strList, bool _enable);

protected:
    mutable bcos::Mutex x_peerList;
    bool m_enable{false};
    std::set<P2PNodeID> m_peerList;
};

}  // namespace bcos::gateway
