#include "Web3Eip7702Fill.h"
#include "Eip7702Delegation.h"
#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-framework/protocol/Web3AuthorizationList.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <charconv>
#include <system_error>

namespace bcos::executor
{
namespace
{
#define WEB3_EIP7702_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("WEB3_EIP7702")

std::optional<uint64_t> parseDecimalU64(std::string const& dec)
{
    if (dec.empty())
    {
        return std::nullopt;
    }
    uint64_t value = 0;
    auto const [ptr, ec] = std::from_chars(dec.data(), dec.data() + dec.size(), value);
    if (ec != std::errc{} || ptr != dec.data() + dec.size())
    {
        return std::nullopt;
    }
    return value;
}

bool authorizationListWithinLimit(size_t size) noexcept
{
    return size <= EIP7702_MAX_AUTHORIZATION_LIST_SIZE;
}

void fillAuthorizationFromWeb3(bcos::rpc::Web3Transaction const& w3, Web3Eip7702Parsed& out)
{
    out.web3TypedTxKind = static_cast<uint8_t>(w3.type);
    if (w3.type != bcos::rpc::TransactionType::EIP7702 || w3.authorizationList.empty())
    {
        out.authorizationList.reset();
        return;
    }
    if (!authorizationListWithinLimit(w3.authorizationList.size()))
    {
        WEB3_EIP7702_LOG(WARNING) << LOG_DESC("authorization_list exceeds max size; ignoring list")
                                  << LOG_KV("size", w3.authorizationList.size())
                                  << LOG_KV("max", EIP7702_MAX_AUTHORIZATION_LIST_SIZE);
        out.authorizationList.reset();
        return;
    }
    auto list = std::make_shared<Eip7702AuthorizationList>();
    list->reserve(w3.authorizationList.size());
    for (auto const& e : w3.authorizationList)
    {
        Eip7702Authorization auth;
        auth.chainId = e.chainId;
        auth.address = e.address;
        auth.nonce = e.nonce;
        auth.yParity = e.yParity;
        auth.r = e.r;
        auth.s = e.s;
        list->emplace_back(std::move(auth));
    }
    out.authorizationList = std::move(list);
}

void fillAuthorizationFromProtocol(
    bcos::protocol::Web3AuthorizationList const& src, Web3Eip7702Parsed& out)
{
    if (!authorizationListWithinLimit(src.size()))
    {
        WEB3_EIP7702_LOG(WARNING) << LOG_DESC(
                                         "Tars authorization_list exceeds max size; ignoring list")
                                  << LOG_KV("size", src.size())
                                  << LOG_KV("max", EIP7702_MAX_AUTHORIZATION_LIST_SIZE);
        out.authorizationList.reset();
        return;
    }
    auto list = std::make_shared<Eip7702AuthorizationList>();
    list->reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        auto const& e = src[i];
        auto const chainId = parseDecimalU64(e.chainIdDec);
        if (!chainId)
        {
            WEB3_EIP7702_LOG(WARNING)
                << LOG_DESC("Skipping authorization tuple with invalid chainIdDec")
                << LOG_KV("index", i) << LOG_KV("chainIdDec", e.chainIdDec);
            continue;
        }
        auto const nonce = parseDecimalU64(e.nonceDec);
        if (!nonce)
        {
            WEB3_EIP7702_LOG(WARNING)
                << LOG_DESC("Skipping authorization tuple with invalid nonceDec")
                << LOG_KV("index", i) << LOG_KV("nonceDec", e.nonceDec);
            continue;
        }
        Eip7702Authorization auth;
        auth.chainId = *chainId;
        auth.address = bcos::toAddress(e.addressHex);
        auth.nonce = *nonce;
        auth.yParity = e.yParity;
        auth.r = e.r;
        auth.s = e.s;
        list->emplace_back(std::move(auth));
    }
    if (list->empty())
    {
        out.authorizationList.reset();
        return;
    }
    out.authorizationList = std::move(list);
}

bool authorizationListsEqual(
    bcos::protocol::Web3AuthorizationList const& protocol, Eip7702AuthorizationList const& decoded)
{
    if (protocol.size() != decoded.size())
    {
        return false;
    }
    for (size_t i = 0; i < protocol.size(); ++i)
    {
        auto const& p = protocol[i];
        auto const& d = decoded[i];
        auto const chainId = parseDecimalU64(p.chainIdDec);
        auto const nonce = parseDecimalU64(p.nonceDec);
        if (!chainId || !nonce || *chainId != d.chainId || p.addressHex != d.address.hex() ||
            *nonce != d.nonce || p.yParity != d.yParity || p.r != d.r || p.s != d.s)
        {
            return false;
        }
    }
    return true;
}

void warnIfProtocolAuthorizationMismatch(protocol::Transaction const& tx,
    bcos::protocol::Web3AuthorizationList const& protocol, Web3Eip7702Parsed const& fromExtra)
{
    if (protocol.empty() || !fromExtra.authorizationList || fromExtra.authorizationList->empty())
    {
        return;
    }
    if (authorizationListsEqual(protocol, *fromExtra.authorizationList))
    {
        return;
    }
    WEB3_EIP7702_LOG(WARNING)
        << LOG_DESC(
               "Tars data.authorizationList disagrees with extraTransactionBytes RLP; using Tars")
        << LOG_KV("tarsEntries", protocol.size())
        << LOG_KV("extraEntries", fromExtra.authorizationList->size());
}

Web3Eip7702Parsed parseEip7702FromExtraBytes(protocol::Transaction const& tx)
{
    Web3Eip7702Parsed out;
    auto const extra = tx.extraTransactionBytes();
    if (extra.empty())
    {
        return out;
    }
    bcos::bytes extraCopy(extra.begin(), extra.end());
    bcos::bytesRef ref(extraCopy.data(), extraCopy.size());
    auto const envelope = static_cast<uint8_t>(extraCopy[0]);
    // Wire-form typed tx: 0x04 || rlp(payload). `encodeForSign()` also prefixes 0x04 (EIP-2718).
    if (envelope > 0 && envelope < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        if (envelope == static_cast<uint8_t>(bcos::rpc::TransactionType::EIP7702))
        {
            ref = ref.getCroppedData(1);
        }
        else if (envelope <= static_cast<uint8_t>(bcos::rpc::TransactionType::EIP4844))
        {
            out.web3TypedTxKind = envelope;
            return out;
        }
    }
    bcos::rpc::Web3Transaction w3{};
    if (auto const decodeError =
            bcos::codec::rlp::decodeFromPayload(ref, w3, bcos::rpc::TransactionType::EIP7702);
        decodeError != nullptr)
    {
        WEB3_EIP7702_LOG(WARNING)
            << LOG_DESC("Failed to decode Web3 extraTransactionBytes for authorization list")
            << LOG_KV("extraLen", extra.size()) << LOG_KV("msg", decodeError->errorMessage());
        return out;
    }
    fillAuthorizationFromWeb3(w3, out);
    return out;
}
}  // namespace

Web3Eip7702Parsed parseEip7702FromWeb3Transaction(protocol::Transaction const& tx)
{
    Web3Eip7702Parsed out;
    if (tx.type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return out;
    }

    auto const kind = tx.web3TypedTxKind();
    constexpr auto kEip7702Kind = static_cast<uint8_t>(bcos::rpc::TransactionType::EIP7702);

    if (kind == kEip7702Kind)
    {
        out.web3TypedTxKind = kind;
        auto const& list = tx.web3AuthorizationList();
        if (!list.empty())
        {
            fillAuthorizationFromProtocol(list, out);
            auto const fromExtra = parseEip7702FromExtraBytes(tx);
            warnIfProtocolAuthorizationMismatch(tx, list, fromExtra);
            return out;
        }
        auto fromExtra = parseEip7702FromExtraBytes(tx);
        if (fromExtra.authorizationList && !fromExtra.authorizationList->empty())
        {
            out.authorizationList = std::move(fromExtra.authorizationList);
        }
        return out;
    }

    if (kind != 0)
    {
        out.web3TypedTxKind = kind;
        return out;
    }

    return parseEip7702FromExtraBytes(tx);
}

}  // namespace bcos::executor
