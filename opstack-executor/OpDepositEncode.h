#pragma once
// Deposit-envelope encoder (promoted from test support 08-19: the Tier-2 attribute-driven
// OP build synthesizes the L1-attributes deposit envelope in the engine).
#include <opstack-executor/RlpEncodeTuple.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.hpp>

// 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data]).
inline bcos::bytes encodeDepositEnvelope(const bcos::evm::opstack::DepositTx& d)
{
    using bcos::evm::eth::detail::encodeTuple;
    auto body = encodeTuple(evmc::bytes_view(d.source_hash), evmc::bytes_view(d.from),
        d.to.has_value() ? evmc::bytes_view(*d.to) : evmc::bytes_view{}, d.mint.value_or(0),
        d.value, static_cast<uint64_t>(d.gas_limit),
        static_cast<uint64_t>(d.is_system_tx ? 1 : 0), d.data);
    bcos::bytes out;
    out.reserve(body.size() + 1);
    out.push_back(0x7e);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
