/// @file OpstackExecutor.h
/// @brief OP Stack (Optimism L2) transaction executor based on bcos-evm/opstack.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept (ExecuteContext with
/// prepare/execute/finish): opValidate + opTransition for NORMAL transactions, runDeposit for
/// 0x7E deposits, finalizeOpBlock for block finalize. The caller passes an already-decoded
/// DepositTx. Storage-backed StateView and state-diff writeback (Storage2State::applyDiff) are
/// shared with the base module.

#pragma once

#include "bcos-evm/opstack/OpFeeParams.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpTransition.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/TBBWait.h"
#include "ethereum-executor/EVMSupport.h"
#include "opstack-executor/OpCommon.h"  // detail::narrowU256ToU64 / toEvmcAddress / toEvmcBytes32
#include "opstack-executor/OpDepositEncode.h"  // detail::encodeRlpItem (call-path sizing envelope)
#include "opstack-executor/Storage2State.h"    // Storage2State / SharedErrorSlot
#include <bcos-codec/rlp/Common.h>     // BYTES_HEAD_BASE (consensus deposit-envelope decode)
#include <bcos-codec/rlp/RLPDecode.h>  // decodeHeader / decode / decodeItems
#include <bcos-rlp-protocol/Web3TxEnvelope.h>   // isTypedWeb3Envelope (header-only)
#include <bcos-utilities/BoostLog.h>            // BCOS_LOG
#include <bcos-utilities/DataConvertUtility.h>  // safeFromHex / safeFromQuantity
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::opstack
{
// Defined in OpBlockExecute.cpp — forward-declared here to avoid including OpBlockExecute.h
// (which includes this header).
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);
}  // namespace bcos::evm::opstack


namespace bcos::executor_v1::eth
{
/// Convert bcos::u256 to intx::uint256 for the evmone::state (bcos-evm) types this executor
/// still drives. ethereum-executor's own arithmetic and interfaces no longer use intx, so the
/// bridge lives here with its only consumer and retires together with the bcos-evm dependency.
inline intx::uint256 toIntxU256(bcos::u256 const& val)
{
    std::array<bcos::byte, 32> be{};
    bcos::toBigEndian(val, be);  // writes 32 big-endian bytes, no allocation.
    return intx::be::unsafe::load<intx::uint256>(be.data());
}

/// Convert a FISCO `protocol::Transaction` into the evmone `state::Transaction` the OP executor
/// feeds to the VM. Moved from BCOS2Evmone (the only consumer is OpstackExecutor's transaction
/// execution); kept in the eth namespace so `toIntxU256` / the state helpers resolve unchanged.
inline evmone::state::Transaction toEvmoneTransaction(bcos::protocol::Transaction const& tx)
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
        // Fail closed: web3TypedTxKind() is a tars wire field (untrusted input). Folding an
        // unknown kind into legacy would bypass opValidate's type whitelist; op-geth's
        // UnmarshalBinary rejects with ErrTxTypeNotSupported.
        throw bcos::evm::OpConsensusError("toEvmoneTransaction: unsupported web3TypedTxKind: " +
                                          std::to_string(tx.web3TypedTxKind()));
    }
    auto const& input = tx.input();
    evmTx.data = evmc::bytes(input.begin(), input.end());
    evmTx.gas_limit = tx.gasLimit();
    if (auto gp = tx.gasPrice(); gp.has_value())
        evmTx.max_gas_price = toIntxU256(*gp);
    if (auto mf = tx.maxFeePerGas(); mf.has_value())
        evmTx.max_gas_price = toIntxU256(*mf);
    if (auto mp = tx.maxPriorityFeePerGas(); mp.has_value())
        evmTx.max_priority_gas_price = toIntxU256(*mp);
    if (auto mb = tx.maxFeePerBlobGas(); mb.has_value())
        evmTx.max_blob_gas_price = toIntxU256(*mb);

    // For legacy/access_list txs (no explicit maxPriorityFeePerGas),
    // set it = max_gas_price so coinbase gets the gas tip when base_fee=0.
    // Reference: evmone test/statetest/statetest_runner.cpp
    if ((evmTx.type == evmone::state::Transaction::Type::legacy ||
            evmTx.type == evmone::state::Transaction::Type::access_list) &&
        evmTx.max_priority_gas_price == 0)
        evmTx.max_priority_gas_price = evmTx.max_gas_price;
    auto const& sb = tx.sender();
    // An omitted sender is intentional for eth_call/estimateGas and means address(0). Any
    // non-empty representation must be exactly one address; truncating an over-wide value or
    // silently zeroing a short value would fabricate the execution sender.
    if (!sb.empty() && sb.size() != sizeof(evmc_address))
        throw bcos::evm::OpConsensusError(
            "toEvmoneTransaction: sender must be empty or exactly 20 bytes");
    if (!sb.empty())
        std::copy_n(sb.begin(), sizeof(evmc_address), evmTx.sender.bytes);
    auto const& tb = tx.to();
    if (!tb.empty())
    {
        // `to` is uniformly an ASCII hex address string ("0x..." / 40 hex chars)
        // on every tx path: Web3Transaction::takeToTarsTransaction() writes
        // hexPrefixed(), RPC/txpool treat it as hex (TxValidator::isValidToField
        // requires exactly 40 hex chars), and the test helpers use the same
        // "0x..." encoding. The raw 20-byte branch below is kept only as a
        // defensive fallback.
        const bool has0x = tb.size() >= 2 && tb[0] == '0' && (tb[1] == 'x' || tb[1] == 'X');
        const bool is40Hex =
            tb.size() == sizeof(evmc_address) * 2 && std::all_of(tb.begin(), tb.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            });
        if (has0x || is40Hex)
        {
            // Hex-string form. Only a well-formed 20-byte address decodes to a
            // valid recipient: a short decode (e.g. "0x1234" -> 2 bytes) or a
            // malformed one (e.g. "0x" or "0xzz") must NOT be left-aligned into
            // a 20-byte address — Ethereum addresses are big-endian and
            // right-aligned, and copying a short value to the front would
            // produce a wrong (or all-zero) address. Such input leaves `to`
            // unset, i.e. contract creation, matching base behaviour for
            // anything that is not a well-formed address.
            if (auto decoded = bcos::safeFromHex(tb);
                decoded && decoded->size() == sizeof(evmc_address))
            {
                evmc_address ta{};
                std::copy(decoded->begin(), decoded->end(), ta.bytes);
                evmTx.to = ta;
            }
        }
        else if (tb.size() == sizeof(evmc_address))
        {
            // Defensive fallback for raw 20-byte addresses. No production tx path uses this
            // encoding today (EEMakeTransferTx and every RPC/txpool path write hex), but if it
            // ever appears it must be a full 20-byte value — a shorter copy would silently
            // fabricate an address out of the first bytes. Log a warning so an unexpected
            // input encoding that reaches this branch surfaces instead of passing silently.
            BCOS_LOG(WARNING) << LOG_BADGE("OPSTACK")
                              << "to field uses raw 20-byte encoding (fallback branch); not "
                                 "expected from any production tx path";
            evmc_address ta{};
            std::copy_n(tb.begin(), sizeof(evmc_address), ta.bytes);
            evmTx.to = ta;
        }
        // Anything else (short raw bytes, malformed hex) leaves `to` unset —
        // contract creation, never a transfer to address(0).
    }
    evmTx.value = toIntxU256(tx.value());
    // Access-list / blob / auth entries are compile-time FixedBytes (Address / h256).
    // `size()` is constexpr SIZE, so a runtime `size() < N` + `copy_n(N)` guard can never
    // reject over-wide input and would silently look like a length check. Convert through the
    // typed helpers (full N-byte memcpy). The only variable-width addresses on this path are
    // tx.sender() / tx.to() (string_view), already exact-size above.
    static_assert(sizeof(bcos::Address) == sizeof(evmc_address));
    static_assert(sizeof(bcos::h256) == sizeof(evmc_bytes32));
    for (auto const& entry : tx.web3AccessList())
    {
        std::vector<evmc::bytes32> keys;
        keys.reserve(entry.storageKeys.size());
        for (auto const& sk : entry.storageKeys)
            keys.push_back(bcos::evm::engine::detail::toEvmcBytes32(sk));
        evmTx.access_list.emplace_back(
            bcos::evm::engine::detail::toEvmcAddress(entry.account), std::move(keys));
    }
    for (auto const& h : tx.blobVersionedHashes())
        evmTx.blob_hashes.push_back(bcos::evm::engine::detail::toEvmcBytes32(h));
    // chainId and nonce from the BCOS tx use DIFFERENT string encodings in the tars field:
    // nonce is a hex quantity (takeToTarsTransaction writes toQuantity(nonce),
    // Web3Transaction.cpp:262), while chainID is DECIMAL (takeToTarsTransaction writes
    // std::to_string(chainId), Web3Transaction.cpp:263; FISCO-native txs use a decimal chain_id
    // too). safeFromQuantity parses hex quantities, so it is correct for nonce but misparses a
    // decimal chainID
    // ("10" -> 0x10 = 16). Parse chainID as decimal instead. Both are parsed strictly and
    // non-throwing: empty, a double 0x (e.g. "0x0x1a"), a non-numeric value (e.g. a FISCO
    // chain_id like "chain0"), a sign, trailing garbage, or a value above uint64 all fall
    // through to 0. Note that 0 is NOT a guaranteed rejection:
    //   * chain_id 0 never matches a real chain id (and evmone's
    //     validate_transaction does not check chain_id — the signature binds it
    //     upstream, so a malformed chain_id implies an invalid signature that
    //     admission rejects before this executor sees it);
    //   * nonce 0 IS a valid nonce for a fresh account, so a malformed nonce
    //     can only be accepted when the sender's nonce is 0. Like chain_id, the
    //     nonce is part of the signed payload, so a malformed nonce means the
    //     signature check failed upstream — this fallback only ever matters for
    //     byzantine/unsigned inputs.
    evmTx.chain_id = [&]() -> uint64_t {
        auto const s = tx.chainId();
        if (s.empty())
            return 0;
        // std::from_chars: no allocation, no exceptions, no whitespace/sign leniency; an
        // unparsed remainder ("10x") or out-of-range value falls through to 0, same as before.
        uint64_t v = 0;
        auto const* const last = s.data() + s.size();
        auto const [ptr, ec] = std::from_chars(s.data(), last, v, 10);
        return (ec == std::errc{} && ptr == last) ? v : 0;
    }();
    evmTx.nonce = bcos::safeFromQuantity(tx.nonce()).value_or(0);
    for (auto const& auth : tx.authorizationList())
    {
        evmone::state::Authorization ea{};
        ea.chain_id = toIntxU256(bcos::u256(auth.chainId));
        ea.addr = bcos::evm::engine::detail::toEvmcAddress(auth.address);
        ea.nonce = auth.nonce;
        ea.signer = bcos::evm::engine::detail::toEvmcAddress(auth.signer);
        ea.r = toIntxU256(auth.r);
        ea.s = toIntxU256(auth.s);
        ea.v = toIntxU256(bcos::u256(auth.v));
        evmTx.authorization_list.push_back(std::move(ea));
    }
    return evmTx;
}
}  // namespace bcos::executor_v1::eth

