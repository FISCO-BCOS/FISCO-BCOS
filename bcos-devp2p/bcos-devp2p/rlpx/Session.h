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
// session. Owns the Host together with the session so the network outlives the
// connection setup scope. The ingress cipher lives in the net session's
// decoder; the egress cipher lives here (two FramingCipher instances built
// from the same KeyMaterial, one direction each, so their stream states never
// interfere).
class Session
{
public:
    Session(NetHostPtr _host, NetSessionPtr _netSession, FramingCipher _egressCipher);
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

    void enableCompression() { m_codec.enableCompression(); }

    // Explicit teardown (drop the connection, stop the host). Idempotent; the
    // destructor calls it too.
    void close();

private:
    NetHostPtr m_host;
    NetSessionPtr m_netSession;
    FramingCipher m_egressCipher;  // only encryptFrame is used on this instance
    MessageCodec m_codec;
};
}  // namespace bcos::devp2p::rlpx
