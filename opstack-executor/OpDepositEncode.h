#pragma once
// 0x7e deposit-envelope encoder. Local RLP helpers only cover the deposit field
// set (bytes32 / address / uint256 / uint64); the shared encodeTuple helper
// lives in a later EngineService slice.
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace bcos::evm::opstack::detail
{
inline void encodeRlpItem(bcos::bytes& to, evmc::bytes_view v)
{
    bcos::codec::rlp::encode(to, bcos::bytesConstRef(v.data(), v.size()));
}

inline void encodeRlpItem(bcos::bytes& to, const intx::uint256& x)
{
    if (x == 0)
    {
        to.push_back(bcos::codec::rlp::BYTES_HEAD_BASE);
        return;
    }
    uint8_t be[sizeof(intx::uint256)]{};
    intx::be::store(be, x);
    size_t first = 0;
    while (be[first] == 0)
        ++first;
    const size_t len = sizeof(intx::uint256) - first;
    if (len == 1 && be[first] < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        to.push_back(be[first]);
        return;
    }
    bcos::codec::rlp::encodeHeader(to, {.isList = false, .payloadLength = len});
    to.insert(to.end(), be + first, be + sizeof(intx::uint256));
}

inline void encodeRlpItem(bcos::bytes& to, uint64_t v)
{
    bcos::codec::rlp::encode(to, v);
}
}  // namespace bcos::evm::opstack::detail

namespace bcos::evm::opstack
{
// 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data]).
inline bcos::bytes encodeDepositEnvelope(const bcos::evm::opstack::DepositTx& d)
{
    using bcos::evm::opstack::detail::encodeRlpItem;
    bcos::bytes payload;
    encodeRlpItem(payload, evmc::bytes_view(d.source_hash));
    encodeRlpItem(payload, evmc::bytes_view(d.from));
    encodeRlpItem(payload, d.to.has_value() ? evmc::bytes_view(*d.to) : evmc::bytes_view{});
    encodeRlpItem(payload, d.mint.value_or(0));
    encodeRlpItem(payload, d.value);
    encodeRlpItem(payload, static_cast<uint64_t>(d.gas_limit));
    encodeRlpItem(payload, static_cast<uint64_t>(d.is_system_tx ? 1 : 0));
    encodeRlpItem(payload, evmc::bytes_view(d.data.data(), d.data.size()));

    bcos::bytes body;
    bcos::codec::rlp::encodeHeader(body, {.isList = true, .payloadLength = payload.size()});
    body.insert(body.end(), payload.begin(), payload.end());

    bcos::bytes out;
    out.reserve(body.size() + 1);
    out.push_back(0x7e);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
}  // namespace bcos::evm::opstack