namespace bcos::executor_v1::opstack
{

// Prefixed with Op: ethereum-executor defines a same-named but distinct exception
// (EthereumExecutor.h) — an unqualified cross-executor catch would silently mismatch.
DERIVE_BCOS_EXCEPTION(OpEvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpForkRevisionMismatch);
DERIVE_BCOS_EXCEPTION(OpTxValidationFailed);

using bcos::evm::evmstate::SharedErrorSlot;  // Storage2State.h

namespace engine = bcos::evm::engine;

/// Fail-closed mirror↔envelope cross-check (review finding A): the tars mirror's execution
/// fields must match the SIGNED envelope — the bytes the signature binds. A peer that can
/// construct a Transaction whose mirror diverges from its envelope would otherwise execute
/// forged code/caller/value. Returns a mismatch description, or nullopt when consistent.
/// Consensus-critical: field indices and RLP decoding must mirror op-geth's UnmarshalBinary
/// Integer RLP canonicality (op-geth): reject 0x00 for zero (zero must be the empty item
/// 0x80) and leading-zero multi-byte ints. rlp::decode already rejects over-wide payloads
/// (UnexpectedLength); this helper is the canonicality gate, not a width substitute for a
/// truncating decoder. Shared by the deposit decoder and the envelope↔mirror walker so both
/// treat a non-canonical integer the same way. Returns nullopt for anything that is not a
/// canonical integer (list, truncated, non-canonical form); otherwise the payload length.
[[nodiscard]] inline std::optional<size_t> integerPayloadLength(bcos::bytesConstRef const& ref)
{
    if (ref.empty())
        return std::nullopt;
    uint8_t const b = ref[0];
    if (b < 0x80)
    {
        // Byte item: 0x00 is non-canonical (integer zero must be the empty item 0x80);
        // a bare byte 0x01..0x7f is a single payload byte.
        return b == 0 ? std::nullopt : std::optional<size_t>{1};
    }
    if (b <= 0xb7)
    {  // short string
        size_t const pl = static_cast<size_t>(b - 0x80);
        if (ref.size() < 1 + pl)
            return std::nullopt;  // truncated length prefix
        if (pl == 1 && ref[1] < 0x80)
            return std::nullopt;  // single-byte payload < 0x80 must be a bare Byte
        if (pl >= 2 && ref[1] == 0)
            return std::nullopt;  // leading zero byte
        return pl;                // pl == 0: the empty item 0x80, canonical zero
    }
    if (b <= 0xbf)
    {  // long string
        size_t const n = b - 0xb7;
        if (ref.size() < 1 + n)
            return std::nullopt;  // truncated length prefix
        size_t len = 0;
        for (size_t i = 0; i < n; ++i)
            len = (len << 8) | ref[1 + i];
        // Addition-free comparison: ref.size() >= 1 + n is established above, so the
        // subtraction cannot underflow — whereas `ref.size() < 1 + n + len` wraps for
        // len >= 2^64 - 9 and would skip the truncation check entirely.
        if (len > ref.size() - 1 - n)
            return std::nullopt;  // truncated payload
        if (len == 1 && ref[1 + n] < 0x80)
            return std::nullopt;  // single-byte payload < 0x80 must be a bare Byte
        if (len >= 2 && ref[1 + n] == 0)
            return std::nullopt;  // leading zero byte
        return len;               // decodeHeader rejects a long-string length < 56
    }
    return std::nullopt;  // list (not an integer)
}

/// (legacy = bare list; typed 0x01..0x04 = type byte + list, field order per EIP-2718/2930/1559).
/// BOUND COVERAGE: type byte, nonce, gasLimit, to, value, data. NOT bound: sender (needs
/// ecrecover), the fee fields, accessList, blobVersionedHashes, authorizationList — part-5
/// wiring must close those before this gate is the sole trust boundary.
/// The envelope-bytes core below is shared by the per-tx path (m_prepare) and the block path
/// (processOpBlock). Unbound fields are not trusted as a signature: the block path rejects
/// a missing sender and a non-empty authorizationList until ecrecover lands in part-5.
[[nodiscard]] inline std::optional<std::string> envelopeExecutionFieldsMismatch(
    bcos::bytesConstRef extraBytes, evmone::state::Transaction const& evmTx)
{
    namespace rlp = bcos::codec::rlp;
    if (extraBytes.empty())
        return "empty extraTransactionBytes";

    bcos::bytesRef cursor(const_cast<bcos::byte*>(extraBytes.data()), extraBytes.size());
    bool const typed = bcos::rlp::protocol::isTypedWeb3Envelope(extraBytes);
    // The type byte comes from the ENVELOPE, never the forgeable mirror; the mirror-derived
    // evmTx.type must agree with it (a 0x02 envelope with a legacy mirror would otherwise pass
    // the field checks yet execute with legacy fee semantics and a divergent receipts-root leaf).
    uint8_t const envelopeKind = typed ? static_cast<uint8_t>(extraBytes[0]) : uint8_t{0};
    if (envelopeKind != static_cast<uint8_t>(evmTx.type))
        return "tx type mismatch (envelope vs mirror)";
    if (typed)
    {
        cursor = cursor.getCroppedData(1);  // drop the EIP-2718 type byte
    }
    auto [listErr, header] = rlp::decodeHeader(cursor);
    if (listErr || !header.isList || header.payloadLength > cursor.size())
        return "unparseable envelope list";
    bcos::bytesRef walker(cursor.data(), header.payloadLength);

    // Execution-field indices per shape (chainId is checked separately in m_prepare):
    //   legacy:       [nonce, gasPrice, gasLimit, to, value, data, ...]
    //   0x01 (2930):  [chainId, nonce, gasPrice, gasLimit, to, value, data, accessList]
    //   0x02/03/04:   [chainId, nonce, prio, maxFee, gasLimit, to, value, data, ...]
    size_t const nonceIdx = typed ? 1 : 0;
    size_t const gasIdx = typed ? (envelopeKind == 0x01 ? 3 : 4) : 2;
    size_t const toIdx = typed ? (envelopeKind == 0x01 ? 4 : 5) : 3;
    size_t const valueIdx = typed ? (envelopeKind == 0x01 ? 5 : 6) : 4;
    size_t const dataIdx = typed ? (envelopeKind == 0x01 ? 6 : 7) : 5;

    // Walk once, capturing each target item: whole item (header + payload) for the uint
    // fields (with the payload width, so this gate can report "nonce/value over-wide"
    // instead of a generic decode failure), bare payload for the byte fields.
    // rlp::decode already rejects over-wide integers (UnexpectedLength); the plen
    // pre-check is a more specific error, not a substitute for a truncating decoder.
    std::optional<bcos::bytesRef> nonceItem, gasItem, valueItem;
    std::optional<size_t> noncePlen, gasPlen, valuePlen;
    std::optional<bcos::bytesRef> toPayload, dataPayload;
    bool nonceIsList = false;
    bool gasIsList = false;
    bool valueIsList = false;
    bool toIsList = false;
    bool dataIsList = false;
    size_t idx = 0;
    while (!walker.empty())
    {
        auto const itemStart = walker;
        auto [itemErr, itemHeader] = rlp::decodeHeader(walker);
        if (itemErr || itemHeader.payloadLength > walker.size())
            return "malformed envelope item";
        size_t const headerLen = static_cast<size_t>(walker.data() - itemStart.data());
        bcos::bytesRef const wholeItem(itemStart.data(), headerLen + itemHeader.payloadLength);
        bcos::bytesRef const payload = walker.getCroppedData(0, itemHeader.payloadLength);
        if (idx == nonceIdx)
        {
            nonceItem = wholeItem;
            noncePlen = itemHeader.payloadLength;
            nonceIsList = itemHeader.isList;
        }
        if (idx == gasIdx)
        {
            gasItem = wholeItem;
            gasPlen = itemHeader.payloadLength;
            gasIsList = itemHeader.isList;
        }
        if (idx == valueIdx)
        {
            valueItem = wholeItem;
            valuePlen = itemHeader.payloadLength;
            valueIsList = itemHeader.isList;
        }
        if (idx == toIdx)
        {
            toPayload = payload;
            toIsList = itemHeader.isList;
        }
        if (idx == dataIdx)
        {
            dataPayload = payload;
            dataIsList = itemHeader.isList;
        }
        walker = walker.getCroppedData(itemHeader.payloadLength);
        ++idx;
    }
    if (!nonceItem || !gasItem || !valueItem || !toPayload || !dataPayload)
        return "envelope has fewer fields than the type requires";

    // nonce (uint64). rlp::decode rejects over-wide payloads (UnexpectedLength); the plen
    // pre-check keeps the gate's "nonce over-wide" string. List-shaped items are rejected
    // here rather than relying on rlp::decode's UnexpectedList: a 1-byte list (0xc1 0x05)
    // passes the width guard, so the kind check must be explicit like to/data. The
    // canonicality gate (integerPayloadLength) is the same one the deposit decoder applies,
    // so a non-canonical integer cannot slip the mirror↔envelope cross-check as a "match".
    {
        if (nonceIsList)
            return "nonce field is an RLP list";
        if (!integerPayloadLength(*nonceItem).has_value())
            return "nonce is not a canonical integer";
        if (*noncePlen > sizeof(uint64_t))
            return "nonce over-wide";
        uint64_t envNonce = 0;
        if (auto e = rlp::decode(*nonceItem, envNonce); e != nullptr)
            return "nonce decode failed";
        if (evmTx.nonce != envNonce)
            return "nonce mismatch";
    }
    // gasLimit (uint64)
    {
        if (gasIsList)
            return "gasLimit field is an RLP list";
        if (!integerPayloadLength(*gasItem).has_value())
            return "gasLimit is not a canonical integer";
        if (*gasPlen > sizeof(uint64_t))
            return "gasLimit over-wide";
        uint64_t envGas = 0;
        if (auto e = rlp::decode(*gasItem, envGas); e != nullptr)
            return "gasLimit decode failed";
        if (static_cast<uint64_t>(evmTx.gas_limit) != envGas)
            return "gasLimit mismatch";
    }
    // to (20-byte address, or empty for contract creation)
    {
        if (toIsList)
            return "to field is an RLP list";
        if (evmTx.to.has_value())
        {
            if (toPayload->size() != sizeof(evmc_address) ||
                !std::equal(toPayload->begin(), toPayload->end(), evmTx.to->bytes))
                return "to mismatch";
        }
        else if (!toPayload->empty())
        {
            return "to mismatch";
        }
    }
    // value (uint256). Same as nonce: plen pre-check is a specific error string;
    // rlp::decode would also reject over-wide payloads.
    {
        if (valueIsList)
            return "value field is an RLP list";
        if (!integerPayloadLength(*valueItem).has_value())
            return "value is not a canonical integer";
        if (*valuePlen > sizeof(intx::uint256))
            return "value over-wide";
        bcos::u256 envValue{};
        if (auto e = rlp::decode(*valueItem, envValue); e != nullptr)
            return "value decode failed";
        if (eth::toIntxU256(envValue) != evmTx.value)
            return "value mismatch";
    }
    // data
    {
        if (dataIsList)
            return "data field is an RLP list";
        auto const& mirrorData = evmTx.data;
        if (mirrorData.size() != dataPayload->size() ||
            !std::equal(mirrorData.begin(), mirrorData.end(), dataPayload->begin()))
            return "data mismatch";
    }
    return std::nullopt;
}

/// Transaction convenience overload: forwards the tars mirror's envelope bytes to the
/// envelope-bytes core above (the mirror is never trusted — only extraTransactionBytes is read).
[[nodiscard]] inline std::optional<std::string> envelopeExecutionFieldsMismatch(
    bcos::protocol::Transaction const& tx, evmone::state::Transaction const& evmTx)
{
    return envelopeExecutionFieldsMismatch(tx.extraTransactionBytes(), evmTx);
}

/// Shared chainId gate (review finding C): the SIGNED envelope's chainId must equal the node
/// chainId — never the forgeable tars mirror. Typed envelopes carry chainId in RLP field 0,
/// except 0x7E deposits whose field 0 is sourceHash; nullopt elsewhere is malformed, not a
/// pre-EIP-155 exemption. Both execution paths call this envelope-bytes core so parser
/// semantics and rejection text cannot drift.
/// Legacy envelopes: only a genuinely unprotected form (6-field preimage or v=27/28) is exempt;
/// a malformed v (0/1, 29-34) or unparseable tail fails closed instead of being folded into the
/// unprotected exemption (op-geth rejects such signatures).
[[nodiscard]] inline std::optional<std::string> envelopeChainIdMismatch(
    bcos::bytesConstRef envelope, uint64_t nodeChainId)
{
    namespace protocol = bcos::rlp::protocol;
    auto const classified = protocol::classifyWeb3EnvelopeChainId(envelope);
    if (classified.kind == protocol::Web3EnvelopeChainIdKind::Malformed)
    {
        if (protocol::isTypedWeb3Envelope(envelope))
        {
            return "typed tx envelope is missing a parseable chainId";
        }
        return "legacy tx envelope has a malformed chainId/v field";
    }
    // Deposit (0x7E): no chainId field. executeDeposit skips this gate; pool/RPC reject.
    if (classified.kind == protocol::Web3EnvelopeChainIdKind::Protected &&
        classified.chainId != nodeChainId)
    {
        return "tx envelope chain_id " + std::to_string(classified.chainId) +
               " does not match node chainId " + std::to_string(nodeChainId);
    }
    return std::nullopt;
}

/// Transaction convenience overload: deliberately forwards bytes instead of calling the virtual
/// parser, keeping the per-tx path identical to processOpBlock.
[[nodiscard]] inline std::optional<std::string> envelopeChainIdMismatch(
    bcos::protocol::Transaction const& tx, uint64_t nodeChainId)
{
    return envelopeChainIdMismatch(tx.extraTransactionBytes(), nodeChainId);
}

/// Unsigned EIP-2718 sizing envelope for eth_call / estimateGas. CallRequest cannot carry a
/// signed envelope, but opValidate still needs non-empty bytes for L1-cost / calldata-gas
/// estimates. Legacy has no type prefix (type 0 is not an EIP-2718 marker). Typed forms get
/// their type byte and an empty access-list slot; blobHashes / authorizationList are omitted
/// (CallRequest does not populate them). Signatures are omitted — there is none on this path.
///
/// KNOWN ESTIMATE BIAS (accepted, documented): the synthesized envelope is ~65 bytes shorter
/// than the eventually-signed transaction (v/r/s), so estimateGas's L1-cost component is
/// systematically LOW for the same calldata. This is inherent to pre-signature estimation:
/// the caller has not signed yet, so the exact signature bytes (and their flz-compressibility)
/// cannot be known; appending a zero placeholder would mislead the size-based L1-cost formula
/// (zeros compress differently from real v/r/s). The BLOCK path always prices the real signed
/// envelope, so the bias is confined to the estimate returned by eth_call/estimateGas, not to
/// executed transactions.
[[nodiscard]] inline bcos::bytes synthesizeCallSizingEnvelope(
    evmone::state::Transaction const& evmTx)
{
    namespace rlp = bcos::codec::rlp;
    namespace enc = bcos::evm::opstack::detail;
    bcos::bytes payload;
    auto appendU64 = [&](uint64_t v) { enc::encodeRlpItem(payload, v); };
    auto appendU256 = [&](intx::uint256 const& v) { enc::encodeRlpItem(payload, v); };
    auto appendBytes = [&](evmc::bytes_view b) { enc::encodeRlpItem(payload, b); };
    auto appendTo = [&] {
        if (evmTx.to.has_value())
            appendBytes({evmTx.to->bytes, sizeof(evmTx.to->bytes)});
        else
            appendBytes({});
    };
    auto appendEmptyList = [&] {
        rlp::encodeHeader(payload, {.isList = true, .payloadLength = 0});
    };

    if (evmTx.type == evmone::state::Transaction::Type::legacy)
    {
        appendU64(evmTx.nonce);
        appendU256(evmTx.max_gas_price);
        appendU64(static_cast<uint64_t>(evmTx.gas_limit));
        appendTo();
        appendU256(evmTx.value);
        appendBytes({evmTx.data.data(), evmTx.data.size()});
    }
    else
    {
        appendU64(evmTx.chain_id);
        appendU64(evmTx.nonce);
        if (evmTx.type == evmone::state::Transaction::Type::access_list)
        {
            appendU256(evmTx.max_gas_price);
        }
        else
        {
            appendU256(evmTx.max_priority_gas_price);
            appendU256(evmTx.max_gas_price);
        }
        appendU64(static_cast<uint64_t>(evmTx.gas_limit));
        appendTo();
        appendU256(evmTx.value);
        appendBytes({evmTx.data.data(), evmTx.data.size()});
        appendEmptyList();  // accessList
    }

    bcos::bytes out;
    if (evmTx.type != evmone::state::Transaction::Type::legacy)
        out.push_back(static_cast<bcos::byte>(evmTx.type));
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// Block-path only: address(0) is the eth_call default, but a sealed block must not execute
/// with that sender. Used by both m_prepare (after toEvmoneTransaction) and processOpBlock
/// (evmone::state::Transaction already in hand).
[[nodiscard]] inline std::optional<std::string> blockPathZeroSender(evmc::address const& sender)
{
    if (sender == evmc::address{})
        return "empty sender";
    return std::nullopt;
}

/// Block-path only: 7702 authorizationList[].signer is copied from the tars mirror and is
/// not recovered from the signed envelope (part-5 ecrecover). A non-empty list would let
/// processAuthorizationList apply unbound signers. A set_code (0x04) transaction is rejected
/// regardless of the mirror list: an empty mirror list would otherwise let the block producer
/// strip the delegations while the envelope (whose type byte is bound by
/// envelopeExecutionFieldsMismatch before this gate runs on both block paths) still says 0x04.
/// Used by both m_prepare and processOpBlock.
[[nodiscard]] inline std::optional<std::string> blockPathUnboundAuthorizationList(
    evmone::state::Transaction const& evmTx)
{
    if (evmTx.type == evmone::state::Transaction::Type::set_code ||
        !evmTx.authorization_list.empty())
        return "authorizationList is not bound to the signed envelope";
    return std::nullopt;
}

/// BlockHashes that answers zero for every block number — the eth_call / standalone-tx paths
/// have no block-hash source (the BLOCKHASH opcode then reads zero, as an out-of-window lookup).
class NullBlockHashes final : public evmone::state::BlockHashes
{
public:
    evmc::bytes32 get_block_hash(int64_t) const noexcept override { return {}; }
};

/// Decode `0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx, data])`.
/// Trust boundary: deposits are unsigned, so a peer can forge tars mint/value; only the
/// 0x7e envelope bytes bind those fields. Never read mint/value from the tars mirror.
[[nodiscard]] inline bcos::evm::opstack::DepositTx decodeDepositEnvelope(bcos::bytesConstRef env)
{
    namespace op = bcos::evm::opstack;
    namespace rlp = bcos::codec::rlp;
    auto fail = [](std::string const& msg) {
        BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(msg));
    };
    // Canonicality gate shared with the envelope↔mirror walker (integerPayloadLength above).
    if (env.size() < 2 || env[0] != static_cast<uint8_t>(op::kDepositTxType))
        fail("deposit envelope: not a 0x7e deposit");
    bcos::bytesRef body{const_cast<bcos::byte*>(env.data() + 1), env.size() - 1};
    auto [err, header] = rlp::decodeHeader(body);
    if (err != nullptr || !header.isList)
        fail("deposit envelope: body must be an RLP list");
    if (header.payloadLength != body.size())
        fail("deposit envelope: trailing bytes after the RLP list");
    bcos::bytesRef items(body.data(), header.payloadLength);

    bcos::crypto::HashType sourceHash;
    bcos::Address from;
    if (auto e = rlp::decodeItems(items, sourceHash, from); e != nullptr)
        fail("deposit envelope: sourceHash/from decode failed: " + e->errorMessage());

    op::DepositTx dep;
    std::copy_n(sourceHash.begin(), sizeof(evmc::bytes32), dep.source_hash.bytes);
    std::copy_n(from.begin(), sizeof(evmc::address), dep.from.bytes);

    // to: empty RLP item = contract creation (same convention as every Ethereum tx type)
    if (items.empty())
        fail("deposit envelope: missing to field");
    if (items[0] == rlp::BYTES_HEAD_BASE)
    {
        items = items.getCroppedData(1);
    }
    else
    {
        bcos::Address to{};
        if (auto e = rlp::decode(items, to); e != nullptr)
            fail("deposit envelope: to decode failed");
        evmc::address ta{};
        std::copy_n(to.begin(), sizeof(evmc::address), ta.bytes);
        dep.to = ta;
    }
    // mint: empty RLP item = no mint (op-geth encodes nil *big.Int as the empty item; on the wire
    // nil and zero are the same 0x80, so nullopt matches op-geth's decode-side behavior)
    if (items.empty())
        fail("deposit envelope: missing mint field");
    std::optional<bcos::u256> mint;
    if (items[0] == rlp::BYTES_HEAD_BASE)
    {
        items = items.getCroppedData(1);
    }
    else
    {
        if (auto pl = integerPayloadLength(items); !pl || *pl > 32)
            fail("deposit envelope: mint non-canonical or over-wide (>32 bytes)");
        bcos::u256 m{0};
        if (auto e = rlp::decode(items, m); e != nullptr)
            fail("deposit envelope: mint decode failed");
        mint = m;
    }
    bcos::u256 value{0};
    uint64_t gas = 0;
    uint64_t isSystemTxValue = 0;
    bcos::bytes data;
    // value (u256): width + canonicality check before decode — over-wide would truncate silently.
    if (auto pl = integerPayloadLength(items); !pl || *pl > 32)
        fail("deposit envelope: value non-canonical or over-wide (>32 bytes)");
    if (auto e = rlp::decode(items, value); e != nullptr)
        fail("deposit envelope: value decode failed");
    // gas: width + canonicality, then int64 range. Over-range would wrap to -1.
    if (auto pl = integerPayloadLength(items); !pl || *pl > 8)
        fail("deposit envelope: gas non-canonical or over-wide (>8 bytes)");
    if (auto e = rlp::decode(items, gas); e != nullptr)
        fail("deposit envelope: gas decode failed");
    if (gas > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        fail("deposit envelope: gas exceeds int64 range");
    // isSystemTx: 0 or 1 only, matching op-geth decodeBool. Decoded as uint64 because
    // the bool overload rejects the empty-item false.
    if (auto pl = integerPayloadLength(items); !pl || *pl > 8)
        fail("deposit envelope: isSystemTx non-canonical or over-wide (>8 bytes)");
    if (auto e = rlp::decode(items, isSystemTxValue); e != nullptr)
        fail("deposit envelope: isSystemTx decode failed");
    if (isSystemTxValue > 1)
        fail("deposit envelope: isSystemTx must be 0 or 1");
    if (auto e = rlp::decode(items, data); e != nullptr)
        fail("deposit envelope: data decode failed");
    if (!items.empty())
        fail("deposit envelope: trailing bytes inside the RLP list");

    dep.mint = mint.has_value() ?
                   std::optional<intx::uint256>{bcos::executor_v1::eth::toIntxU256(*mint)} :
                   std::nullopt;
    dep.value = bcos::executor_v1::eth::toIntxU256(value);
    dep.gas_limit = static_cast<int64_t>(gas);
    dep.is_system_tx = isSystemTxValue != 0;
    dep.data = evmc::bytes(data.begin(), data.end());
    return dep;
}

/// Per-block execution state threaded through ExecuteContext (shared scheduler plan Task 3).
/// fee is loaded lazily on the first NORMAL tx (after the L1 attributes deposit has run);
/// blockGasLeft / cumulativeGasUsed / seenNonDeposit are mutated per tx; hashes / chainId are
/// fixed at block construction; daFootprintGasScalar (Jovian) overrides
/// fee.da_footprint_gas_scalar when set. The first five fields are mutable so a
/// `BlockContext const*` can write them. Namespace-scope (not nested) so OpScheduler / tests can
/// value-initialize it (a nested struct's default member initializers are unusable outside
/// OpstackExecutor's member functions).
///
/// SERIAL-ONLY: the mutable fields are written per-tx through a shared const* without
/// synchronization, and the cumulativeGasUsed backfill in finish() assumes strict tx order.
/// Correctness relies on a serial driver (SchedulerSerialImpl).
struct OpBlockExecutionContext
{
    mutable bcos::evm::opstack::OpFeeParams fee;
    mutable bool feeLoaded = false;
    mutable int64_t blockGasLeft = 0;
    mutable int64_t cumulativeGasUsed = 0;
    mutable size_t transactionIndex = 0;
    mutable bool seenNonDeposit = false;
    evmone::state::BlockHashes* blockHashes = nullptr;
    uint64_t chainId = 0;
    std::optional<uint16_t> daFootprintGasScalar;
};

/// The OP transaction executor. Discard-writes contract: on any throw out of
/// prepare/execute/finish or executeTransaction/executeDeposit, the caller must discard all
/// writes already applied to the storage view. Error classification: OpConsensusError -> INVALID,
/// OpStorageError -> -32603; a poisoned shared error slot always means a storage fault.
class OpstackExecutor
{
public:
    OpstackExecutor(protocol::TransactionReceiptFactory::Ptr receiptFactory,
        crypto::Hash::Ptr hashImpl,
        bcos::evm::opstack::OpForkConfig forkConfig = bcos::evm::opstack::jovianConfig(),
        std::shared_ptr<SharedErrorSlot> sharedError = {})
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_forkConfig(std::move(forkConfig)),
        m_sharedError(std::move(sharedError)),
        m_vm(evmc_create_evmone())
    {}

    /// The block-wide storage-error slot shared by every Storage2State this executor builds
    /// (op-geth's dbErr analogue): a read error in ANY per-tx execution instance poisons the
    /// shared slot, which the block-level stateRoot check then rejects.
    [[nodiscard]] const std::shared_ptr<SharedErrorSlot>& sharedError() const noexcept
    {
        return m_sharedError;
    }

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// BlockInfo for tx execution, mirroring detail::toBlockInfo. Leniency follows the call
    /// kind: eth_call (lenientOptionals=true) reads unset header fields as 0; block execution
    /// (false) rejects a malformed header at the point of use instead of failing open.
    static evmone::state::BlockInfo buildBlockInfo(
        protocol::BlockHeader const& header, uint64_t gasLimit, bool lenientOptionals = true)
    {
        return bcos::evm::engine::detail::toBlockInfo(header, gasLimit, lenientOptionals);
    }

    /// Real header gasLimit, falling back to the caller's blockGasLeft when the header leaves it
    /// unset (==0, e.g. minimal test headers).
    static uint64_t opBlockGasLimit(protocol::BlockHeader const& header, uint64_t fallback)
    {
        namespace detail = bcos::evm::engine::detail;
        auto const gl = header.gasLimit();  // non-optional u256 (BlockHeader.h:156)
        return (gl == 0) ? fallback : detail::narrowU256ToU64(gl, "BlockInfo::gasLimit");
    }

    // ---- TransactionExecutor concept: ExecuteContext with prepare/execute/finish ----
    // BlockContext aliases the namespace-scope OpBlockExecutionContext (defined above).
    using BlockContext = OpBlockExecutionContext;

    template <class Storage>
    struct ExecuteContext
    {
        OpstackExecutor& executor;
        Storage& storage;
        protocol::BlockHeader const& blockHeader;
        protocol::Transaction const& transaction;
        int contextID;
        ledger::LedgerConfig const& ledgerConfig;
        // eth_call leniency: skips the chainId gate (prepare) and uses lenient header optionals.
        // The concept path is still block-execution-oriented: per-block ctx bookkeeping
        // (seenNonDeposit / blockGasLeft / cumulativeGasUsed) runs regardless, so a dry-run
        // driver must own a throwaway BlockContext.
        bool call;

        // Per-transaction state threaded across the concept lifecycle.
        bcos::evm::opstack::OpTxProperties m_props;   // set by prepare()
        protocol::TransactionReceipt::Ptr m_receipt;  // set by execute()
        evmone::state::StateDiff m_diff;              // writeback deferred to finish()
        bcos::evm::opstack::DepositTx m_deposit;      // set by prepare() for deposit txs
        // BlockInfo built once in prepare() and reused by execute() (same inputs, same value).
        std::optional<evmone::state::BlockInfo> m_blockInfo;

        // One Storage2State per tx for prepare + execute so validate-phase cache hits in
        // transition.
        std::unique_ptr<bcos::evm::evmstate::Storage2State<Storage>> stateView;

        // Shared per-block context; the mutable fields above are written through this const
        // pointer. The caller owns the BlockContext and must keep it alive across the lifecycle.
        BlockContext const* m_ctx;

        ExecuteContext(OpstackExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c,
            BlockContext const* blockCtx)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c),
            m_props{},
            m_receipt{},
            m_diff{},
            m_deposit{},
            // Initializer order must match declaration order: stateView is declared before m_ctx,
            // so it must be initialized first (GCC -Werror=reorder).
            stateView(std::make_unique<bcos::evm::evmstate::Storage2State<Storage>>(
                st, exec.sharedError())),
            m_ctx(blockCtx)
        {}

        // concept lifecycle: prepare (validate) -> execute (transition) -> finish (writeback).
        // Deposit txs short-circuit (no opValidate / fee / m_finish writeback); normal txs run the
        // three shared stages with the block-context fee (lazily loaded) + blockGasLeft.

        // The 6-arg createExecuteContext leaves m_ctx null; every lifecycle call on that form is
        // unsupported for OP execution.
        void requireBlockContext() const
        {
            if (m_ctx == nullptr)
                throw bcos::evm::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
        }

        task::Task<void> prepare()
        {
            requireBlockContext();
            if (transaction.isDepositTx())
            {
                if (m_ctx->seenNonDeposit)
                    // Deposit after a non-deposit: warn only. op-geth/op-reth accept this
                    // at validation (they enforce deposit-first at the sequencer).
                    BCOS_LOG(WARNING)
                        << LOG_BADGE("OPSTACK") << "deposit after non-deposit in block — accepted";
                try
                {
                    m_deposit = OpstackExecutor::depositFromTransaction(transaction);
                }
                catch (const OpTxValidationFailed& e)
                {
                    // Bad deposit envelope is a consensus reject, not an internal error.
                    throw bcos::evm::OpConsensusError(
                        std::string("OpScheduler: deposit envelope validation failed: ") +
                        e.what());
                }
                co_return;  // deposit has no opValidate
            }
            if (!m_ctx->feeLoaded)
            {  // Load fee params after the L1 attributes deposit.
                namespace op = bcos::evm::opstack;
                m_ctx->fee = op::loadOpFeeParams(*stateView);
                if (m_ctx->daFootprintGasScalar)
                    m_ctx->fee.da_footprint_gas_scalar = *m_ctx->daFootprintGasScalar;
                m_ctx->feeLoaded = true;
            }
            m_blockInfo = buildBlockInfo(blockHeader,
                opBlockGasLimit(blockHeader, static_cast<uint64_t>(m_ctx->blockGasLeft)), call);
            try
            {  // Validation failure is a consensus reject.
                m_props = co_await executor.m_prepare(*stateView, blockHeader, transaction,
                    ledgerConfig, m_ctx->fee, m_ctx->blockGasLeft,
                    call ? std::optional<uint64_t>{} : std::optional<uint64_t>(m_ctx->chainId),
                    &*m_blockInfo, call);
            }
            catch (const OpTxValidationFailed& e)
            {
                // The offending tx's hash rides in a structured member (bcos::Error carries a
                // string only across the delegate boundary): the engine's OP build loop reads
                // e.txHash to evict the culprit from the pool instead of failing every
                // subsequent build — never parse the message text.
                bcos::evm::OpConsensusError err(
                    std::string("OpScheduler: normal tx validation failed: ") + e.what());
                err.txHash = transaction.hash();
                throw err;
            }
            // Only after a successful prepare: a rejected normal tx must not flip the
            // deposit-after-non-deposit warn path for a later deposit in the same block.
            m_ctx->seenNonDeposit = true;
        }
        task::Task<void> execute()
        {
            requireBlockContext();
            // A missing block-hashes source on the block path would silently degrade BLOCKHASH
            // to zeros (NullBlockHashes is the documented eth_call/standalone fallback) — fail
            // loud instead of executing a deterministic-but-wrong state transition.
            if (m_ctx->blockHashes == nullptr && !call)
                throw bcos::evm::OpConsensusError(
                    "OpstackExecutor: block execution requires wired RecentBlockHashes");
            if (transaction.isDepositTx())
            {
                // executeDeposit member (not the op::runDeposit free function); applies the state
                // diff internally.
                try
                {
                    m_receipt = co_await executor.executeDeposit(storage, blockHeader, m_deposit,
                        m_ctx->chainId, m_ctx->blockGasLeft, ledgerConfig, m_ctx->blockHashes,
                        call);
                }
                catch (...)
                {
                    // Typed engine errors pass through; the rest classify as INVALID.
                    rethrowExecError("deposit execution");
                }
            }
            else
            {
                m_receipt = co_await executor.m_execute(*stateView, blockHeader, transaction,
                    ledgerConfig, m_props, m_diff, m_ctx->chainId, m_ctx->blockGasLeft,
                    m_ctx->blockHashes,  // Real block history; only eth_call uses the null
                                         // fallback.
                    m_blockInfo.has_value() ? &*m_blockInfo : nullptr, call);
            }
        }
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            requireBlockContext();
            namespace op = bcos::evm::opstack;
            protocol::TransactionReceipt::Ptr receipt;
            if (transaction.isDepositTx())
            {
                receipt = m_receipt;  // executeDeposit applied the diff only when call=false
            }
            else
            {
                receipt = co_await executor.m_finish(
                    storage, blockHeader, ledgerConfig, m_receipt, m_diff, call);
            }
            // This stage solely owns cumulative-gas backfill + blockGasLeft decrement
            // (narrowGasUsed / decimalCumulative live in OpCommon.h). Decimal + the block index —
            // the RPC read path lexical_casts decimal only and serves transactionIndex from the
            // receipt.
            auto gasUsed = op::narrowGasUsed(receipt->gasUsed());
            m_ctx->cumulativeGasUsed += gasUsed;
            receipt->setCumulativeGasUsed(
                op::decimalCumulative(static_cast<uint64_t>(m_ctx->cumulativeGasUsed)));
            receipt->setTransactionIndex(m_ctx->transactionIndex++);
            m_ctx->blockGasLeft -= gasUsed;
            co_return receipt;
        }
    };

