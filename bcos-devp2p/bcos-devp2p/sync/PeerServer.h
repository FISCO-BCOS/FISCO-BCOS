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
 * @file PeerServer.h
 * @brief The eth request/response loop served to ONE connected peer: GetBlockHeaders,
 *        GetBlockBodies, GetReceipts (68/69/70 layouts), base-protocol Ping/Pong and
 *        Disconnect, BlockRangeUpdate when the local head advances. Serving limits
 *        follow go-ethereum eth/handler.go (maxHeadersServe 1024, maxBodiesServe 256,
 *        maxReceiptsServe 256, softResponseLimit 2 MiB, estHeaderRlpSize 500).
 * @date 2026/11/3
 */
#pragma once

#include "../eth/Protocol.h"
#include "../rlpx/Client.h"
#include "../rlpx/MessageCodec.h"
#include "../rlpx/Messages.h"
#include "../rlpx/Session.h"
#include "BlockSource.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace bcos::devp2p::sync
{
struct ServingLimits
{
    size_t maxHeaders{1024};
    size_t maxBodies{256};
    size_t maxReceipts{256};
    size_t softResponseBytes{2 * 1024 * 1024};
    size_t estHeaderBytes{500};
};

class PeerServer
{
public:
    explicit PeerServer(BlockSourceInterface& _source, ServingLimits _limits = {})
      : m_source(_source), m_limits(_limits)
    {}

    // What one inbound frame leads to. Reply carries the frame to send back; Ignore
    // means no reply (gossip, responses to requests we never sent, unknown codes);
    // Disconnect means the peer breached the protocol (malformed request, invalid
    // BlockRangeUpdate) and the reason must be sent before closing; PeerClosed means the
    // peer sent Disconnect.
    struct Outcome
    {
        enum class Kind
        {
            Reply,
            Ignore,
            Disconnect,
            PeerClosed,
        };
        Kind kind{Kind::Ignore};
        rlpx::Message reply;
        rlpx::DisconnectReason reason{rlpx::DisconnectReason::ProtocolBreach};
        std::string detail;
    };

    // Pure dispatch (no I/O): the response to one frame under the negotiated eth version.
    Outcome handle(uint8_t _ethVersion, rlpx::Message const& _message);

    // Why serve() returned.
    enum class ExitReason
    {
        PeerDisconnected,  // the peer sent Disconnect or closed the connection
        Shutdown,          // _running cleared or the session was shut down by stop()
        ProtocolBreach,    // we sent Disconnect for a malformed/invalid frame
        PingTimeout,       // two idle intervals passed without a Pong to our Ping
        TransportError,    // any other socket/framing failure
    };

    // Blocking loop over an established (post-Status) session: recv, handle, reply.
    // On an idle socket timeout it pings once and gives up on the second silence;
    // after every frame (and every idle wake-up) it announces a moved head with
    // BlockRangeUpdate (eth/69+). Returns when the session ends; never throws.
    ExitReason serve(rlpx::EstablishedSession& _session, std::atomic<bool> const* _running);

private:
    Outcome serveHeaders(eth::GetBlockHeadersMessage const& _request);
    Outcome serveBodies(eth::GetBlockBodiesMessage const& _request);
    Outcome serveReceipts(uint8_t _ethVersion, eth::GetReceiptsMessage const& _request);
    static Outcome reply(uint8_t _messageId, bcos::bytes _payload);
    static Outcome breach(std::string _detail);

    BlockSourceInterface& m_source;
    ServingLimits m_limits;
};
}  // namespace bcos::devp2p::sync
