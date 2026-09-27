/// @file OpEnvelopeCheck.cpp
/// @brief Implementation of the envelope↔mirror cross-checks declared in
///        OpEnvelopeCheck.h. The RLP walker is ported verbatim from
///        OpstackExecutor.h (envelopeExecutionFieldsMismatch :456-790 and the
///        bind/read helpers above it); the mirror side now reads
///        protocol::Transaction accessors instead of an
///        evmone::state::Transaction. Error strings are kept byte-identical
///        to the legacy gate so rejection classification cannot drift.

#include <opstack-executor/OpEnvelopeCheck.h>

#include <ethereum-executor/EthereumHost.h>  // eth::ethSender / ethMaxGasPrice
#include <opstack-executor/OpEthDeposit.h>   // opeth_deposit_detail::integerPayloadLength
#include <bcos-codec/rlp/RLPDecode.h>        // tryDecodeHeader / decode / captureRlp
#include <bcos-framework/protocol/TxGasModel.h>  // protocol::ethToAddress
#include <bcos-rlp-protocol/Web3Transaction.h>   // rpc::AuthorizationListEntry decode (EIP-7702 bind)
#include <bcos-rlp-protocol/Web3TxEnvelope.h>    // isTypedWeb3Envelope / classifyWeb3EnvelopeChainId
#include <bcos-utilities/DataConvertUtility.h>   // safeFromQuantity
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace bcos::executor_v1::opstack
{
namespace
{
namespace rlp = bcos::codec::rlp;
using opeth_deposit_detail::integerPayloadLength;

/// Read one fixed-width RLP string item from `walker` into `out`:
/// tryDecodeHeader plus an exact-width check, then crop. Returns false when
/// the item is missing, a list, or not exactly sizeof(out) payload bytes; the
/// caller maps the failure to its own error message. (Ported verbatim.)
template <typename Out>
[[nodiscard]] bool readFixedRlpItem(bcos::bytesRef& walker, Out& out)
{
    auto header = rlp::tryDecodeHeader(walker);
    if (!header || header->isList || header->payloadLength != sizeof(out.bytes) ||
        header->payloadLength > walker.size())
        return false;
    std::memcpy(out.bytes, walker.data(), sizeof(out.bytes));
    walker = walker.getCroppedData(header->payloadLength);
    return true;
}

/// Decode the envelope's accessList item (EIP-2930 shape: [[address20,
/// [keys32...]],...]) and require element-wise equality with the mirror the
/// executor will run. (Ported; mirror is now protocol::Web3AccessList.)
[[nodiscard]] std::optional<std::string> bindEnvelopeAccessList(
    bcos::bytesConstRef listPayload, bool isList, protocol::Web3AccessList const& mirror)
{
    if (!isList)
        return "accessList field is an RLP string";
    bcos::bytesRef walker(const_cast<bcos::byte*>(listPayload.data()), listPayload.size());
    size_t envEntries = 0;
    while (!walker.empty())
    {
        auto entryHeader = rlp::tryDecodeHeader(walker);
        if (!entryHeader || !entryHeader->isList || entryHeader->payloadLength > walker.size())
            return "accessList entry is malformed";
        bcos::bytesRef entry = walker.getCroppedData(0, entryHeader->payloadLength);
        walker = walker.getCroppedData(entryHeader->payloadLength);

        evmc::address addr{};
        if (!readFixedRlpItem(entry, addr))
            return "accessList address is malformed";

        auto keysHeader = rlp::tryDecodeHeader(entry);
        if (!keysHeader || !keysHeader->isList || keysHeader->payloadLength > entry.size())
            return "accessList storageKeys is malformed";
        bcos::bytesRef keys = entry.getCroppedData(0, keysHeader->payloadLength);
        std::vector<evmc::bytes32> storageKeys;
        while (!keys.empty())
        {
            evmc::bytes32 key{};
            if (!readFixedRlpItem(keys, key))
                return "accessList storage key is malformed";
            storageKeys.push_back(key);
        }
        if (envEntries >= mirror.size() ||
            !std::equal(mirror[envEntries].account.begin(), mirror[envEntries].account.end(),
                addr.bytes) ||
            mirror[envEntries].storageKeys.size() != storageKeys.size())
            return "accessList is not bound to the signed envelope";
        for (size_t i = 0; i < storageKeys.size(); ++i)
        {
            if (!std::equal(mirror[envEntries].storageKeys[i].begin(),
                    mirror[envEntries].storageKeys[i].end(), storageKeys[i].bytes))
                return "accessList mismatch";
        }
        ++envEntries;
    }
    if (envEntries != mirror.size())
        return "accessList is not bound to the signed envelope";
    return std::nullopt;
}

/// Decode the envelope's blobVersionedHashes item (list of 32-byte strings)
/// and require element-wise equality with the mirror. (Ported; mirror is now
/// protocol::VersionedHashes.)
[[nodiscard]] std::optional<std::string> bindEnvelopeBlobHashes(
    bcos::bytesConstRef listPayload, bool isList, protocol::VersionedHashes const& mirror)
{
    if (!isList)
        return "blobVersionedHashes field is an RLP string";
    bcos::bytesRef walker(const_cast<bcos::byte*>(listPayload.data()), listPayload.size());
    size_t count = 0;
    while (!walker.empty())
    {
        evmc::bytes32 hash{};
        if (!readFixedRlpItem(walker, hash))
            return "blobVersionedHashes entry is malformed";
        if (count >= mirror.size() ||
            !std::equal(mirror[count].begin(), mirror[count].end(), hash.bytes))
            return "blobVersionedHashes is not bound to the signed envelope";
        ++count;
    }
    if (count != mirror.size())
        return "blobVersionedHashes is not bound to the signed envelope";
    return std::nullopt;
}

/// Full authorizationList bind for 0x04: decode each signed tuple from the
/// envelope and require element-wise equality with the mirror on
/// chain_id/address/nonce/yParity/r/s. The mirror's signer field is not
/// compared: processAuthorizationList (ethereum-executor EthereumTransition.h)
/// ignores Authorization::signer and always ecrecovers from these six bound
/// fields. (Ported; mirror is now protocol::AuthorizationList.)
[[nodiscard]] std::optional<std::string> bindEnvelopeAuthorizationList(
    bcos::bytesConstRef listPayload, bool isList, protocol::AuthorizationList const& mirror)
{
    if (!isList)
        return "authorizationList field is an RLP string";
    bcos::bytesRef walker(const_cast<bcos::byte*>(listPayload.data()), listPayload.size());
    size_t count = 0;
    while (!walker.empty())
    {
        bcos::rpc::AuthorizationListEntry entry{};
        if (!rlp::captureRlp([&] { rlp::decode(walker, entry); }))
            return "authorizationList entry is malformed";
        if (count >= mirror.size())
            return "authorizationList is not bound to the signed envelope";
        const auto& m = mirror[count];
        if (entry.chainId != bcos::u256(m.chainId))
            return "authorizationList is not bound to the signed envelope";
        if (entry.address != m.address)
            return "authorizationList is not bound to the signed envelope";
        if (entry.nonce != m.nonce)
            return "authorizationList is not bound to the signed envelope";
        if (bcos::u256{entry.yParity} != bcos::u256(m.v))
            return "authorizationList is not bound to the signed envelope";
        if (entry.r != m.r)
            return "authorizationList is not bound to the signed envelope";
        if (entry.s != m.s)
            return "authorizationList is not bound to the signed envelope";
        ++count;
    }
    if (count != mirror.size())
        return "authorizationList is not bound to the signed envelope";
    return std::nullopt;
}
}  // namespace

/// (legacy = bare list; typed 0x01..0x04 = type byte + list, field order per
/// EIP-2718/2930/1559). BOUND COVERAGE: type byte, nonce, gasLimit, to, value,
/// data, and FULL element-wise binds of accessList (0x01 idx 7, 0x02/0x03/0x04
/// idx 8) and blobVersionedHashes (0x03 idx 10; 0x02 only when the
/// 4844-in-1559 extension is present, i.e. >= 14 items). Empty == empty stays
/// legal. Fee fields (gasPrice / maxPriorityFeePerGas / maxFeePerGas /
/// maxFeePerBlobGas) are bound against the signed envelope. Sender
/// (ecrecover) stays unbound; the block path rejects a missing sender and a
/// non-empty authorizationList on non-0x04 txs (the two helpers below).
std::optional<std::string> opEthEnvelopeExecutionFieldsMismatch(
    bcos::bytesConstRef extraBytes, protocol::Transaction const& tx)
{
    namespace protocol = bcos::rlp::protocol;
    if (extraBytes.empty())
        return "empty extraTransactionBytes";

    bcos::bytesRef cursor(const_cast<bcos::byte*>(extraBytes.data()), extraBytes.size());
    bool const typed = protocol::isTypedWeb3Envelope(extraBytes);
    // The type byte comes from the ENVELOPE, never the forgeable mirror; the
    // mirror-derived web3TypedTxKind must agree with it (a 0x02 envelope with a
    // legacy mirror would otherwise pass the field checks yet execute with
    // legacy fee semantics and a divergent receipts-root leaf).
    uint8_t const envelopeKind = typed ? static_cast<uint8_t>(extraBytes[0]) : uint8_t{0};
    if (envelopeKind != tx.web3TypedTxKind())
        return "tx type mismatch (envelope vs mirror)";
    if (typed)
    {
        cursor = cursor.getCroppedData(1);  // drop the EIP-2718 type byte
    }
    auto header = rlp::tryDecodeHeader(cursor);
    if (!header || !header->isList || header->payloadLength > cursor.size())
        return "unparseable envelope list";
    bcos::bytesRef walker(cursor.data(), header->payloadLength);

    // Execution-field indices per shape (chainId is checked separately):
    // legacy: [nonce, gasPrice, gasLimit, to, value, data,...]
    // 0x01 (2930): [chainId, nonce, gasPrice, gasLimit, to, value, data, accessList]
    // 0x02/03/04: [chainId, nonce, prio, maxFee, gasLimit, to, value, data,...]
    size_t const nonceIdx = typed ? 1 : 0;
    size_t const gasIdx = typed ? (envelopeKind == 0x01 ? 3 : 4) : 2;
    size_t const toIdx = typed ? (envelopeKind == 0x01 ? 4 : 5) : 3;
    size_t const valueIdx = typed ? (envelopeKind == 0x01 ? 5 : 6) : 4;
    size_t const dataIdx = typed ? (envelopeKind == 0x01 ? 6 : 7) : 5;

    // Walk once, capturing each target item: whole item (header + payload) for
    // the uint fields (with the payload width, so this gate can report
    // "nonce/value over-wide" instead of a generic decode failure), bare
    // payload for the byte fields.
    std::optional<bcos::bytesRef> nonceItem, gasItem, valueItem;
    std::optional<size_t> noncePlen, gasPlen, valuePlen;
    std::optional<bcos::bytesRef> gasPriceItem, prioFeeItem, maxFeeItem, blobFeeItem;
    std::optional<size_t> gasPricePlen, prioFeePlen, maxFeePlen, blobFeePlen;
    std::optional<bcos::bytesRef> toPayload, dataPayload;
    std::optional<bcos::bytesRef> accessListPayload, blobPayload, authorizationListPayload;
    bool nonceIsList = false;
    bool gasIsList = false;
    bool valueIsList = false;
    bool gasPriceIsList = false;
    bool prioFeeIsList = false;
    bool maxFeeIsList = false;
    bool blobFeeIsList = false;
    bool toIsList = false;
    bool dataIsList = false;
    bool accessListIsList = false;
    bool blobIsList = false;
    bool authorizationListIsList = false;
    // Bind field indices: accessList at 7 (0x01) / 8 (0x02/0x03/0x04); 0x7e deposits and
    // legacy carry no accessList field. blobVersionedHashes candidate at 10
    // (0x02/0x03 — only consumed for 0x03, or 0x02 with the 4844 extension).
    constexpr size_t c_noField = std::numeric_limits<size_t>::max();
    size_t const accessListIdx =
        !typed || envelopeKind == 0x7e ? c_noField : (envelopeKind == 0x01 ? 7 : 8);
    size_t const blobIdx = envelopeKind == 0x02 || envelopeKind == 0x03 ? 10 : c_noField;
    size_t const authorizationListIdx = envelopeKind == 0x04 ? 9 : c_noField;
    // Fee indices. Deposits have none. Sender stays unbound (ecrecover).
    size_t const gasPriceIdx = envelopeKind == 0x7e ?
                                   c_noField :
                                   (!typed ? size_t{1} : (envelopeKind == 0x01 ? 2 : c_noField));
    size_t const prioFeeIdx = typed && envelopeKind >= 0x02 && envelopeKind <= 0x04 ? 2 : c_noField;
    size_t const maxFeeIdx = typed && envelopeKind >= 0x02 && envelopeKind <= 0x04 ? 3 : c_noField;
    // maxFeePerBlobGas lives at idx 9: always for 0x03; for 0x02 only with the
    // EIP-4844-in-1559 extension (>= 14 items), which is known only after the
    // walk — capture it for both and bind it under the blobFieldsPresent
    // condition below.
    size_t const blobFeeIdx = (envelopeKind == 0x02 || envelopeKind == 0x03) ? 9 : c_noField;
    size_t idx = 0;
    while (!walker.empty())
    {
        auto const itemStart = walker;
        auto itemHeaderOpt = rlp::tryDecodeHeader(walker);
        if (!itemHeaderOpt || itemHeaderOpt->payloadLength > walker.size())
            return "malformed envelope item";
        auto const itemHeader = *itemHeaderOpt;
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
        if (idx == gasPriceIdx)
        {
            gasPriceItem = wholeItem;
            gasPricePlen = itemHeader.payloadLength;
            gasPriceIsList = itemHeader.isList;
        }
        if (idx == prioFeeIdx)
        {
            prioFeeItem = wholeItem;
            prioFeePlen = itemHeader.payloadLength;
            prioFeeIsList = itemHeader.isList;
        }
        if (idx == maxFeeIdx)
        {
            maxFeeItem = wholeItem;
            maxFeePlen = itemHeader.payloadLength;
            maxFeeIsList = itemHeader.isList;
        }
        if (idx == blobFeeIdx)
        {
            blobFeeItem = wholeItem;
            blobFeePlen = itemHeader.payloadLength;
            blobFeeIsList = itemHeader.isList;
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
        if (idx == accessListIdx)
        {
            accessListPayload = payload;
            accessListIsList = itemHeader.isList;
        }
        if (idx == blobIdx)
        {
            blobPayload = payload;
            blobIsList = itemHeader.isList;
        }
        if (idx == authorizationListIdx)
        {
            authorizationListPayload = payload;
            authorizationListIsList = itemHeader.isList;
        }
        walker = walker.getCroppedData(itemHeader.payloadLength);
        ++idx;
    }
    if (!nonceItem || !gasItem || !valueItem || !toPayload || !dataPayload ||
        (gasPriceIdx != c_noField && !gasPriceItem) || (prioFeeIdx != c_noField && !prioFeeItem) ||
        (maxFeeIdx != c_noField && !maxFeeItem) ||
        // 0x03 must carry maxFeePerBlobGas at idx 9; 0x02 carries it only with the 4844
        // extension, checked at the bind site once the item count is known.
        (envelopeKind == 0x03 && !blobFeeItem) ||
        (typed && envelopeKind != 0x7e && !accessListPayload) ||
        // A type-0x03 envelope must carry blobVersionedHashes (idx 10): without this arm a
        // 9/10-item 0x03 passed the guard and dereferenced a disengaged blobPayload below.
        (typed && envelopeKind == 0x03 && !blobPayload) ||
        (typed && envelopeKind == 0x04 && !authorizationListPayload))
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
        if (!rlp::captureRlp([&] { rlp::decode(*nonceItem, envNonce); }))
            return "nonce decode failed";
        // The mirror's nonce is a hex-quantity string (the same decode the
        // executor applies — eth::effectiveNonce).
        if (bcos::safeFromQuantity(tx.nonce()).value_or(0) != envNonce)
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
        if (!rlp::captureRlp([&] { rlp::decode(*gasItem, envGas); }))
            return "gasLimit decode failed";
        if (static_cast<uint64_t>(tx.gasLimit()) != envGas)
            return "gasLimit mismatch";
    }
    // Fee fields: bind the signed envelope to the mirror the executor will charge.
    {
        auto bindUint256 = [](char const* name, bool isList,
                               std::optional<bcos::bytesRef> const& item,
                               std::optional<size_t> const& plen,
                               bcos::u256 const& mirror) -> std::optional<std::string> {
            if (!item)
                return std::string(name) + " missing from envelope";
            if (isList)
                return std::string(name) + " field is an RLP list";
            if (!integerPayloadLength(*item).has_value())
                return std::string(name) + " is not a canonical integer";
            if (*plen > sizeof(bcos::u256))
                return std::string(name) + " over-wide";
            bcos::u256 env{};
            bcos::bytesRef cursor = *item;
            if (!rlp::captureRlp([&] { rlp::decode(cursor, env); }))
                return std::string(name) + " decode failed";
            if (env != mirror)
                return std::string(name) + " mismatch";
            return std::nullopt;
        };
        // The effective-price accessor the executor charges
        // (eth::ethMaxGasPrice): maxFeePerGas when present, else gasPrice.
        eth::EthCallParams const noCall{};
        if (gasPriceIdx != c_noField)
        {
            if (auto err = bindUint256("gasPrice", gasPriceIsList, gasPriceItem, gasPricePlen,
                    eth::ethMaxGasPrice(tx, noCall)))
                return err;
        }
        if (prioFeeIdx != c_noField)
        {
            if (auto err = bindUint256("maxPriorityFeePerGas", prioFeeIsList, prioFeeItem,
                    prioFeePlen, tx.maxPriorityFeePerGas().value_or(0)))
                return err;
        }
        if (maxFeeIdx != c_noField)
        {
            if (auto err = bindUint256("maxFeePerGas", maxFeeIsList, maxFeeItem, maxFeePlen,
                    eth::ethMaxGasPrice(tx, noCall)))
                return err;
        }
        // 0x03 always binds; 0x02 only with the 4844 extension (>= 14 items). Without the
        // extension idx 9 is yParity and must not be read as a fee.
        if (blobFeeIdx != c_noField && (envelopeKind == 0x03 || idx >= 14))
        {
            if (auto err = bindUint256("maxFeePerBlobGas", blobFeeIsList, blobFeeItem, blobFeePlen,
                    tx.maxFeePerBlobGas().value_or(0)))
                return err;
        }
    }
    // to (20-byte address, or empty for contract creation). protocol::ethToAddress
    // applies the same hex/raw decoding the executor's message builder uses.
    {
        if (toIsList)
            return "to field is an RLP list";
        auto const mirrorTo = bcos::protocol::ethToAddress(tx);
        if (mirrorTo.has_value())
        {
            if (toPayload->size() != sizeof(evmc_address) ||
                !std::equal(toPayload->begin(), toPayload->end(), mirrorTo->bytes))
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
        if (*valuePlen > sizeof(bcos::u256))
            return "value over-wide";
        bcos::u256 envValue{};
        if (!rlp::captureRlp([&] { rlp::decode(*valueItem, envValue); }))
            return "value decode failed";
        if (envValue != tx.value())
            return "value mismatch";
    }
    // data
    {
        if (dataIsList)
            return "data field is an RLP list";
        auto const mirrorData = tx.input();
        if (mirrorData.size() != dataPayload->size() ||
            !std::equal(mirrorData.begin(), mirrorData.end(), dataPayload->begin()))
            return "data mismatch";
    }
    // Full accessList / blobVersionedHashes bind: decode the envelope's own lists and require
    // element-wise equality with the mirror (both directions — a mirror stripped against a
    // non-empty envelope list previously passed and executed the signed envelope without its
    // warm slots / blob accounting, and a non-empty mirror list was blanket-rejected even when
    // it faithfully echoed the envelope). Empty == empty stays legal. Shapes without the field
    // (legacy) still reject a non-empty mirror list.
    auto const mirrorAccessList = tx.web3AccessList();
    if (accessListPayload)
    {
        if (auto err = bindEnvelopeAccessList(*accessListPayload, accessListIsList, mirrorAccessList))
            return err;
    }
    else if (!mirrorAccessList.empty())
    {
        return "accessList is not bound to the signed envelope";
    }
    // blobVersionedHashes: 0x03 always has the field; 0x02 only with the 4844 extension
    // (>= 14 items — below that idx 10 is the signature and must not be read). 0x01/0x04
    // have no blob fields at all.
    auto const mirrorBlobHashes = tx.blobVersionedHashes();
    bool const blobFieldsPresent = envelopeKind == 0x03 || (envelopeKind == 0x02 && idx >= 14);
    if (blobFieldsPresent)
    {
        if (auto err = bindEnvelopeBlobHashes(*blobPayload, blobIsList, mirrorBlobHashes))
            return err;
    }
    else if (!mirrorBlobHashes.empty())
    {
        return "blobVersionedHashes is not bound to the signed envelope";
    }
    auto const mirrorAuthorizationList = tx.authorizationList();
    if (envelopeKind == 0x04)
    {
        if (auto err = bindEnvelopeAuthorizationList(
                *authorizationListPayload, authorizationListIsList, mirrorAuthorizationList))
            return err;
    }
    else if (!mirrorAuthorizationList.empty())
    {
        return "authorizationList is not bound to the signed envelope";
    }
    return std::nullopt;
}

std::optional<std::string> opEthEnvelopeChainIdMismatch(
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

std::optional<std::string> opEthBlockPathZeroSender(protocol::Transaction const& tx)
{
    if (eth::ethSender(tx) == evmc::address{})
        return "empty sender";
    return std::nullopt;
}

std::optional<std::string> opEthBlockPathUnboundAuthorizationList(protocol::Transaction const& tx)
{
    if (tx.web3TypedTxKind() != 0x04 && !tx.authorizationList().empty())
        return "authorizationList is not bound to the signed envelope";
    return std::nullopt;
}
}  // namespace bcos::executor_v1::opstack