    /// 7-arg form (OP path): the caller owns the BlockContext and must keep it alive across the
    /// prepare/execute/finish lifecycle (SchedulerSerialImpl forwards the ctx that lives in
    /// OpScheduler's coroutine frame).
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        BlockContext const& blockCtx)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call, &blockCtx};
    }

    // Deleted rvalue overload: this is a lazy coroutine, so a temporary bound to the const& above
    // dies at the call-site full expression — before the body first runs — leaving m_ctx dangling.
    // Make the misuse a compile error instead of UB.
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        BlockContext const&& blockCtx) = delete;


    /// 6-arg form (generic scheduler + the TransactionExecutor concept probe): no BlockContext is
    /// available, so m_ctx is null and any prepare/execute/finish throws. The previous default
    /// argument `= BlockContext{}` bound a temporary to the const-ref parameter whose lifetime
    /// ended at the full expression — m_ctx stayed dangling (footgun). OP is never driven through
    /// this form (SchedulerSerialImpl's requires probe always picks the 7-arg overload), but it
    /// must stay valid for the concept, which calls with 6 args.
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call, nullptr};
    }

    /// 6-arg form matching the TransactionExecutor concept probe (TransactionExecutor.h:18).
    /// This IS the eth_call / estimateGas entry point: BaselineScheduler's coCallLatest
    /// (transaction-scheduler/BaselineScheduler-tpp.h:805) and callAtBlock (:868) resolve to this
    /// overload, so it must drive a real simulation rather than throw. A local BlockContext is
    /// built in the coroutine frame (an lvalue, so it outlives the awaits and does not trip the
    /// deleted rvalue createExecuteContext overload); ctx.blockHashes stays null, which
    /// execute() accepts for call=true. The block path (call=false) still throws: it needs the
    /// fee / blockGasLeft / blockHashes that only a scheduler-provided BlockContext carries.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        if (!call)
        {
            throw bcos::evm::OpConsensusError(
                "OpstackExecutor: 6-arg executeTransaction block execution requires a "
                "scheduler-provided BlockContext (use the 10-arg form)");
        }
        BlockContext ctx{};
        if (auto const& chainId = ledgerConfig.chainId(); chainId.has_value())
        {
            ctx.chainId = static_cast<uint64_t>(bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef{
                reinterpret_cast<bcos::byte const*>(chainId->bytes), sizeof(chainId->bytes)}));
        }
        ctx.blockGasLeft =
            static_cast<int64_t>(opBlockGasLimit(blockHeader, static_cast<uint64_t>(0)));
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call, ctx);
        co_await executeContext.prepare();
        co_await executeContext.execute();
        co_return co_await executeContext.finish();
    }

    /// Execute a single OP normal transaction (injection-style, mirroring processOpBlock).
    /// Orchestrator supplies fee, decrementing blockGasLeft, chainId, and real block hashes.
    /// All trailing params are required: these are coroutines (task::Task is lazy — the body runs
    /// after the call expression), so a defaulted `const& fee = {}` would bind a temporary that is
    /// destroyed before first use, leaving the frame holding a dangling reference.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        bcos::evm::opstack::OpFeeParams const& fee, int64_t blockGasLeft, uint64_t chainId,
        evmone::state::BlockHashes const* blockHashes)
    {
        (void)contextID;

        // Same fail-loud guard as ExecuteContext::execute: a missing block-hashes source on the
        // block path would silently degrade BLOCKHASH to zeros (NullBlockHashes is the documented
        // eth_call/standalone fallback) — fail loud instead of executing a deterministic-but-wrong
        // state transition.
        if (blockHashes == nullptr && !call)
            throw bcos::evm::OpConsensusError(
                "OpstackExecutor: block execution requires wired RecentBlockHashes");

        if (transaction.isDepositTx())
        {
            bcos::evm::opstack::DepositTx dep;
            try
            {
                dep = depositFromTransaction(transaction);
            }
            catch (const OpTxValidationFailed& e)
            {
                // Same error normalization as ExecuteContext::prepare: a malformed deposit
                // envelope is a CONSENSUS rejection (INVALID), not an internal error.
                throw bcos::evm::OpConsensusError(
                    std::string("OpScheduler: deposit envelope validation failed: ") + e.what());
            }
            try
            {
                co_return co_await executeDeposit(storage, blockHeader, dep, chainId, blockGasLeft,
                    ledgerConfig, blockHashes, call);
            }
            catch (...)
            {
                // Same ladder as ExecuteContext::execute's deposit branch.
                rethrowExecError("deposit execution");
            }
        }

        // eth_call (call=true) simulates without chain binding — op-geth eth_call is lenient about
        // chainId; block execution (call=false) compares the SIGNED envelope to the node (op-geth
        // EIP155Signer/modernSigner ErrInvalidChainId).
        // Same Storage2State for prepare and execute.
        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)), call);
        bcos::evm::opstack::OpTxProperties props;
        try
        {  // Validation failure is a consensus reject.
            props = co_await m_prepare(stateView, blockHeader, transaction, ledgerConfig, fee,
                blockGasLeft, call ? std::optional<uint64_t>{} : chainId, &blockInfo, call);
        }
        catch (const OpTxValidationFailed& e)
        {
            // Mirror the ExecuteContext::prepare catch: the offending tx's hash rides in a
            // structured member so the engine's OP build loop can evict the culprit by hash —
            // never parse the message text.
            bcos::evm::OpConsensusError err(
                std::string("OpScheduler: normal tx validation failed: ") + e.what());
            err.txHash = transaction.hash();
            throw err;
        }
        evmone::state::StateDiff diff;
        auto receipt = co_await m_execute(stateView, blockHeader, transaction, ledgerConfig, props,
            diff, chainId, blockGasLeft, blockHashes, &blockInfo, call);
        co_return co_await m_finish(storage, blockHeader, ledgerConfig, receipt, diff, call);
    }

    /// Execute a single OP 0x7E deposit transaction (reuses runDeposit).
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeDeposit(Storage& storage,
        protocol::BlockHeader const& blockHeader, bcos::evm::opstack::DepositTx const& dep,
        uint64_t chainId, int64_t blockGasLeft, ledger::LedgerConfig const& ledgerConfig,
        evmone::state::BlockHashes const* blockHashes, bool call)
    {
        namespace op = bcos::evm::opstack;

        checkForkRevision(ledgerConfig);

        // Same fail-loud guard as ExecuteContext::execute / executeTransaction: a missing
        // block-hashes source on the block path would silently degrade BLOCKHASH to zeros.
        if (blockHashes == nullptr && !call)
            throw bcos::evm::OpConsensusError(
                "OpstackExecutor: block execution requires wired RecentBlockHashes");

        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)), call);
        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        NullBlockHashes nullBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : nullBlockHashes;

        evmone::state::StateDiff diff;
        auto receipt = op::runDeposit(stateView, blockInfo, bh, dep, m_forkConfig, m_vm, chainId,
            blockGasLeft, m_receiptFactory, diff);
        // runDeposit sanitizes the diff at source (OpTransition.cpp), satisfying applyDiff's
        // precondition. applyDiff poisons AND rethrows raw; every write-back failure is a local
        // storage fault, so it must leave as OpStorageError (-32603), never INVALID.
        // call=true (eth_call/estimateGas) discards the simulated deposit diff, matching the
        // normal-tx guard in m_finish; the poisoned() check still runs so a simulation storage
        // fault surfaces as OpStorageError.
        if (!call)
        {
            try
            {
                stateView.applyDiff(diff);
            }
            catch (const std::exception& e)
            {
                throw engine::OpStorageError(std::string("deposit write-back failed: ") + e.what());
            }
            catch (...)
            {
                throw engine::OpStorageError("deposit write-back failed: unknown exception");
            }
        }
        // Read-path poison from runDeposit with applyDiff returning normally.
        if (stateView.poisoned())
            throw engine::OpStorageError("deposit write-back poisoned: " + stateView.firstError());
        co_return receipt;
    }

    /// OP block-level finalize (no block reward, via finalizeOpBlock).
    template <class Storage>
    task::Task<void> finalizeBlock(Storage& storage, protocol::BlockHeader const& blockHeader,
        ledger::LedgerConfig const& ledgerConfig)
    {
        namespace op = bcos::evm::opstack;

        checkForkRevision(ledgerConfig);

        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        // coinbase is a fixed 20-byte bcos::Address — the shared converter handles the
        // fixed-size copy; no silent zero-pad branch for a wrong-sized header field.
        auto const coinbase = bcos::evm::engine::detail::toEvmcAddress(blockHeader.coinbase());

        auto diff = op::finalizeOpBlock(stateView, m_forkConfig, coinbase);
        // finalizeOpBlock sanitizes the diff; applyDiff poisons AND rethrows.
        try
        {
            stateView.applyDiff(diff);
        }
        catch (const std::exception& e)
        {
            throw engine::OpStorageError(std::string("finalize write-back failed: ") + e.what());
        }
        catch (...)
        {
            throw engine::OpStorageError("finalize write-back failed: unknown exception");
        }
        if (stateView.poisoned())
            throw engine::OpStorageError("finalize write-back poisoned: " + stateView.firstError());
        co_return;
    }

