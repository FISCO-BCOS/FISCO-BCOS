/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Session.h
 * @brief An established RLPx session: framed, encrypted message exchange.
 *        Synchronous facade over a bcos-network async session — the public
 *        API (sendMessage/recvMessage/enableCompression) is unchanged from
 *        the blocking-socket implementation it replaces.
 * @date 2026/8/18
 */
#pragma once

#include "FrameDecoder.h"
#include "Framing.h"
#include "MessageCodec.h"
#include <bcos-network/Host.h>
#include <bcos-network/Session.h>
#include <bcos-network/Socket.h>

namespace bcos::devp2p::rlpx
{
using NetHost = bcos::network::Host<RlpxFrameDecoder, bcos::network::PlainSocket>;
using NetSession = bcos::network::BasicSession<RlpxFrameDecoder, bcos::network::PlainSocket>;
using NetHostPtr = std::shared_ptr<NetHost>;
using NetSessionPtr = std::shared_ptr<NetSession>;

// Sends/receives framed + encrypted messages over an established bcos-network
// session. Holds the Host alongside the session so the network outlives the
// connection setup scope. The ingress cipher lives in the net session's
// decoder; the egress cipher lives here (two FramingCipher instances built
// from the same KeyMaterial, one direction each, so their stream states never
// interfere).
//
// HOST OWNERSHIP (`_ownsHost`): a CLIENT session owns its Host — `RlpxClient::connect()` builds
// one Host per connection and hands it over, so close() must stop() it or the Host (and the
// coroutine frames holding it) would outlive the caller's last reference. A SERVER session only
// BORROWS the Host: `RlpxServer` keeps one Host for its whole lifetime and hands it to every
// accepted session. Calling stop() from a borrowed session closed the server's accept channel on
// the first disconnect, so the server could serve exactly one client (a later accept() threw
// HostStopped and std::call_once prevented a restart). Borrowers therefore leave stop() to the
// owner.
class Session
{
public:
    Session(NetHostPtr _host, NetSessionPtr _netSession, FramingCipher _egressCipher,
        bool _ownsHost = true);
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;
    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;
    ~Session();

    void sendMessage(Message const& _message);
    // I/O and framing (MAC) failures still throw; a malformed frame payload is
    // reported as an RlpError value by the codec. A 15s read timeout throws a
    // std::runtime_error containing "timed out" and the session stays alive
    // (TxGossipService matches that substring to recognize a healthy idle peer).
    bcos::codec::rlp::RlpResult<Message> recvMessage();

    // True when this session owns (and therefore stops) the Host. See the class comment.
    bool ownsHost() const { return m_ownsHost; }

    void enableCompression() { m_codec.enableCompression(); }

    // Explicit teardown (drop the connection, stop the host). Idempotent; the
    // destructor calls it too.
    void close();

private:
    NetHostPtr m_host;
    bool m_ownsHost = true;
    NetSessionPtr m_netSession;
    FramingCipher m_egressCipher;  // only encryptFrame is used on this instance
    MessageCodec m_codec;
};
}  // namespace bcos::devp2p::rlpx
