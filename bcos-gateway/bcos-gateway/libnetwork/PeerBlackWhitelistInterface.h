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
#include <vector>

using NodeID = bcos::h512;

namespace bcos
{

namespace gateway
{

class PeerBlackWhitelistInterface
{
public:
    using Ptr = std::shared_ptr<PeerBlackWhitelistInterface>;

public:
    PeerBlackWhitelistInterface(std::set<std::string> const& _strList, bool _enable = false);
    PeerBlackWhitelistInterface(std::set<NodeID> const& _nodeList, bool _enable);
    virtual ~PeerBlackWhitelistInterface() = default;

    virtual bool has(NodeID _peer) const;
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
    std::set<NodeID> m_peerList;
};

}  // namespace gateway

}  // namespace bcos