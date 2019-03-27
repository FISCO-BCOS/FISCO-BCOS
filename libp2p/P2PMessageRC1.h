/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file P2PMessageRC1.h
 *  @author monan
 *  @date 20181112
 */

#pragma once
#include "P2PMessage.h"

namespace dev
{
namespace p2p
{
class P2PMessageRC1 : public P2PMessage
{
public:
    const static size_t HEADER_LENGTH = 12;
    const static size_t MAX_LENGTH = 1024 * 1024;  ///< The maximum length of data is 1M.

    P2PMessageRC1() { m_buffer = std::make_shared<bytes>(); }
    virtual ~P2PMessageRC1() {}

    void encode(bytes& buffer) override;
    /// < If the decoding is successful, the length of the decoded data is returned; otherwise, 0 is
    /// returned.
    ssize_t decode(const byte* buffer, size_t size) override;
};
enum AMOPPacketType
{
    SendTopicSeq = 1,
    RequestTopics = 2,
    SendTopics = 3
};
}  // namespace p2p
}  // namespace dev
