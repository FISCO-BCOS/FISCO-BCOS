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
/** @file P2PMessageRC2.h
 *  @author yujiechen
 *  @date 2019.03.26
 */

#pragma once

#include "P2PMessage.h"

namespace dev
{
namespace p2p
{
class P2PMessageRC2 : public P2PMessage
{
public:
    /// m_length(4bytes) + m_version(2bytes) + m_protocolID(2bytes) + m_groupID(2bytes) +
    /// m_packetType(2bytes) + m_seq(4bytes)
    const static size_t HEADER_LENGTH = 16;

    P2PMessageRC2()
    {
        m_buffer = std::make_shared<bytes>();
        m_cache = std::make_shared<bytes>();
    }
    bool isRequestPacket() override { return (m_protocolID > 0); }

    virtual ~P2PMessageRC2() {}
    void encode(bytes& buffer) override;
    /// < If the decoding is successful, the length of the decoded data is returned; otherwise, 0 is
    /// returned.
    ssize_t decode(const byte* buffer, size_t size) override;

    virtual void setVersion(VERSION_TYPE const& _version) { setField(m_version, _version); }
    virtual VERSION_TYPE version() const { return m_version; }

    uint32_t deliveredLength() override { return m_deliveredLength; }

protected:
    virtual void encode(std::shared_ptr<bytes> encodeBuffer);

    VERSION_TYPE m_version = 0;

private:
    /// compress the data to be sended
    bool compress(std::shared_ptr<bytes>);
    std::shared_ptr<dev::bytes> m_cache;
    // the packet length delivered by the network
    uint32_t m_deliveredLength = 0;
};
}  // namespace p2p
}  // namespace dev
