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
/** @file MessageFactory.h
 *  @author monan
 *  @date 20181112
 */
#pragma once

#include "P2PMessage.h"
#include "P2PMessageRC2.h"
#include <libconfig/GlobalConfigure.h>
#include <libnetwork/Common.h>
namespace dev
{
namespace p2p
{
class P2PMessageFactory : public dev::network::MessageFactory
{
public:
    typedef std::shared_ptr<P2PMessageFactory> Ptr;

    virtual ~P2PMessageFactory() {}
    dev::network::Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<P2PMessage>();
        return message;
    }

    virtual uint32_t newSeq()
    {
        uint32_t seq = ++m_seq;
        return seq;
    }
    std::atomic<uint32_t> m_seq = {1};
};

class P2PMessageFactoryRC2 : public P2PMessageFactory
{
public:
    virtual ~P2PMessageFactoryRC2() {}
    dev::network::Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<P2PMessageRC2>();
        return message;
    }
};

}  // namespace p2p
}  // namespace dev