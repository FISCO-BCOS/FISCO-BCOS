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
#include <algorithm>
#include <array>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
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

// Field offsets into the setL1BlockValues calldata laid out below (op-node L1BlockInfo
// binary layout: uint32 baseFeeScalar/blobBaseFeeScalar, uint32 operatorFeeScalar,
// uint64 operatorFeeConstant; Jovian appends a uint16 DA-footprint scalar).
inline constexpr std::size_t c_l1AttributesBaseFeeScalarOffset = 4;
inline constexpr std::size_t c_l1AttributesBlobBaseFeeScalarOffset = 8;
inline constexpr std::size_t c_l1AttributesSeqOffset = 12;
inline constexpr std::size_t c_l1AttributesTimeOffset = 20;
inline constexpr std::size_t c_l1AttributesNumberOffset = 28;
inline constexpr std::size_t c_l1AttributesBaseFeeOffset = 36;
inline constexpr std::size_t c_l1AttributesBlobBaseFeeOffset = 68;
inline constexpr std::size_t c_l1AttributesBlockHashOffset = 100;
inline constexpr std::size_t c_l1AttributesBatcherHashOffset = 132;
inline constexpr std::size_t c_l1AttributesOperatorFeeScalarOffset = 164;
inline constexpr std::size_t c_l1AttributesOperatorFeeConstantOffset = 168;

inline void storeU32BE(std::span<uint8_t> dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value >> 24);
    dst[1] = static_cast<uint8_t>(value >> 16);
    dst[2] = static_cast<uint8_t>(value >> 8);
    dst[3] = static_cast<uint8_t>(value);
}

/// Build the L1-attributes deposit envelope (0x7e).
/// sourceHash = keccak256(bytes32(1) || keccak256(l1BlockHash || bytes32(seq))) — L2 time is
/// deliberately not bound into the sourceHash, so the builder takes no L2 time.
/// Calldata is the setL1BlockValues* layout — Isthmus 176B, Jovian 178B (op-node
/// L1BlockInfo marshalBinaryIsthmus/Jovian):
///   [0:4] selector | [4:8] baseFeeScalar | [8:12] blobBaseFeeScalar
///   [12:20] sequenceNumber | [20:28] timestamp | [28:36] number
///   [36:68] baseFee | [68:100] blobBaseFee | [100:132] blockHash
///   [132:164] batcherHash | [164:168] operatorFeeScalar | [168:176] operatorFeeConstant
///   (Jovian only) [176:178] DA-footprint scalar
/// The Jovian DA-footprint scalar is zero here (op-node decodes zero as its default).
/// The production seam refuses to synthesize from an L1BlockInfo without SystemConfig
/// fields (see isUnsetSystemConfig).
inline bcos::bytes synthesizeL1AttributesDeposit(const L1BlockInfo& l1Info, bool jovianActive)
{
    // op-node L1InfoDepositSource: inner = keccak(l1Hash[32] || bytes32(seq)),
    // sourceHash = keccak(bytes32(domain=1) || inner).
    std::array<uint8_t, 64> innerInput{};
    std::copy_n(l1Info.blockHash.bytes, sizeof(evmc::bytes32), innerInput.begin());
    std::array<uint8_t, 8> seqBe{};
    bcos::toBigEndian(l1Info.sequenceNumber, seqBe);
    std::copy(seqBe.begin(), seqBe.end(), innerInput.begin() + 56);
    const auto inner =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(innerInput.data(), innerInput.size()));
    std::array<uint8_t, 64> domainInput{};
    domainInput[31] = 1;  // bytes32(uint256(1)) — L1InfoDepositSourceDomain
    std::copy(inner.begin(), inner.end(), domainInput.begin() + 32);
    const auto keccak =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(domainInput.data(), domainInput.size()));
    evmc::bytes32 sourceHash{};
    std::copy_n(keccak.begin(), sizeof(evmc::bytes32), sourceHash.bytes);

    DepositTx deposit{.source_hash = sourceHash,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = c_l1InfoDepositGas,
        .is_system_tx = false,
        .data = {}};
    deposit.data.resize(jovianActive ? JovianL1AttributesLen : IsthmusL1AttributesLen, 0);
    const auto& selector = jovianActive ? JovianL1AttributesSelector : IsthmusL1AttributesSelector;
    std::copy(selector.begin(), selector.end(), deposit.data.begin());
    auto dataSpan = std::span(deposit.data);
    storeU32BE(dataSpan.subspan(c_l1AttributesBaseFeeScalarOffset, 4), l1Info.baseFeeScalar);
    storeU32BE(
        dataSpan.subspan(c_l1AttributesBlobBaseFeeScalarOffset, 4), l1Info.blobBaseFeeScalar);
    auto seqField = dataSpan.subspan(c_l1AttributesSeqOffset, 8);
    bcos::toBigEndian(l1Info.sequenceNumber, seqField);
    auto timeField = dataSpan.subspan(c_l1AttributesTimeOffset, 8);
    bcos::toBigEndian(l1Info.time, timeField);
    auto numberField = dataSpan.subspan(c_l1AttributesNumberOffset, 8);
    bcos::toBigEndian(l1Info.number, numberField);
    intx::be::store(
        std::span<uint8_t, 32>(dataSpan.subspan(c_l1AttributesBaseFeeOffset, 32).data(), 32),
        l1Info.baseFee);
    intx::be::store(
        std::span<uint8_t, 32>(dataSpan.subspan(c_l1AttributesBlobBaseFeeOffset, 32).data(), 32),
        l1Info.blobBaseFee);
    std::copy_n(l1Info.blockHash.bytes, sizeof(evmc::bytes32),
        deposit.data.begin() + c_l1AttributesBlockHashOffset);
    std::copy_n(l1Info.batcherHash.bytes, sizeof(evmc::bytes32),
        deposit.data.begin() + c_l1AttributesBatcherHashOffset);
    storeU32BE(
        dataSpan.subspan(c_l1AttributesOperatorFeeScalarOffset, 4), l1Info.operatorFeeScalar);
    auto opFeeConstantField = dataSpan.subspan(c_l1AttributesOperatorFeeConstantOffset, 8);
    bcos::toBigEndian(l1Info.operatorFeeConstant, opFeeConstantField);
    return encodeDepositEnvelope(deposit);
}
}  // namespace bcos::evm::opstack