private:
    // ---- Shared normal-tx pipeline: three stages (prepare/execute/finish). ----
    // Shared execution-path error ladder: typed engine errors and local misconfiguration pass
    // through; anything else (runDeposit/opTransition's block-level errors arrive as bare
    // std::runtime_error) is reclassified to the consensus-rejection channel (INVALID, never
    // -32603).
    [[noreturn]] static void rethrowExecError(std::string const& what)
    {
        try
        {
            throw;
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            throw;
        }
        catch (const engine::OpStorageError&)
        {
            throw;
        }
        catch (const OpEvmcRevisionNotConfigured&)
        {
            throw;  // local misconfiguration, not a consensus rejection
        }
        catch (const OpForkRevisionMismatch&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw bcos::evm::OpConsensusError("OpScheduler: " + what + " failed: " + e.what());
        }
        catch (...)
        {
            throw bcos::evm::OpConsensusError(
                "OpScheduler: " + what + " failed: unknown exception");
        }
    }

    // Fork/revision gate shared by the execute stages: ledger evmcRevision must be configured and
    // match this executor's fork. (m_finish only needs the configured check — the mismatch was
    // already rejected at prepare.)
    void checkForkRevision(ledger::LedgerConfig const& ledgerConfig) const
    {
        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(OpEvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        if (m_forkConfig.rev != *revOpt)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));
    }


    // Stage 1 — validate: fork/evmc revision check, block info + evmone tx + signed envelope, then
    // injection-style opValidate (props.fee snapshotted for the transition stage).
    template <class Storage>
    task::Task<bcos::evm::opstack::OpTxProperties> m_prepare(
        bcos::evm::evmstate::Storage2State<Storage>& stateView,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpFeeParams const& fee,
        int64_t blockGasLeft, std::optional<uint64_t> chainId,
        evmone::state::BlockInfo const* prebuiltBlockInfo, bool call)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        checkForkRevision(ledgerConfig);

        // BlockInfo is built once per tx by the caller (ExecuteContext::prepare /
        // executeTransaction) and shared with m_execute; built here only when not supplied.
        auto blockInfo = (prebuiltBlockInfo != nullptr) ?
                             *prebuiltBlockInfo :
                             buildBlockInfo(blockHeader,
                                 opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        auto evmTx = eth::toEvmoneTransaction(transaction);
        // TRUST BOUNDARY (envelope↔mirror gate, below): the execution fields come from the tars
        // mirror, so they are bound against the signed envelope by
        // envelopeExecutionFieldsMismatch — type byte, nonce, gasLimit, to, value, data are
        // fail-closed (OpConsensusError) on both the scheduler and block paths. NOT bound at
        // this head: sender (needs ecrecover), the fee fields, accessList, blobVersionedHashes,
        // authorizationList. The block path (chainId.has_value()) additionally rejects a
        // zero sender and a non-empty authorizationList; ecrecover of sender/auth signers is
        // part-5. Do not read this gate as "no execution path trusts an unbound mirror".
        // eth_call (call=true) simulates without fee constraints — op-geth's eth_call does
        // not enforce max_gas_price >= base_fee. A pricing-less call (e.g. the RPC default
        // 2 gwei cap) would fail MAX_FEE_PER_GAS_TOO_LOW once the OP base fee exceeds it, so
        // clamp the cap to the block base fee for the simulation. Only a call that carries NO
        // explicit fee cap (gasPrice / maxFeePerGas both unset — the pricing-less shape) gets
        // clamped: a caller that explicitly requested a cap below the base fee keeps it, and
        // opValidate's FEE_CAP_LESS_THAN_BLOCKS (evmone state.cpp) then rejects it exactly like
        // op-geth's ErrFeeCapTooLow instead of silently simulating at an unrequested price.
        if (call && !transaction.gasPrice().has_value() && !transaction.maxFeePerGas().has_value())
        {
            evmTx.max_gas_price = std::max(evmTx.max_gas_price, intx::uint256{blockInfo.base_fee});
        }
        // Block path: compare the SIGNED envelope to the node (op-geth ErrInvalidChainId).
        // Never use tars data.chainID / evmTx.chain_id — the signature does not bind the mirror.
        // eth_call passes nullopt (lenient, like op-geth eth_call).
        if (chainId.has_value())
        {
            if (auto missing = blockPathZeroSender(evmTx.sender))
            {
                throw bcos::evm::OpConsensusError("op block: " + *missing);
            }
            if (auto gate = envelopeChainIdMismatch(transaction, *chainId))
            {
                throw bcos::evm::OpConsensusError("op block: " + *gate);
            }
            // Fail-closed mirror↔envelope cross-check: execution fields (nonce/gasLimit/
            // to/value/data) must match the SIGNED envelope, never the forgeable mirror. Runs
            // before blockPathUnboundAuthorizationList so evmTx.type is envelope-bound when the
            // 7702 gate reads it (same order as processOpBlock).
            if (auto mismatch = envelopeExecutionFieldsMismatch(transaction, evmTx))
            {
                throw bcos::evm::OpConsensusError(
                    "op block: tx execution fields diverge from the signed envelope: " + *mismatch);
            }
            if (auto unbound = blockPathUnboundAuthorizationList(evmTx))
            {
                throw bcos::evm::OpConsensusError("op block: " + *unbound);
            }
        }
        auto envRef = transaction.extraTransactionBytes();
        evmc::bytes_view env{envRef.data(), envRef.size()};

        // eth_call/estimateGas USUALLY carries no signed envelope, but that is not an
        // invariant: for OP headers (baseFee present) CallRequest takes the TARS path and
        // DOES store an envelope (CallRequest.cpp buildCallTransaction, via
        // takeToTarsTransaction). Treat empty as the common case, not a certainty — the
        // guard below synthesizes a sizing envelope only when none is present. The
        // envelope is used only for L1-cost / calldata sizing (computeL1Cost /
        // flzCompressLen / bedrockCalldataGasUsed) — estimates on a simulation, not a
        // correctness precondition — so failing closed on it would reject every eth_call.
        // Re-encoding the executing transaction into its EIP-2718 form keeps the estimates
        // self-consistent with what is simulated. The block path (call=false) keeps the
        // raw signed envelope: there it is the trust anchor for the mirror↔envelope
        // cross-check and must never be synthesized.
        bcos::bytes synthesizedEnvelope;
        if (call && env.empty())
        {
            synthesizedEnvelope = synthesizeCallSizingEnvelope(evmTx);
            env = evmc::bytes_view{synthesizedEnvelope.data(), synthesizedEnvelope.size()};
        }

        // eth_call/estimateGas wrap the view so the sender reports uint256::max() — that
        // lets validate_transaction and the OP 512-bit cap both run (and pass) for an
        // unfunded simulated sender. Skipping the cap used to leave INSUFFICIENT_FUNDS
        // and opTransition's unchecked subtractions unguarded. Block path uses the raw view.
        // The same masked view is handed to opTransition, so the fabricated balance is also
        // visible to the EVM (BALANCE/SELFBALANCE/value transfers, and EXTCODEHASH for a
        // fresh sender) and lands in the simulated StateDiff — m_finish discards that diff for
        // call=true so none of it is written back. This is a decision on record, not an
        // accident (OpTransition.h CallSimulationView doc block).
        auto validated = call ? op::opValidate(op::CallSimulationView{stateView, evmTx.sender},
                                    blockInfo, evmTx, env, m_forkConfig, fee, blockGasLeft) :
                                op::opValidate(stateView, blockInfo, evmTx, env, m_forkConfig, fee,
                                    blockGasLeft);
        if (auto const* err = std::get_if<std::error_code>(&validated))
        {
            // DEBUG not WARNING: this path is reachable from unauthenticated eth_call /
            // estimateGas, where any caller can trigger validation failures at will — a
            // WARNING here would be a log-amplification vector.
            BCOS_LOG(DEBUG) << LOG_BADGE("OPSTACK") << LOG_DESC("opValidate failed")
                            << LOG_KV("reason", err->message())
                            << LOG_KV(
                                   "sender", bcos::toHex(std::span<uint8_t const>(
                                                 evmTx.sender.bytes, sizeof(evmTx.sender.bytes))))
                            << LOG_KV("nonce", evmTx.nonce) << LOG_KV("call", call);
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(err->message()));
        }
        auto props = std::move(std::get<op::OpTxProperties>(validated));
        props.evm_tx = std::move(evmTx);  // carry the built tx to m_execute (build once, not twice)
        co_return props;
    }

    // Stage 2 — execute: injection-style opTransition reusing props.fee (the validate-time
    // snapshot), so the pair can never be fed different OpFeeParams.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_execute(
        bcos::evm::evmstate::Storage2State<Storage>& stateView,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpTxProperties const& props,
        evmone::state::StateDiff& diff, uint64_t chainId, int64_t blockGasLeft,
        evmone::state::BlockHashes const* blockHashes,
        evmone::state::BlockInfo const* prebuiltBlockInfo, bool call)
    {
        namespace op = bcos::evm::opstack;

        (void)ledgerConfig;
        auto blockInfo = (prebuiltBlockInfo != nullptr) ?
                             *prebuiltBlockInfo :
                             buildBlockInfo(blockHeader,
                                 opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        auto const& evmTx = props.evm_tx;

        NullBlockHashes nullBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : nullBlockHashes;
        // opTransition's block-level errors arrive as bare std::runtime_error (e.g.
        // OpTransition.cpp's "negative gas_used") — run them through the same ladder as the
        // deposit path so callers never see an unclassified escape.
        try
        {
            if (call)
            {
                op::CallSimulationView masked{stateView, evmTx.sender};
                co_return op::opTransition(masked, blockInfo, bh, evmTx, m_forkConfig, m_vm, props,
                    chainId, m_receiptFactory, diff);
            }
            co_return op::opTransition(stateView, blockInfo, bh, evmTx, m_forkConfig, m_vm, props,
                chainId, m_receiptFactory, diff);
        }
        catch (...)
        {
            rethrowExecError("tx execution");
        }
    }

    // Stage 3 — writeback: apply the transition's state diff to storage, return the final receipt.
    // `call` (eth_call / estimateGas) discards the diff: the simulation ran against a masked
    // view whose fabricated sender balance must never reach the caller's storage. The
    // poisoned() check still runs so a storage fault observed during the simulation surfaces as
    // OpStorageError rather than a silently "successful" dry run.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_finish(Storage& storage,
        protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
        protocol::TransactionReceipt::Ptr receipt, evmone::state::StateDiff const& diff, bool call)
    {
        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(OpEvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));

        // opTransition sanitizes the diff at source (OpTransition.cpp), satisfying applyDiff's
        // precondition. applyDiff poisons AND rethrows raw; every write-back failure is a local
        // storage fault, so it must leave as OpStorageError (-32603), never INVALID.
        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        if (!call)
        {
            try
            {
                stateView.applyDiff(diff);
            }
            catch (const std::exception& e)
            {
                throw engine::OpStorageError(std::string("tx write-back failed: ") + e.what());
            }
            catch (...)
            {
                throw engine::OpStorageError("tx write-back failed: unknown exception");
            }
        }
        // Read-path poison from the transition with applyDiff returning normally (the shared
        // slot aggregates per-tx instances).
        if (stateView.poisoned())
            throw engine::OpStorageError("tx write-back poisoned: " + stateView.firstError());
        co_return std::move(receipt);
    }

    /// Build DepositTx from the signed 0x7E envelope, never from tars mirrors.
public:
    static bcos::evm::opstack::DepositTx depositFromTransaction(protocol::Transaction const& tx)
    {
        return decodeDepositEnvelope(tx.extraTransactionBytes());
    }

private:
    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    // Unused; write-back is Storage2State::applyDiff.
    [[maybe_unused]] crypto::Hash::Ptr m_hashImpl;
    // Value copy, not a reference: OpForkConfig is small (~32B, once per block) and a reference
    // member to a caller's config is the same lifetime footgun class that m_ctx had.
    bcos::evm::opstack::OpForkConfig m_forkConfig;
    /// Block-wide storage-error slot (op-geth dbErr): shared by every Storage2State this
    /// executor constructs so per-tx read errors surface at the block-level check.
    std::shared_ptr<SharedErrorSlot> m_sharedError;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
