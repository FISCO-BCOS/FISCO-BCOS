/// @file OpstackExecutor.h
/// @brief OP Stack (Optimism L2) transaction executor based on bcos-evm/opstack.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept (ExecuteContext with
/// prepare/execute/finish): opValidate + opTransition for NORMAL transactions, runDeposit for
/// 0x7E deposits, finalizeOpBlock for block finalize. The caller passes an already-decoded
/// DepositTx. Storage-backed StateView and state-diff writeback are shared with EthereumExecutor
/// via ethereum-executor.

#pragma once

#include "bcos-evm/opstack/OpFeeParams.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpTransition.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include "opstack-executor/OpCommon.h"  // detail::narrowU256ToU64 / toEvmcAddress / toEvmcBytes32
#include "opstack-executor/Storage2State.h"  // eth::applyStateDiff / ZeroBlockHashes
#include <bcos-codec/rlp/Common.h>           // BYTES_HEAD_BASE (consensus deposit-envelope decode)
#include <bcos-codec/rlp/RLPDecode.h>        // decodeHeader / decode / decodeItems
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <algorithm>
#include <evmc/evmc.hpp>
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
// toIntxU256 lives in bcos::executor_v1::eth::evm (EVMSupport.h); re-import so
// toEvmoneTransaction's unqualified calls resolve (the deleted StorageStateView.h previously
// carried a bcos::executor_v1::eth copy).
using evm::toIntxU256;

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
        break;
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
    if (sb.size() >= sizeof(evmc_address))
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
        try
        {
            std::size_t pos = 0;
            auto const v = std::stoull(std::string(s), &pos, 10);
            return pos == s.size() ? v : 0;  // reject trailing garbage ("10x")
        }
        catch (...)
        {
            return 0;
        }
    }();
    evmTx.nonce = bcos::safeFromQuantity(tx.nonce()).value_or(0);
    for (auto const& auth : tx.authorizationList())
    {
        evmone::state::Authorization ea{};
        // AuthorizationEntry: all fields are numeric (uint64_t, u256, Address, uint8_t)
        ea.chain_id = toIntxU256(bcos::u256(auth.chainId));
        std::copy_n(auth.address.begin(), sizeof(evmc_address), ea.addr.bytes);
        ea.nonce = auth.nonce;
        if (auth.signer.size() == sizeof(evmc_address))
        {
            evmc_address sa{};
            std::copy_n(auth.signer.begin(), sizeof(evmc_address), sa.bytes);
            ea.signer = sa;
        }
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

DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpForkRevisionMismatch);
DERIVE_BCOS_EXCEPTION(OpTxValidationFailed);

