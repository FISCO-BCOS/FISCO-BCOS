/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "OpEngineService.h"

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <limits>

namespace bcos::engine::split_detail::op
{
namespace
{
constexpr std::size_t c_hashBytes = 32;

const bcos::h256 c_emptyOmmersHash{
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}};
const bcos::h64 c_posNonce{std::string{"0x0000000000000000"}};
const bcos::h256 c_opEmptyRequestsHash{
    std::string{"0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}};
}  // namespace

std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash)
{
    bcos::rpc::Web3Transaction web3Tx;
    bcos::bytesRef envRef{const_cast<bcos::byte*>(env.data()), env.size()};
    if (auto err = bcos::codec::rlp::decode(envRef, web3Tx); err)
    {
        return std::nullopt;
    }
    auto tarsTx = web3Tx.takeToTarsTransaction();
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    if (tarsTx.sender.empty())
    {
        try
        {
            auto sender = bcos::fromHex(web3Tx.sender());
            tarsTx.sender.assign(sender.begin(), sender.end());
        }
        catch (std::exception const&)
        {
            return std::nullopt;
        }
    }
    return tarsTx;
}

void applyOpHeaderConstants(bcos::protocol::BlockHeader& header)
{
    header.setUncleHash(c_emptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(c_posNonce);
}

std::vector<std::string> supportedOpCapabilities()
{
    auto caps = split_detail::supportedCapabilities();
    caps.push_back("engine_forkchoiceUpdatedV4");
    caps.push_back("engine_getPayloadV4");
    caps.push_back("engine_newPayloadV4");
    return caps;
}

std::optional<std::uint64_t> narrowU256ToU64(const u256& value)
{
    static const u256 maxU64(std::numeric_limits<std::uint64_t>::max());
    if (value > maxU64)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

bcos::h2048 toEthLogsBloom(const Bloom& logsBloom)
{
    return bcos::h2048(logsBloom.data(), logsBloom.size());
}

std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, bool jovianActive)
{
    if (!payloadAttributes.gasLimit.has_value())
    {
        return std::string("gasLimit parameter is required (OP rollup)");
    }
    if (!payloadAttributes.eip1559Params.has_value())
    {
        return std::string("eip1559Params is required on the OP path (Holocene+)");
    }
    if (payloadAttributes.eip1559Params->size() != 8)
    {
        return std::string("eip1559Params must be exactly 8 bytes");
    }
    const auto readU32BE = [&](std::size_t off) {
        return (static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off]) << 24) |
               (static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off + 1]) << 16) |
               (static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off + 2]) << 8) |
               static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off + 3]);
    };
    const auto denominator = readU32BE(0);
    const auto elasticity = readU32BE(4);
    if ((denominator == 0) != (elasticity == 0))
    {
        return std::string(
            "eip1559Params denominator and elasticity must both be zero or both non-zero");
    }
    if (payloadAttributes.withdrawals.has_value() && !payloadAttributes.withdrawals->empty())
    {
        return std::string("withdrawals must be empty on the OP path");
    }
    if (jovianActive && !payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee is required after the Jovian fork");
    }
    if (!jovianActive && payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee must be null before the Jovian fork");
    }
    return std::nullopt;
}

