#pragma once
// 0x7e deposit-envelope encoder. Local RLP helpers only cover the deposit field
// set (bytes32 / address / uint256 / uint64); the shared encodeTuple helper
// lives in a later EngineService slice.
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

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

/// Synthesize the L1-attributes deposit envelope (op-geth l1AttributesDeposited /
/// deriveL1InfoDepositHash, with the Isthmus/Jovian calldata layout mirrored by
/// opt8n-ref's l1AttributesLayout and op-geth's rollup_cost.go extractors):
/// sourceHash = keccak256(bytes32(1) || keccak256(l1BlockHash || bytes32(seq))),
/// matching specs.optimism.io L1 attributes deposited (domain 1).
/// calldata = selector || packed fields ([4:8] baseFeeScalar, [8:12] blobBaseFeeScalar,
/// [12:20] seq, [20:28] l1 time, [28:36] l1 number, [36:68] l1 baseFee, [68:100] l1
/// blobBaseFee, [100:132] l1 blockHash, [132:164] batcherHash, Isthmus+ [164:168]
/// opFeeScalar, [168:176] opFeeConstant, Jovian [176:178] DA-footprint gas scalar).
/// The scalar/operator-fee fields have no producer in this chain yet and are zero.
/// Production deposits are derived by the op-node and arrive via
/// payloadAttributes.transactions; this path only serves the built-in single-node CL.
/// l2BlockTime is unused: the spec sourceHash does not bind L2 time.
/// FISCO selects Jovian by genesis feature flag (OpForkFlags::jovianActive), not by
/// an activation height. The synthesizer therefore emits 178B whenever the flag is
/// on — there is no built-in-CL "activation block" that must be 176B. The read side
/// still accepts both forms: 176B is treated as the op-geth activation (deposits-only)
/// shape so an external op-node can send that block; 178B with the Jovian selector is
/// the post-activation form this path produces. Production deposits come from op-node.
inline bcos::bytes synthesizeL1AttributesDeposit(
    const L1BlockInfo& l1Info, bool jovianActive, [[maybe_unused]] uint64_t l2BlockTime)
{
    bcos::bytes data(jovianActive ? JovianL1AttributesLen : IsthmusL1AttributesLen, 0);
    const auto& selector = jovianActive ? JovianL1AttributesSelector :
                                          IsthmusL1AttributesSelector;
    std::copy(selector.begin(), selector.end(), data.begin());
    auto dataSpan = std::span(data);
    auto seqField = dataSpan.subspan(12, 8);
    bcos::toBigEndian(l1Info.sequenceNumber, seqField);
    auto timeField = dataSpan.subspan(20, 8);
    bcos::toBigEndian(l1Info.time, timeField);
    auto numberField = dataSpan.subspan(28, 8);
    bcos::toBigEndian(l1Info.number, numberField);
    intx::be::store(
        std::span<uint8_t, 32>(dataSpan.subspan(36, 32).data(), 32), l1Info.baseFee);
    intx::be::store(
        std::span<uint8_t, 32>(dataSpan.subspan(68, 32).data(), 32), l1Info.blobBaseFee);
    std::copy_n(l1Info.blockHash.bytes, sizeof(evmc::bytes32), data.begin() + 100);

    // op-node L1InfoDepositSource: inner = keccak(l1Hash[32] || bytes32(seq)),
    // sourceHash = keccak(bytes32(domain=1) || inner).
    std::array<uint8_t, 64> innerInput{};
    std::copy_n(l1Info.blockHash.bytes, sizeof(evmc::bytes32), innerInput.begin());
    std::array<uint8_t, 8> seqBe{};
    bcos::toBigEndian(l1Info.sequenceNumber, seqBe);
    std::copy(seqBe.begin(), seqBe.end(), innerInput.begin() + 56);
    const auto inner = bcos::crypto::keccak256Hash(
        bcos::bytesConstRef(innerInput.data(), innerInput.size()));
    std::array<uint8_t, 64> domainInput{};
    domainInput[31] = 1;  // bytes32(uint256(1)) — L1InfoDepositSourceDomain
    std::copy(inner.begin(), inner.end(), domainInput.begin() + 32);
    const auto keccak = bcos::crypto::keccak256Hash(
        bcos::bytesConstRef(domainInput.data(), domainInput.size()));
    evmc::bytes32 sourceHash{};
    std::copy_n(keccak.begin(), sizeof(evmc::bytes32), sourceHash.bytes);

    DepositTx deposit{.source_hash = sourceHash,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = c_l1InfoDepositGas,
        .is_system_tx = false,
        .data = evmc::bytes(data.begin(), data.end())};
    return encodeDepositEnvelope(deposit);
}
}  // namespace bcos::evm::opstack
