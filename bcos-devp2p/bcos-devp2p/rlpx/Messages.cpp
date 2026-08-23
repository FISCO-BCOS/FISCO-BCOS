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
 * @file Messages.cpp
 * @brief devp2p base-protocol message RLP codecs.
 * @date 2026/8/18
 */
#include "Messages.h"

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <stdexcept>
#include <vector>

namespace bcos::devp2p::rlpx
{
namespace
{
// Encode a pre-built payload as an RLP item (list or string).
void appendEncoded(bcos::bytes& _to, bcos::bytes const& _item)
{
    _to.insert(_to.end(), _item.begin(), _item.end());
}

bcos::bytes encodeUint(uint64_t _value)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _value);
    return out;
}

bcos::bytes encodeString(bytesConstRef _data)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _data);
    return out;
}

// Expects a list at `_view` and returns a view over its payload.
bcos::bytesRef takeListPayload(bcos::bytesRef& _view)
{
    auto [error, header] = bcos::codec::rlp::decodeHeader(_view);
    if (error || !header.isList)
    {
        throw std::runtime_error("rlpx: expected an RLP list");
    }
    bcos::bytesRef payload(_view.data(), header.payloadLength);
    _view = bcos::bytesRef(
        _view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return payload;
}
}  // namespace

bcos::bytes encodeHello(HelloMessage const& _msg)
{
    // caps payload: each cap = [name, version]
    bcos::bytes capsPayload;
    for (auto const& cap : _msg.capabilities)
    {
        bcos::codec::rlp::encode(
            capsPayload, cap.name, static_cast<uint64_t>(cap.version));
    }
    bcos::bytes capsList;
    bcos::codec::rlp::encodeHeader(
        capsList, {.isList = true, .payloadLength = capsPayload.size()});
    appendEncoded(capsList, capsPayload);

    bcos::bytes versionItem = encodeUint(_msg.version);
    bcos::bytes nameItem = encodeString(bytesConstRef(
        (const bcos::byte*)_msg.clientId.data(), _msg.clientId.size()));
    bcos::bytes portItem = encodeUint(_msg.listenPort);
    bcos::bytes idItem = encodeString(bytesConstRef(_msg.id.data(), _msg.id.size()));

    size_t payloadLength =
        versionItem.size() + nameItem.size() + capsList.size() + portItem.size() + idItem.size();
    bcos::bytes out;
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payloadLength});
    appendEncoded(out, versionItem);
    appendEncoded(out, nameItem);
    appendEncoded(out, capsList);
    appendEncoded(out, portItem);
    appendEncoded(out, idItem);
    return out;
}

HelloMessage decodeHello(bytesConstRef _data)
{
    HelloMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);

    uint64_t version = 0;
    if (auto err = bcos::codec::rlp::decode(items, version))
    {
        throw std::runtime_error("decodeHello: version decode failed");
    }
    msg.version = version;

    if (auto err = bcos::codec::rlp::decode(items, msg.clientId))
    {
        throw std::runtime_error("decodeHello: clientId decode failed");
    }

    // caps list
    auto capsPayload = takeListPayload(items);
    while (!capsPayload.empty())
    {
        auto capItems = takeListPayload(capsPayload);
        Capability cap;
        if (auto err = bcos::codec::rlp::decode(capItems, cap.name))
        {
            throw std::runtime_error("decodeHello: cap name decode failed");
        }
        uint64_t capVersion = 0;
        if (auto err = bcos::codec::rlp::decode(capItems, capVersion))
        {
            throw std::runtime_error("decodeHello: cap version decode failed");
        }
        cap.version = static_cast<uint8_t>(capVersion);
        msg.capabilities.push_back(std::move(cap));
    }

    uint64_t listenPort = 0;
    if (auto err = bcos::codec::rlp::decode(items, listenPort))
    {
        throw std::runtime_error("decodeHello: listenPort decode failed");
    }
    msg.listenPort = listenPort;

    if (auto err = bcos::codec::rlp::decode(items, msg.id))
    {
        throw std::runtime_error("decodeHello: id decode failed");
    }
    return msg;
}

bcos::bytes encodeDisconnect(DisconnectMessage const& _msg)
{
    // geth wire format: disconnectMsg = [reason] — a one-element RLP list, NOT a
    // bare integer. (We encode/disconnect with a bare integer before this fix, which
    // made real geth peers reject the message and made decodeDisconnect fail on
    // real Disconnect frames.)
    bcos::bytes out;
    bcos::codec::rlp::encode(out, std::vector<uint64_t>{static_cast<uint64_t>(_msg.reason)});
    return out;
}

DisconnectMessage decodeDisconnect(bytesConstRef _data)
{
    DisconnectMessage msg;
    // Wire format varies by client: geth/erigon send the EIP-8 list form [reason],
    // while some clients (e.g. ethrex) send a bare integer. Accept both.
    {
        bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
        std::vector<uint64_t> reasons;
        if (auto err = bcos::codec::rlp::decode(view, reasons); err == nullptr)
        {
            msg.reason = reasons.empty() ? DisconnectReason::DisconnectRequested :
                                           static_cast<DisconnectReason>(reasons[0]);
            return msg;
        }
    }
    {
        bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
        uint64_t reason = 0;
        if (auto err = bcos::codec::rlp::decode(view, reason); err == nullptr)
        {
            msg.reason = static_cast<DisconnectReason>(reason);
            return msg;
        }
    }
    throw std::runtime_error("decodeDisconnect: reason decode failed payload=" +
                             bcos::toHexStringWithPrefix(
                                 bcos::bytes(_data.begin(), _data.end())));
}

bcos::bytes encodePing()
{
    // Ping payload is the empty list: 0xc0.
    return bcos::bytes{0xc0};
}

bcos::bytes encodePong()
{
    // Pong payload is the empty list: 0xc0.
    return bcos::bytes{0xc0};
}

}  // namespace bcos::devp2p::rlpx