/// Strict 0x7E deposit envelope decode (consensus-grade, kyonRay review #5429 K3).
/// `0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx, data])`. Deposit fields fed
/// to execution are re-derived HERE from the signed envelope — never from the unauthenticated tars
/// mirrors (Transaction.tars field 8+) — because a peer-controlled mirror can mint arbitrary value
/// (deposits are unsigned by design; authenticity comes from the L1-derived envelope). Unlike the
/// RPC display-grade decoder, this rejects a non-0x7e type byte, malformed RLP, trailing bytes
/// after the list and over-wide fields. One RLP decode per deposit (blocks carry one or two).
[[nodiscard]] inline bcos::evm::opstack::DepositTx decodeDepositEnvelope(bcos::bytesConstRef env)
{
    namespace op = bcos::evm::opstack;
    namespace rlp = bcos::codec::rlp;
    auto fail = [](std::string const& msg) {
        BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(msg));
    };
    // Gate an integer RLP item for width AND canonicality (kyonRay review #5429 round 3):
    // rlp::decode(UnsignedIntegral) does not check the payload length against the target width —
    // fromBigEndian folds excess high bytes, so a 33-byte mint would silently truncate to its low
    // 32 bytes — and of the non-canonical forms it only rejects a single-byte payload < 0x80 (via
    // decodeHeader). op-geth's Stream.uint / decodeBigInt additionally return ErrCanonInt /
    // ErrCanonSize for the single Byte 0x00 (integer zero must be the empty item 0x80) and for any
    // multi-byte payload with a leading zero byte. Return the payload length of the integer at
    // `ref`'s front, or nullopt if it is not a canonical well-formed integer (RLP list / truncated
    // length prefix / non-canonical). FISCO's own RLPEncode always writes minimal encodings, so
    // only externally-supplied bytes are affected.
    auto integerPayloadLength = [&](bcos::bytesConstRef const& ref) -> std::optional<size_t> {
        if (ref.empty())
            return std::nullopt;
        uint8_t const b = ref[0];
        if (b < 0x80)
        {
            // Byte item: 0x00 is non-canonical (integer zero must be the empty item 0x80).
            return b == 0 ? std::nullopt : std::optional<size_t>{0};
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
            if (ref.size() < 1 + n + len)
                return std::nullopt;  // truncated payload
            if (len == 1 && ref[1 + n] < 0x80)
                return std::nullopt;  // single-byte payload < 0x80 must be a bare Byte
            if (len >= 2 && ref[1 + n] == 0)
                return std::nullopt;  // leading zero byte
            return len;               // decodeHeader rejects a long-string length < 56
        }
        return std::nullopt;  // list (not an integer)
    };
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
    // gas (uint64): width + canonicality check, then int64 range. A uint64 that exceeds
    // INT64_MAX would silently wrap dep.gas_limit's static_cast<int64_t>(gas) to -1 — reject at
    // the decoder, not downstream (kyonRay review #5429 round 3).
    if (auto pl = integerPayloadLength(items); !pl || *pl > 8)
        fail("deposit envelope: gas non-canonical or over-wide (>8 bytes)");
    if (auto e = rlp::decode(items, gas); e != nullptr)
        fail("deposit envelope: gas decode failed");
    if (gas > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        fail("deposit envelope: gas exceeds int64 range");
    // isSystemTx (uint64): width + canonicality check, then a 0/1 value check. op-geth's
    // decodeBool accepts only the empty item (false) and 0x01 (true) and errors on any other
    // value ("rlp: invalid boolean"). Decoded as uint64 — the FISCO bool overload demands
    // payloadLength == 1 and would reject the empty-item false — so the 0/1 check is explicit
    // (kyonRay review #5429 round 3).
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
                   std::optional<intx::uint256>{bcos::executor_v1::eth::evm::toIntxU256(*mint)} :
                   std::nullopt;
    dep.value = bcos::executor_v1::eth::evm::toIntxU256(value);
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
struct OpBlockExecutionContext
{
    mutable bcos::evm::opstack::OpFeeParams fee;   // lazy-load + DA scalar override (H1/H1c)
    mutable bool feeLoaded = false;                // fee lazy-load flag (H1)
    mutable int64_t blockGasLeft;                  // decremented per tx
    mutable int64_t cumulativeGasUsed = 0;         // accumulated across txs (H4)
    mutable bool seenNonDeposit = false;           // deposit-after-non-deposit gate (M2)
    evmone::state::BlockHashes* blockHashes;       // built once at block level (H3)
    uint64_t chainId;                              // constant (H3)
    std::optional<uint16_t> daFootprintGasScalar;  // Jovian DA scalar (H1c)
};

class OpstackExecutor
{
public:
    OpstackExecutor(protocol::TransactionReceiptFactory::Ptr receiptFactory,
        crypto::Hash::Ptr hashImpl,
        bcos::evm::opstack::OpForkConfig forkConfig = bcos::evm::opstack::jovianConfig(),
        std::shared_ptr<std::string> sharedError = {})
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_forkConfig(std::move(forkConfig)),
        m_sharedError(std::move(sharedError)),
        m_vm(evmc_create_evmone())
    {}

    /// The block-wide storage-error slot shared by every Storage2State this executor builds
    /// (op-geth's dbErr analogue): a read error in ANY per-tx execution instance poisons the
    /// shared slot, which the block-level stateRoot check then rejects.
    [[nodiscard]] const std::shared_ptr<std::string>& sharedError() const noexcept
    {
        return m_sharedError;
    }

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// eth_call block context, mirroring detail::toBlockInfo: lenient optionals (unset header
    /// fields read as 0 rather than throwing), gasLimit injected as blockGasLeft.
    static evmone::state::BlockInfo buildBlockInfo(
        protocol::BlockHeader const& header, uint64_t gasLimit)
    {
        return bcos::evm::engine::detail::toBlockInfo(header, gasLimit, /*lenientOptionals=*/true);
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
        bool call;

        // Per-transaction state threaded across the concept lifecycle.
        bcos::evm::opstack::OpTxProperties m_props;   // set by prepare()
        protocol::TransactionReceipt::Ptr m_receipt;  // set by execute()
        evmone::state::StateDiff m_diff;              // writeback deferred to finish()
        bcos::evm::opstack::DepositTx m_deposit;      // set by prepare() for deposit txs

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
            m_ctx(blockCtx)
        {}

        // concept lifecycle: prepare (validate) -> execute (transition) -> finish (writeback).
        // Deposit txs short-circuit (no opValidate / fee / m_finish writeback); normal txs run the
        // three shared stages with the block-context fee (lazily loaded) + blockGasLeft.
        task::Task<void> prepare()
        {
            if (m_ctx == nullptr)
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
            }
            if (transaction.isDepositTx())
            {
                if (m_ctx->seenNonDeposit)
                    // M2 order gate — demoted from a hard reject to an observable log (finding D
                    // #5429): op-geth/op-reth enforce deposit-first only at the sequencer
                    // (construction), not at validation, so a block with a deposit after a
                    // non-deposit is accepted by both reference clients. FISCO keeps the
                    // invariant observable (WARNING) without diverging on acceptance.
                    BCOS_LOG(WARNING) << LOG_BADGE("OPSTACK")
                                      << "deposit after non-deposit in block — accepted "
                                         "(deliberate demotion, op-geth/op-reth accept at "
                                         "validation)";
                try
                {
                    m_deposit = OpstackExecutor::depositFromTransaction(transaction);
                }
                catch (const OpTxValidationFailed& e)
                {
                    // kyonRay review #5429 #2: a malformed deposit envelope (e.g. a 0x02-envelope
                    // whose tars mirror claims deposit) is a CONSENSUS rejection, not an internal
                    // error — reclassify so the block surfaces as INVALID instead of -32603.
                    throw bcos::evm::engine::OpConsensusError(
                        std::string("OpScheduler: deposit envelope validation failed: ") +
                        e.what());
                }
                co_return;  // deposit has no opValidate
            }
            m_ctx->seenNonDeposit = true;
            if (!m_ctx->feeLoaded)
            {  // H1 fee lazy load (after the L1 attributes deposit has executed)
                namespace eth = bcos::executor_v1::eth;
                namespace op = bcos::evm::opstack;
                bcos::evm::evmstate::Storage2State<Storage> stateView(
                    storage, executor.sharedError());
                m_ctx->fee = op::loadOpFeeParams(stateView);
                if (m_ctx->daFootprintGasScalar)  // H1c DA scalar override
                    m_ctx->fee.da_footprint_gas_scalar = *m_ctx->daFootprintGasScalar;
                m_ctx->feeLoaded = true;
            }
            try
            {  // M1 normalization: validation failure -> consensus rejection
                m_props = co_await executor.m_prepare(storage, blockHeader, transaction,
                    ledgerConfig, m_ctx->fee, m_ctx->blockGasLeft, m_ctx->chainId);
            }
            catch (const OpTxValidationFailed& e)
            {
                throw bcos::evm::engine::OpConsensusError(
                    std::string("OpScheduler: normal tx validation failed: ") + e.what());
            }
        }
        task::Task<void> execute()
        {
            if (m_ctx == nullptr)
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
            }
            if (transaction.isDepositTx())
            {
                // executeDeposit member (not the op::runDeposit free function); applies the state
                // diff internally.
                m_receipt = co_await executor.executeDeposit(storage, blockHeader, m_deposit,
                    m_ctx->chainId, m_ctx->blockGasLeft, ledgerConfig, m_ctx->blockHashes);
            }
            else
            {
                m_receipt = co_await executor.m_execute(storage, blockHeader, transaction,
                    ledgerConfig, m_props, m_diff, m_ctx->chainId, m_ctx->blockGasLeft,
                    m_ctx->blockHashes);  // H3
            }
        }
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            if (m_ctx == nullptr)
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
            }
            namespace op = bcos::evm::opstack;
            protocol::TransactionReceipt::Ptr receipt;
            if (transaction.isDepositTx())
            {
                receipt = m_receipt;  // executeDeposit already applied the state diff
            }
            else
            {
                receipt = co_await executor.m_finish(
                    storage, blockHeader, ledgerConfig, m_receipt, m_diff);
            }
            // H4: sole owner of cumulative-gas backfill + blockGasLeft decrement (narrowGasUsed /
            // hexCumulative live in OpCommon.h).
            auto gasUsed = op::narrowGasUsed(receipt->gasUsed());
            m_ctx->cumulativeGasUsed += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(m_ctx->cumulativeGasUsed));
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

    /// Execute a single OP normal transaction (injection-style, mirroring processOpBlock).
    /// Orchestrator supplies fee, decrementing blockGasLeft, chainId, and real block hashes.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        bcos::evm::opstack::OpFeeParams fee = {}, int64_t blockGasLeft = 0, uint64_t chainId = 0,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        (void)contextID;

        if (transaction.isDepositTx())
        {
            auto dep = depositFromTransaction(transaction);
            co_return co_await executeDeposit(
                storage, blockHeader, dep, chainId, blockGasLeft, ledgerConfig, blockHashes);
        }

        // eth_call (call=true) simulates without chain binding — op-geth eth_call is lenient about
        // chainId; block execution (call=false) enforces tx.chainId == node chainId (the op-geth
        // EIP155Signer/modernSigner ErrInvalidChainId check).
        auto props = co_await m_prepare(
            storage, blockHeader, transaction, ledgerConfig, fee, blockGasLeft, call ? 0 : chainId);
        evmone::state::StateDiff diff;
        auto receipt = co_await m_execute(storage, blockHeader, transaction, ledgerConfig, props,
            diff, chainId, blockGasLeft, blockHashes);
        co_return co_await m_finish(storage, blockHeader, ledgerConfig, receipt, diff);
    }

    /// Execute a single OP 0x7E deposit transaction (reuses runDeposit).
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeDeposit(Storage& storage,
        protocol::BlockHeader const& blockHeader, bcos::evm::opstack::DepositTx const& dep,
        uint64_t chainId, int64_t blockGasLeft, ledger::LedgerConfig const& ledgerConfig,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;

        evmone::state::StateDiff diff;
        auto receipt = op::runDeposit(stateView, blockInfo, bh, dep, m_forkConfig, m_vm, chainId,
            blockGasLeft, m_receiptFactory, diff);
        co_await eth::applyStateDiff(storage, diff, rev, *m_hashImpl);
        co_return receipt;
    }

    /// OP block-level finalize (no block reward, via finalizeOpBlock).
    template <class Storage>
    task::Task<void> finalizeBlock(Storage& storage, protocol::BlockHeader const& blockHeader,
        ledger::LedgerConfig const& ledgerConfig)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        evmc_address coinbase{};
        auto const& cb = blockHeader.coinbase();
        if (cb.size() == sizeof(evmc_address))
            std::copy_n(cb.begin(), sizeof(evmc_address), coinbase.bytes);

        auto diff = op::finalizeOpBlock(stateView, m_forkConfig, coinbase);
        co_await eth::applyStateDiff(storage, diff, rev, *m_hashImpl);
    }

