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
 * @brief RLPx session implementation.
 * @date 2026/8/18
 */
#include "Session.h"

#include <stdexcept>

namespace bcos::devp2p::rlpx
{
Session::Session(Socket&& _socket, FramingCipher _cipher)
  : m_socket(std::move(_socket)), m_cipher(std::move(_cipher))
{}

void Session::sendMessage(Message const& _message)
{
    auto frameData = m_codec.encode(_message);
    auto encrypted = m_cipher.encryptFrame(std::move(frameData));
    m_socket.sendAll(bytesConstRef(encrypted.data(), encrypted.size()));
}

Message Session::recvMessage()
{
    auto header = m_socket.recvFixed(FramingCipher::headerSize());
    size_t frameSize = m_cipher.decryptHeader(bytesConstRef(header.data(), header.size()));
    if (frameSize > MessageCodec::kMaxFrameSize)
    {
        throw std::runtime_error("Session: frame too large");
    }

    auto encrypted = m_socket.recvFixed(FramingCipher::frameSize(frameSize));
    auto frameData = m_cipher.decryptFrame(
        bytesConstRef(encrypted.data(), encrypted.size()), frameSize);
    return m_codec.decode(bytesConstRef(frameData.data(), frameData.size()));
}

}  // namespace bcos::devp2p::rlpx
