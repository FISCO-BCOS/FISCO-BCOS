/// @file OpEthL1Attributes.cpp
/// @brief Implementation of OpEthL1Attributes.h. The calldata layout and the
///        sourceHash derivation are ported byte-for-byte from
///        opstack-executor/OpDepositEncode.h (synthesizeL1AttributesDeposit),
///        which cites op-node's L1BlockInfo marshalBinaryIsthmus/Jovian; the
///        deposit envelope encoder mirrors encodeDepositEnvelope with
///        bcos::u256 amounts.

#include <opstack-executor/OpEthL1Attributes.h>

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace bcos::executor_v1::opstack
{
namespace
{
namespace rlp = bcos::codec::rlp;

/// Canonical RLP integer encode for a u256: minimal big-endian payload (empty
/// for zero) through the string encoder — zero -> 0x80, a single payload byte
/// < 0x80 -> the bare byte. Byte-identical to the legacy intx encoder
/// (OpDepositEncode.h's detail::encodeRlpItem).
void encodeRlpU256(bcos::bytes& to, const bcos::u256& v)
{
    auto const compact = bcos::toCompactBigEndian(v);
    rlp::encode(to, bcos::bytesConstRef{compact.data(), compact.size()});
}

void encodeRlpItem(bcos::bytes& to, evmc::bytes_view v)
{
    rlp::encode(to, bcos::bytesConstRef(v.data(), v.size()));
}

void storeU32BE(std::span<uint8_t> dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value >> 24);
    dst[1] = static_cast<uint8_t>(value >> 16);
    dst[2] = static_cast<uint8_t>(value >> 8);
    dst[3] = static_cast<uint8_t>(value);
}

void storeU256BE(std::span<uint8_t> dst, const bcos::u256& value)
{
    std::array<uint8_t, 32> be{};
    bcos::toBigEndian(value, be);
    std::copy(be.begin(), be.end(), dst.begin());
}

// Field offsets into the setL1BlockValues calldata (op-node L1BlockInfo
// binary layout: uint32 baseFeeScalar/blobBaseFeeScalar, uint32
// operatorFeeScalar, uint64 operatorFeeConstant; Jovian appends a uint16
// DA-footprint scalar).
inline constexpr std::size_t c_baseFeeScalarOffset = 4;
inline constexpr std::size_t c_blobBaseFeeScalarOffset = 8;
inline constexpr std::size_t c_seqOffset = 12;
inline constexpr std::size_t c_timeOffset = 20;
inline constexpr std::size_t c_numberOffset = 28;
inline constexpr std::size_t c_baseFeeOffset = 36;
inline constexpr std::size_t c_blobBaseFeeOffset = 68;
inline constexpr std::size_t c_blockHashOffset = 100;
inline constexpr std::size_t c_batcherHashOffset = 132;
inline constexpr std::size_t c_operatorFeeScalarOffset = OP_ETH_L1_OPERATOR_FEE_SCALAR_OFFSET;
inline constexpr std::size_t c_operatorFeeConstantOffset = OP_ETH_L1_OPERATOR_FEE_CONSTANT_OFFSET;
}  // namespace

