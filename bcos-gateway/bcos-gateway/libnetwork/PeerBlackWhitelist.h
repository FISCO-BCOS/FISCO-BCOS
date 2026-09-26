/** @file PeerBlackWhitelist.h
 * Peer blacklist/whitelist of peer connection
 * @author jimmyshi
 * @date: 2019-08-06
 */
#pragma once

#include "bcos-utilities/FixedBytes.h"

#include <atomic>
#include <set>
#include <string>

using P2PNodeID = bcos::h512;

namespace bcos::gateway
{

class PeerBlackWhitelist
{
public:
    enum class Type
    {
        Blacklist,
        Whitelist
    };

    PeerBlackWhitelist(Type _type, std::set<std::string> const& _strList, bool _enable = false);
    PeerBlackWhitelist(Type _type, std::set<P2PNodeID> const& _nodeList, bool _enable);

    PeerBlackWhitelist(PeerBlackWhitelist&& _other) noexcept;
    PeerBlackWhitelist& operator=(PeerBlackWhitelist&& _other) noexcept;
    PeerBlackWhitelist(const PeerBlackWhitelist&) = delete;
    PeerBlackWhitelist& operator=(const PeerBlackWhitelist&) = delete;

    bool has(P2PNodeID _peer) const;
    bool has(const std::string& _peer) const;
    void setEnable(bool _enable);
    bool enable() const;
    std::string dump(bool _isAbridged = false);
    size_t size() const;
    void update(std::set<std::string> const& _strList, bool _enable);

private:
    mutable bcos::Mutex x_peerList;
    Type m_type;
    // atomic: has() runs on any IO thread during the TLS handshake while update()/setEnable()
    // run on the SIGUSR1 reload path
    std::atomic<bool> m_enable{false};
    std::set<P2PNodeID> m_peerList;
};

}  // namespace bcos::gateway