private:
    // ---- Shared normal-tx pipeline: three stages (prepare/execute/finish). ----
    // Stage 1 — validate: fork/evmc revision check, block info + evmone tx + signed envelope, then
    // injection-style opValidate (props.fee snapshotted for the transition stage).
    template <class Storage>
    task::Task<bcos::evm::opstack::OpTxProperties> m_prepare(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpFeeParams const& fee = {},
        int64_t blockGasLeft = 0, uint64_t chainId = 0)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        auto evmTx = eth::toEvmoneTransaction(transaction);
        // op-geth parity (morebtcg review #5429): reject txs whose chainId differs from the node's
        // chainId. op-geth EIP155Signer.Sender / modernSigner.Sender check
        // `tx.ChainId() == chainID` (ErrInvalidChainId) during block processing; FISCO previously
        // accepted any self-consistent chainId (the signature binds the tx's own chainId, so
        // sender recovery always succeeds). Only legacy UNPROTECTED txs (v=27/28, chain_id==0,
        // Homestead) have no chainId concept and are accepted; every other tx (EIP-155 protected
        // legacy + ALL typed txs) must match the node chainId — including a typed tx whose
        // chain_id field is 0, which modernSigner.Sender also rejects (0 != chainID →
        // ErrInvalidChainId; a malicious proposer can craft a 0x02 envelope with chain_id 0).
        // chainId == 0 here means the caller did not supply a node chainId (fail-open); the block
        // path always passes m_ctx->chainId.
        // Known residual (review #5429 finding K, DIVERGENCES.md phase-3): chain_id==0 also arises
        // from a v=35/36 EIP-155-protected legacy tx (chain id 0), which the tars layer collapses
        // onto the same "0" as v=27/28 — so a protected-chain-0 legacy tx is exempted here where
        // op-geth's Protected() rejects it (ErrInvalidChainId on any real chain). Nil security
        // impact (the signature is re-encodable as v=27/28, which both clients accept); full parity
        // needs a protected flag in the tars Transaction.
        if (chainId != 0)
        {
            if (evmTx.type == evmone::state::Transaction::Type::legacy)
            {
                if (evmTx.chain_id != 0 && evmTx.chain_id != chainId)  // EIP-155 protected legacy
                    throw bcos::evm::engine::OpConsensusError(
                        "OpScheduler: tx chain_id " + std::to_string(evmTx.chain_id) +
                        " does not match node chainId " + std::to_string(chainId));
            }
            else if (evmTx.chain_id != chainId)  // typed: chain_id==0 is NOT exempt
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpScheduler: tx chain_id " + std::to_string(evmTx.chain_id) +
                    " does not match node chainId " + std::to_string(chainId));
            }
        }
        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);
        auto envRef = transaction.extraTransactionBytes();
        evmc::bytes_view env{envRef.data(), envRef.size()};

        auto validated =
            op::opValidate(stateView, blockInfo, evmTx, env, m_forkConfig, fee, blockGasLeft);
        if (auto const* err = std::get_if<std::error_code>(&validated))
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(err->message()));
        auto props = std::move(std::get<op::OpTxProperties>(validated));
        props.evm_tx = std::move(evmTx);  // carry the built tx to m_execute (build once, not twice)
        co_return props;
    }

    // Stage 2 — execute: injection-style opTransition reusing props.fee (the validate-time
    // snapshot), so the pair can never be fed different OpFeeParams.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_execute(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpTxProperties const& props,
        evmone::state::StateDiff& diff, uint64_t chainId = 0, int64_t blockGasLeft = 0,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        (void)ledgerConfig;
        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        auto const& evmTx = props.evm_tx;  // reuse the prepare-built tx (review finding F)
        bcos::evm::evmstate::Storage2State<Storage> stateView(storage, m_sharedError);

        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;
        co_return op::opTransition(stateView, blockInfo, bh, evmTx, m_forkConfig, m_vm, props,
            chainId, m_receiptFactory, diff);
    }

    // Stage 3 — writeback: apply the transition's state diff to storage, return the final receipt.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_finish(Storage& storage,
        protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
        protocol::TransactionReceipt::Ptr receipt, evmone::state::StateDiff const& diff)
    {
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;

        co_await eth::applyStateDiff(storage, diff, rev, *m_hashImpl);
        co_return std::move(receipt);
    }

    /// Build a DepositTx from a protocol::Transaction whose isDepositTx() is true, re-deriving the
    /// deposit fields from the signed 0x7E envelope (tx.extraTransactionBytes) — NEVER from the
    /// unauthenticated tars mirrors (kyonRay review #5429 K3). The tars mirrors are display-only:
    /// a peer-controlled mirror can mint arbitrary value (deposits are unsigned by design;
    /// authenticity comes from the L1-derived envelope). On the engine newPayload path the mirrors
    /// happen to be self-consistent (opEnvelopeToTars derives them in-process), but with OP mode
    /// behind MultiVersionScheduler slot 3 any future wiring that feeds wire-decoded tars into
    /// OpScheduler must not be able to mint. The strict decode rejects envelope/mirror mismatch.
public:
    static bcos::evm::opstack::DepositTx depositFromTransaction(protocol::Transaction const& tx)
    {
        return decodeDepositEnvelope(tx.extraTransactionBytes());
    }

private:
    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    // Value copy, not a reference: OpForkConfig is small (~32B, once per block) and a reference
    // member to a caller's config is the same lifetime footgun class that m_ctx had.
    bcos::evm::opstack::OpForkConfig m_forkConfig;
    /// Block-wide storage-error slot (op-geth dbErr): shared by every Storage2State this
    /// executor constructs so per-tx read errors surface at the block-level check.
    std::shared_ptr<std::string> m_sharedError;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