bcos::bytes encodeOpEthDepositEnvelope(const DepositTx& dep)
{
    bcos::bytes payload;
    encodeRlpItem(payload, evmc::bytes_view(dep.sourceHash));
    encodeRlpItem(payload, evmc::bytes_view(dep.from));
    encodeRlpItem(payload, dep.to.has_value() ? evmc::bytes_view(*dep.to) : evmc::bytes_view{});
    // mint: nullopt = no mint; on the wire nil and zero are the same 0x80
    // (op-geth encodes nil *big.Int as the empty item).
    encodeRlpU256(payload, dep.mint.value_or(0));
    encodeRlpU256(payload, dep.value);
    rlp::encode(payload, static_cast<uint64_t>(dep.gasLimit));
    rlp::encode(payload, static_cast<uint64_t>(dep.isSystemTx ? 1 : 0));
    encodeRlpItem(payload, evmc::bytes_view(dep.data.data(), dep.data.size()));

    bcos::bytes body;
    rlp::encodeHeader(body, {.isList = true, .payloadLength = payload.size()});
    body.insert(body.end(), payload.begin(), payload.end());

    bcos::bytes out;
    out.reserve(body.size() + 1);
    out.push_back(static_cast<bcos::byte>(OP_DEPOSIT_TX_TYPE));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

DepositTx buildOpEthL1AttributesDeposit(const OpEthL1BlockInfo& l1Info, bool jovianLayout)
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

    DepositTx deposit{};
    std::copy_n(keccak.begin(), sizeof(evmc::bytes32), deposit.sourceHash.bytes);
    deposit.from = OP_DEPOSITOR;
    deposit.to = OP_L1_BLOCK;
    deposit.mint = std::nullopt;
    deposit.value = 0;
    deposit.gasLimit = OP_ETH_L1_INFO_DEPOSIT_GAS;
    deposit.isSystemTx = false;
    deposit.data.resize(
        jovianLayout ? OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN : OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN, 0);
    const auto& selector =
        jovianLayout ? OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR : OP_ETH_ISTHMUS_L1_ATTRIBUTES_SELECTOR;
    std::copy(selector.begin(), selector.end(), deposit.data.begin());
    auto dataSpan = std::span(deposit.data);
    storeU32BE(dataSpan.subspan(c_baseFeeScalarOffset, 4), l1Info.baseFeeScalar);
    storeU32BE(dataSpan.subspan(c_blobBaseFeeScalarOffset, 4), l1Info.blobBaseFeeScalar);
    auto seqField = dataSpan.subspan(c_seqOffset, 8);
    bcos::toBigEndian(l1Info.sequenceNumber, seqField);
    auto timeField = dataSpan.subspan(c_timeOffset, 8);
    bcos::toBigEndian(l1Info.time, timeField);
    auto numberField = dataSpan.subspan(c_numberOffset, 8);
    bcos::toBigEndian(l1Info.number, numberField);
    storeU256BE(dataSpan.subspan(c_baseFeeOffset, 32), l1Info.baseFee);
    storeU256BE(dataSpan.subspan(c_blobBaseFeeOffset, 32), l1Info.blobBaseFee);
    std::copy_n(l1Info.blockHash.bytes, sizeof(evmc::bytes32),
        deposit.data.begin() + c_blockHashOffset);
    std::copy_n(l1Info.batcherHash.bytes, sizeof(evmc::bytes32),
        deposit.data.begin() + c_batcherHashOffset);
    storeU32BE(dataSpan.subspan(c_operatorFeeScalarOffset, 4), l1Info.operatorFeeScalar);
    auto opFeeConstantField = dataSpan.subspan(c_operatorFeeConstantOffset, 8);
    bcos::toBigEndian(l1Info.operatorFeeConstant, opFeeConstantField);
    return deposit;
}

bcos::bytes synthesizeOpEthL1AttributesDeposit(const OpEthL1BlockInfo& l1Info, bool jovianLayout)
{
    return encodeOpEthDepositEnvelope(buildOpEthL1AttributesDeposit(l1Info, jovianLayout));
}

bcos::bytes synthesizeOpEthL1AttributesEnvelope(const OpForkSchedule& schedule,
    const OpEthL1BlockInfo& l1Info, int64_t l2InternalTimestampMs,
    int64_t parentInternalTimestampMs)
{
    if (isUnsetOpEthL1BlockInfo(l1Info))
    {
        throw std::invalid_argument(
            "synthesizeOpEthL1AttributesEnvelope: refuse to synthesize L1-attributes from an "
            "unset L1BlockInfo (number, time, and blockHash are all zero)");
    }
    if (isUnsetOpEthSystemConfig(l1Info))
    {
        throw std::invalid_argument(
            "synthesizeOpEthL1AttributesEnvelope: refuse to synthesize L1-attributes with an "
            "unset SystemConfig (baseFeeScalar and batcherHash must be non-zero)");
    }
    // Jovian layout only once the PARENT is Jovian too — on the activation block itself
    // the child is Jovian but the parent is not, and op-node still emits Isthmus there.
    auto const jovianAt = [&schedule](int64_t internalTimestampMs) {
        return bcos::ledger::resolveOpFork(schedule, opForkTimestampSec(internalTimestampMs)) >=
               OpFork::Jovian;
    };
    const bool jovianLayout =
        jovianAt(l2InternalTimestampMs) && jovianAt(parentInternalTimestampMs);
    return synthesizeOpEthL1AttributesDeposit(l1Info, jovianLayout);
}
}  // namespace bcos::executor_v1::opstack
