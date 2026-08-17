/// @file OpStackTransition.cpp
/// @brief Out-of-line pieces of the OP Stack execution lane: the consensus-grade
///        0x7E deposit decoder and the BCOS <-> evmone converters. The converter
///        bodies follow the pre-split BCOS2Evmone.cpp (dropped by the pure-
///        Ethereum split 4/4) — the OP lane re-needs them because it feeds
///        bcos-evm's opTransition/runDeposit, which consume the evmone types.

#include "OpStackTransition.h"

#include "EthereumTransition.h"  // mapEvmcStatusToBcosStatus / safeFromHex helpers
#include "bcos-framework/protocol/LogEntry.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace bcos::executor_v1::eth
{

const std::error_category& depositDecodeCategory() noexcept
{
    struct Category : std::error_category
    {
        [[nodiscard]] const char* name() const noexcept final { return "op-deposit-decode"; }

        [[nodiscard]] std::string message(int ev) const noexcept final
        {
            switch (static_cast<DepositDecodeError>(ev))
            {
            case DepositDecodeError::WrongTypeByte:
                return "not a 0x7e deposit envelope";
            case DepositDecodeError::MalformedEnvelope:
                return "malformed RLP envelope header (not a list)";
            case DepositDecodeError::TrailingBytes:
                return "trailing bytes after the deposit field list";
            case DepositDecodeError::TruncatedBody:
                return "truncated deposit field list";
            case DepositDecodeError::MalformedField:
                return "malformed deposit field (hash / address / data / integer)";
            case DepositDecodeError::NonCanonicalInteger:
                return "non-canonical integer (leading zero bytes)";
            case DepositDecodeError::IntegerTooWide:
                return "integer wider than the field allows";
            case DepositDecodeError::NonCanonicalBool:
                return "non-canonical bool for isSystemTx";
            case DepositDecodeError::GasLimitOutOfRange:
                return "gas does not fit int64";
            }
            return "unknown deposit decode error";
        }
    };

    static const Category categoryInstance;
    return categoryInstance;
}

std::error_code make_error_code(DepositDecodeError error) noexcept
{
    return {static_cast<int>(error), depositDecodeCategory()};
}

namespace
{
/// Strict RLP unsigned-integer read for the consensus-grade deposit decoder. decodeHeader
/// already enforces the canonical-size rules (0x81-prefixed single byte < 0x80, long-form
/// length < 56); this adds op-geth's integer rules on top: the payload is at most
/// @p maxBytes — the shared decode(UnsignedIntegral) silently wraps/truncates an over-long
/// value — and must not start with a zero byte (canonical zero is the EMPTY payload, so an
/// inline 0x00 is equally non-canonical: "rlp: non-canonical integer (leading zero bytes)").
/// Returns a default-constructed (success) error_code on acceptance.
std::error_code decodeStrictUint(bcos::bytesRef& from, size_t maxBytes, bcos::u256& out)
{
    using namespace bcos::codec::rlp;
    auto&& [error, header] = decodeHeader(from);
    if (error != nullptr || header.isList)
    {
        // MalformedField, not MalformedEnvelope: this is an integer FIELD inside the list
        // (mint / value / gas / isSystemTx), so reporting the outer envelope's code would
        // point an operator at the wrong place.
        return make_error_code(DepositDecodeError::MalformedField);
    }
    if (header.payloadLength > maxBytes)
    {
        return make_error_code(DepositDecodeError::IntegerTooWide);
    }
    // The direct from[i] indexing below is in bounds: decodeHeader's last act is to reject
    // a header announcing more payload than remains (RLPDecode.h, InputTooShort), which
    // the MalformedField branch above already returned on.
    // For an inline single byte (< 0x80) decodeHeader leaves the byte in place as the
    // payload; for prefixed forms it has already consumed the header — either way the
    // payload starts at from[0].
    if (header.payloadLength >= 1 && from[0] == 0x00)
    {
        return make_error_code(DepositDecodeError::NonCanonicalInteger);
    }
    out = 0;
    for (size_t i = 0; i < header.payloadLength; ++i)
    {
        out = (out << 8) | from[i];
    }
    from = from.getCroppedData(header.payloadLength);
    return {};
}
}  // namespace

std::variant<bcos::evm::opstack::DepositTx, std::error_code> decodeDepositTx(
    bcos::bytesConstRef rawDeposit)
{
    using namespace bcos::codec::rlp;

    if (rawDeposit.empty() || rawDeposit[0] != 0x7e)
    {
        return make_error_code(DepositDecodeError::WrongTypeByte);
    }
    // The bcos-codec rlp decode API only offers mutable-ref entry points (it re-slices the
    // ref as it consumes; it never writes through it, but the type demands a writable
    // span). Rather than const_cast the caller's buffer into one, decode from a local
    // copy — deposit envelopes are small (bounded by OP payload attributes), so the copy
    // is a few hundred bytes per deposit.
    bcos::bytes buffer(rawDeposit.begin() + 1, rawDeposit.end());
    bcos::bytesRef in(buffer.data(), buffer.size());

    // Consensus-grade: the envelope is exactly the type byte + one RLP list — trailing
    // bytes after the list are rejected (the display decoder tolerates them).
    auto&& [headerError, header] = decodeHeader(in);
    if (headerError != nullptr || !header.isList)
    {
        return make_error_code(DepositDecodeError::MalformedEnvelope);
    }
    // decodeHeader already rejected a list announcing more than remains, so a mismatch
    // here can only be leftover bytes past the list's end.
    if (header.payloadLength != in.size())
    {
        return make_error_code(DepositDecodeError::TrailingBytes);
    }
    bcos::bytesRef body(in.data(), header.payloadLength);

    bcos::h256 sourceHash;
    bcos::Address from;
    if (decodeItems(body, sourceHash, from) != nullptr)
    {
        return make_error_code(DepositDecodeError::MalformedField);
    }

    bcos::evm::opstack::DepositTx deposit{};
    std::copy_n(sourceHash.data(), sizeof(evmc_bytes32), deposit.source_hash.bytes);
    std::copy_n(from.data(), sizeof(evmc_address), deposit.from.bytes);

    // `to`: the empty RLP item = contract creation (same convention as every
    // Ethereum tx type).
    if (body.empty())
    {
        return make_error_code(DepositDecodeError::TruncatedBody);
    }
    if (body[0] == BYTES_HEAD_BASE)
    {
        deposit.to = std::nullopt;
        body = body.getCroppedData(1);
    }
    else
    {
        bcos::Address to;
        if (decode(body, to) != nullptr)
        {
            return make_error_code(DepositDecodeError::MalformedField);
        }
        evmc::address toAddr{};
        std::copy_n(to.data(), sizeof(evmc_address), toAddr.bytes);
        deposit.to = toAddr;
    }

    // `mint`: the empty RLP item = no mint (op-geth encodes a nil *big.Int that way).
    if (body.empty())
    {
        return make_error_code(DepositDecodeError::TruncatedBody);
    }
    if (body[0] == BYTES_HEAD_BASE)
    {
        deposit.mint = std::nullopt;
        body = body.getCroppedData(1);
    }
    else
    {
        bcos::u256 mint{0};
        if (auto error = decodeStrictUint(body, sizeof(evmc_bytes32), mint))
        {
            return error;
        }
        deposit.mint = evm::toIntxU256(mint);
    }

    // Integer bounds mirror op-geth's DepositTx field types: mint/value are 256-bit
    // (32 bytes), gas is a uint64 (8 bytes), isSystemTx is a bool whose only canonical
    // encodings are the empty payload (false) and 0x01 (true).
    bcos::u256 value{0};
    bcos::u256 gas{0};
    bcos::u256 isSystemTx{0};
    bcos::bytes input;
    if (auto error = decodeStrictUint(body, sizeof(evmc_bytes32), value))
    {
        return error;
    }
    if (auto error = decodeStrictUint(body, sizeof(uint64_t), gas))
    {
        return error;
    }
    if (auto error = decodeStrictUint(body, 1, isSystemTx))
    {
        return error;
    }
    if (isSystemTx > 1)
    {
        return make_error_code(DepositDecodeError::NonCanonicalBool);
    }
    if (decode(body, input) != nullptr)
    {
        return make_error_code(DepositDecodeError::MalformedField);
    }
    if (!body.empty())
    {
        return make_error_code(DepositDecodeError::TrailingBytes);
    }
    // Consensus-grade: gas must additionally fit an int64 (the executable gas_limit).
    if (gas > bcos::u256(std::numeric_limits<int64_t>::max()))
    {
        return make_error_code(DepositDecodeError::GasLimitOutOfRange);
    }
    deposit.value = evm::toIntxU256(value);
    deposit.gas_limit = static_cast<int64_t>(gas);
    deposit.is_system_tx = isSystemTx != 0;
    deposit.data = evmc::bytes(input.begin(), input.end());
    return deposit;
}

evmone::state::BlockInfo toEvmoneBlockInfo(EthBlockInfo const& blockInfo)
{
    evmone::state::BlockInfo info{};
    info.number = blockInfo.number;
    info.timestamp = blockInfo.timestamp;
    info.gas_limit = blockInfo.gas_limit;
    info.coinbase = blockInfo.coinbase;
    info.difficulty = blockInfo.difficulty;
    info.prev_randao = blockInfo.prev_randao;
    info.base_fee = blockInfo.base_fee;
    info.blob_gas_used = blockInfo.blob_gas_used;
    info.excess_blob_gas = blockInfo.excess_blob_gas;
    info.blob_base_fee = blockInfo.blob_base_fee;
    return info;
}

evmone::state::Transaction toEvmoneTransaction(protocol::Transaction const& tx)
{
    evmone::state::Transaction evmTx{};
    switch (tx.web3TypedTxKind())
    {
    case 0:
        evmTx.type = evmone::state::Transaction::Type::legacy;
        break;
    case 1:
        evmTx.type = evmone::state::Transaction::Type::access_list;
        break;
    case 2:
        evmTx.type = evmone::state::Transaction::Type::eip1559;
        break;
    case 3:
        evmTx.type = evmone::state::Transaction::Type::blob;
        break;
    case 4:
        evmTx.type = evmone::state::Transaction::Type::set_code;
        break;
    default:
        // Out-of-enum kinds keep the raw byte so opValidate's whitelist rejects
        // them (its type check runs before anything else).
        evmTx.type = static_cast<evmone::state::Transaction::Type>(tx.web3TypedTxKind());
        break;
    }
    auto const& input = tx.input();
    evmTx.data = evmc::bytes(input.begin(), input.end());
    evmTx.gas_limit = tx.gasLimit();
    if (auto gp = tx.gasPrice(); gp.has_value())
        evmTx.max_gas_price = evm::toIntxU256(*gp);
    if (auto mf = tx.maxFeePerGas(); mf.has_value())
        evmTx.max_gas_price = evm::toIntxU256(*mf);
    if (auto mp = tx.maxPriorityFeePerGas(); mp.has_value())
        evmTx.max_priority_gas_price = evm::toIntxU256(*mp);
    if (auto mb = tx.maxFeePerBlobGas(); mb.has_value())
        evmTx.max_blob_gas_price = evm::toIntxU256(*mb);

    // For legacy/access_list txs (no explicit maxPriorityFeePerGas in the signed
    // RLP) the whole gas price is the tip above the base fee — decided on the
    // kind, same as ethMaxPriorityGasPrice in the pure-Ethereum lane.
    if ((evmTx.type == evmone::state::Transaction::Type::legacy ||
            evmTx.type == evmone::state::Transaction::Type::access_list))
        evmTx.max_priority_gas_price = evmTx.max_gas_price;

    evmTx.sender = ethSender(tx);
    evmTx.to = ethToAddress(tx);
    evmTx.value = evm::toIntxU256(tx.value());
    for (auto const& entry : tx.web3AccessList())
    {
        evmc_address addr{};
        std::copy_n(entry.account.begin(), sizeof(evmc_address), addr.bytes);
        std::vector<evmc::bytes32> keys;
        for (auto const& sk : entry.storageKeys)
        {
            evmc_bytes32 key{};
            std::copy_n(sk.begin(), sizeof(evmc_bytes32), key.bytes);
            keys.push_back(key);
        }
        evmTx.access_list.emplace_back(addr, std::move(keys));
    }
    for (auto const& h : tx.blobVersionedHashes())
    {
        evmc_bytes32 hash{};
        std::copy_n(h.begin(), sizeof(evmc_bytes32), hash.bytes);
        evmTx.blob_hashes.push_back(hash);
    }
    // Parsed strictly and non-throwing (see the pure-Ethereum lane's
    // effectiveNonce): a malformed value falls through to 0, which the signature
    // check upstream already rejected for signed inputs.
    evmTx.chain_id = bcos::safeFromQuantity(tx.chainId()).value_or(0);
    evmTx.nonce = bcos::safeFromQuantity(tx.nonce()).value_or(0);
    for (auto const& auth : tx.authorizationList())
    {
        evmone::state::Authorization ea{};
        ea.chain_id = evm::toIntxU256(bcos::u256(auth.chainId));
        std::copy_n(auth.address.begin(), sizeof(evmc_address), ea.addr.bytes);
        ea.nonce = auth.nonce;
        if (auth.signer.size() == sizeof(evmc_address))
        {
            evmc_address sa{};
            std::copy_n(auth.signer.begin(), sizeof(evmc_address), sa.bytes);
            ea.signer = sa;
        }
        ea.r = evm::toIntxU256(auth.r);
        ea.s = evm::toIntxU256(auth.s);
        ea.v = evm::toIntxU256(bcos::u256(auth.v));
        evmTx.authorization_list.push_back(std::move(ea));
    }
    return evmTx;
}

protocol::TransactionReceipt::Ptr toBcosReceipt(evmone::state::TransactionReceipt const& receipt,
    protocol::TransactionReceiptFactory const& receiptFactory, int64_t blockNumber)
{
    std::vector<protocol::LogEntry> logs;
    logs.reserve(receipt.logs.size());
    for (auto const& l : receipt.logs)
    {
        bcos::bytes addr(l.addr.bytes, l.addr.bytes + sizeof(evmc_address));
        bcos::h256s topics;
        for (auto const& t : l.topics)
            topics.emplace_back(bcos::bytesConstRef(t.bytes, sizeof(evmc_bytes32)));
        bcos::bytes data(l.data.begin(), l.data.end());
        logs.emplace_back(std::move(addr), std::move(topics), std::move(data));
    }
    // evmone's receipt does not carry output data (return data is consumed during
    // execution), matching the pure-Ethereum lane's documented limitation.
    bcos::bytes output;
    return receiptFactory.createReceipt(bcos::u256(static_cast<uint64_t>(receipt.gas_used)),
        std::string{}, logs, mapEvmcStatusToBcosStatus(receipt.status), bcos::ref(output),
        blockNumber);
}

bcos::bytes signedTxEnvelope(protocol::Transaction const& transaction)
{
    if (transaction.type() != static_cast<uint8_t>(protocol::TransactionType::Web3Transaction))
    {
        return {};
    }
    try
    {
        return bcostars::protocol::reassembleWeb3RawTransaction(
            transaction.extraTransactionBytes(), transaction.signatureData());
    }
    catch (std::invalid_argument const&)
    {
        // No decodable wire form — the empty envelope makes opValidate reject the
        // transaction instead of pricing its L1 fee over garbage.
        return {};
    }
}

}  // namespace bcos::executor_v1::eth
