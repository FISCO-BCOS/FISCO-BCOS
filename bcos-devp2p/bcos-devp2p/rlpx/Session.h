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
 * @brief An established RLPx session: framed, encrypted message exchange over a
 *        socket (port of silkworm MessageStream over geth-compatible framing).
 * @date 2026/8/18
 */
#pragma once

#include "Framing.h"
#include "MessageCodec.h"
#include "Socket.h"

namespace bcos::devp2p::rlpx
{
// Sends/receives framed + encrypted messages over a connected socket.
// Owns the socket so that the session can outlive the connection setup scope.
class Session
{
public:
    Session(Socket&& _socket, FramingCipher _cipher);

    void sendMessage(Message const& _message);
    Message recvMessage();

    void enableCompression() { m_codec.enableCompression(); }

private:
    Socket m_socket;
    FramingCipher m_cipher;
    MessageCodec m_codec;
};
}  // namespace bcos::devp2p::rlpx