std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive)
{
    const auto& payload = request.executionPayload;

    if (!payload.rawTransactions.has_value())
    {
        return std::string("executionPayload.rawTransactions is required on the OP path");
    }
    if (!payload.withdrawals.has_value() || !payload.withdrawals->empty())
    {
        return std::string("withdrawals must be present and empty on the OP path");
    }
    if (!request.expectedBlobVersionedHashes.empty())
    {
        return std::string("expectedBlobVersionedHashes must be an empty array on the OP path");
    }
    if (!request.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV4");
    }
    if (!payload.withdrawalsRoot.has_value())
    {
        return std::string("withdrawalsRoot is required on the OP path (Isthmus+)");
    }
    if (!payload.excessBlobGas.has_value() || *payload.excessBlobGas != 0)
    {
        return std::string("excessBlobGas must be present and zero on the OP path");
    }
    if (!payload.blobGasUsed.has_value())
    {
        return std::string("blobGasUsed must be present on the OP path");
    }
    if (!jovianActive && *payload.blobGasUsed != 0)
    {
        return std::string("blobGasUsed must be zero before Jovian (OP Isthmus)");
    }
    if (payload.blockNumber < 0)
    {
        return std::string("blockNumber must not be negative");
    }
    if (!narrowU256ToU64(payload.gasLimit).has_value())
    {
        return std::string("gasLimit exceeds the uint64 range of the ETH header field");
    }
    if (*narrowU256ToU64(payload.gasLimit) >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::string("gasLimit exceeds the maximum block gas limit (2^63-1)");
    }
    {
        const auto& extra = payload.extraData;
        if (jovianActive)
        {
            if (extra.size() != 17)
            {
                return std::string("extraData must be exactly 17 bytes on the OP path (Jovian)");
            }
            if (extra[0] != 0x01)
            {
                return std::string("extraData version byte must be 0x01 on the OP path (Jovian)");
            }
        }
        else
        {
            if (extra.size() != 9)
            {
                return std::string("extraData must be exactly 9 bytes on the OP path (Isthmus)");
            }
            if (extra[0] != 0x00)
            {
                return std::string("extraData version byte must be 0x00 on the OP path (Isthmus)");
            }
        }
        const auto readU32BE = [&extra](std::size_t off) {
            return (static_cast<std::uint32_t>(extra[off]) << 24) |
                   (static_cast<std::uint32_t>(extra[off + 1]) << 16) |
                   (static_cast<std::uint32_t>(extra[off + 2]) << 8) |
                   static_cast<std::uint32_t>(extra[off + 3]);
        };
        if (readU32BE(1) == 0)
        {
            return std::string("extraData must encode a non-zero eip-1559 denominator");
        }
        if (readU32BE(5) == 0)
        {
            return std::string("extraData must encode a non-zero eip-1559 elasticity");
        }
    }
    if (!narrowU256ToU64(payload.gasUsed).has_value())
    {
        return std::string("gasUsed exceeds the uint64 range of the ETH header field");
    }
    if (!narrowU256ToU64(*payload.blobGasUsed).has_value())
    {
        return std::string("blobGasUsed exceeds the uint64 range of the ETH header field");
    }
    if (jovianActive && *payload.blobGasUsed > payload.gasLimit)
    {
        return std::string("DA footprint (blobGasUsed) exceeds the block gas limit");
    }
    if (request.executionRequests.has_value() && !request.executionRequests->empty())
    {
        return std::string("executionRequests must be absent or empty on the OP path");
    }
    return std::nullopt;
}

bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot)
{
    auto header = factory->createBlockHeader();
    const auto number = static_cast<bcos::protocol::BlockNumber>(payload.blockNumber);
    header->setNumber(number);
    header->setTimestamp(static_cast<int64_t>(payload.timestamp));
    header->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = payload.parentHash});
    header->setCoinbase(payload.feeRecipient);
    header->setStateRoot(payload.stateRoot);
    header->setTxsRoot(transactionsRoot);
    header->setReceiptsRoot(payload.receiptsRoot);
    const auto bloom = toEthLogsBloom(payload.logsBloom);
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    header->setGasLimit(payload.gasLimit);
    header->setGasUsed(payload.gasUsed);
    header->setExtraData(payload.extraData);
    header->setPrevRandao(payload.prevRandao);
    header->setBaseFee(payload.baseFeePerGas);
    header->setWithdrawalsRoot(payload.withdrawalsRoot.value());
    header->setBlobGasUsed(payload.blobGasUsed.value());
    header->setExcessBlobGas(bcos::u256(0));
    header->setParentBeaconBlockRoot(parentBeaconBlockRoot);
    header->setRequestsHash(c_opEmptyRequestsHash);
    applyOpHeaderConstants(*header);
    return header;
}

}  // namespace bcos::engine::split_detail::op
