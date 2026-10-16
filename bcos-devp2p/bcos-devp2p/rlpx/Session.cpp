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
 * @file Session.cpp
 * @brief RLPx session facade over the bcos-network async transport.
 * @date 2026/8/18
 */
#include "Session.h"

#include <bcos-network/Common.h>
#include <bcos-task/Wait.h>
#include <array>
#include <stdexcept>

namespace bcos::devp2p::rlpx
{
Session::Session(NetHostPtr _host, NetSessionPtr _netSession, FramingCipher _egressCipher)
  : m_host(std::move(_host)),
    m_netSession(std::move(_netSession)),
    m_egressCipher(std::move(_egressCipher))
{}

// Defined explicitly (not defaulted in-class): FramingCipher is move-only, and
// declaring a destructor suppresses the implicit move operations.
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

Session::~Session()
{
    try
    {
        close();
    }
    catch (...)
    {}
}

void Session::close()
{
    if (m_netSession)
    {
        // drop() is idempotent in bcos-network (internal CAS).
        m_netSession->drop(bcos::network::DisconnectReason::DisconnectRequested);
        m_netSession.reset();
    }
    if (m_host)
    {
        // Host contract: stop() before the last strong reference is released.
        m_host->stop();
        m_host.reset();
    }
}

void Session::sendMessage(Message const& _message)
{
    auto frameData = m_codec.encode(_message);
    auto encrypted = m_egressCipher.encryptFrame(std::move(frameData));
    // Single-segment payload range (zero-copy); `encrypted` outlives the send
    // because syncWait blocks until the frame has been handed to the socket.
    std::array<bytesConstRef, 1> payloads{bytesConstRef(encrypted.data(), encrypted.size())};
    bool sent = false;
    try
    {
        sent = task::syncWait(m_netSession->sendMessage(payloads));
    }
    catch (bcos::network::NetworkException const& e)
    {
        throw std::runtime_error(std::string("Session::sendMessage failed: ") + e.what());
    }
    if (!sent)
    {
        throw std::runtime_error("Session::sendMessage: session inactive");
    }
}

bcos::codec::rlp::RlpResult<Message> Session::recvMessage()
{
    bcos::network::FrameMeta meta;
    try
    {
        meta = task::syncWait(m_netSession->recvMessage());
    }
    catch (bcos::network::NetworkException const& e)
    {
        // Peer disconnect / local drop closed the recv channel.
        throw std::runtime_error(std::string("Session::recvMessage: connection closed: ") + e.what());
    }
    if (meta.status == bcos::network::FrameMeta::Status::Timeout)
    {
        // 15s of silence: the session stays alive (the read timeout does not
        // drop it); the "timed out" substring is matched by TxGossipService.
        throw std::runtime_error("Session::recvMessage timed out");
    }
    if (meta.status != bcos::network::FrameMeta::Status::Frame)
    {
        throw std::runtime_error("Session::recvMessage: unexpected frame status " +
                                 std::to_string(static_cast<int>(meta.status)));
    }
    // meta.frame holds the decrypted payload; meta.consumed counts wire bytes
    // (see RlpxFrameDecoder), so frameData() does not apply here.
    return m_codec.decode(bytesConstRef(meta.frame.data(), meta.frame.size()));
}

}  // namespace bcos::devp2p::rlpx
