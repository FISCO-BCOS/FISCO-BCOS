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
/**
 * @brief : The implementation of callback from p2p
 * @author: jimmyshi
 * @date: 2018-10-17
 */
#include "SyncMsgHandler.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::blockchain;
using namespace dev::txpool;


void SyncMsgHandler::messageHandler(
    P2PException _e, std::shared_ptr<dev::p2p::Session> _session, Message::Ptr _msg)
{
    if (!checkSession(_session))
    {
        _session->disconnect(LocalIdentity);
        return;
    }

    bytesConstRef frame = ref(*(_msg->buffer()));
    if (!checkPacket(frame))
    {
        LOG(WARNING) << "Received " << frame.size() << ": " << toHex(frame) << endl;
        LOG(WARNING) << "INVALID MESSAGE RECEIVED";
        _session->disconnect(BadProtocol);
        return;
    }

    SyncPacketType packetType = (SyncPacketType)RLP(frame.cropped(0, 1)).toInt<unsigned>();
    RLP r(frame.cropped(1));
    bool ok = interpret(packetType, r);
    if (!ok)
        LOG(WARNING) << "Couldn't interpret packet. " << RLP(r);
}

bool SyncMsgHandler::checkSession(std::shared_ptr<dev::p2p::Session> _session)
{
    /// TODO: denine LocalIdentity after SyncPeer finished
    //_session->id();
    return true;
}

bool SyncMsgHandler::checkPacket(bytesConstRef _msg)
{
    if (_msg[0] > 0x7f || _msg.size() < 2)
        return false;
    if (RLP(_msg.cropped(1)).actualSize() + 1 != _msg.size())
        return false;
    return true;
}

bool SyncMsgHandler::interpret(SyncPacketType _id, RLP const& _r)
{
    switch (_id)
    {
    case StatusPacket:
    case BlockPacket:
    default:
        return false;
    }
    return true;
}
