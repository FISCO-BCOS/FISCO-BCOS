#include "Web3Eip2930Fill.h"
#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/Web3AccessList.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"

namespace bcos::executor
{
namespace
{
#define WEB3_EIP2930_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("WEB3_EIP2930")

void fillAccessListFromWeb3(bcos::rpc::Web3Transaction const& w3, Web3Eip2930Parsed& out)
{
    out.web3TypedTxKind = static_cast<uint8_t>(w3.type);
    if (w3.accessList.empty())
    {
        out.accessList.reset();
        return;
    }
    auto list = std::make_shared<Eip2930AccessList>();
    list->reserve(w3.accessList.size());
    for (auto const& e : w3.accessList)
    {
        std::vector<h256> keys = e.storageKeys;
        list->emplace_back(e.account.hex(), std::move(keys));
    }
    out.accessList = std::move(list);
}

void fillAccessListFromProtocol(bcos::protocol::Web3AccessList const& src, Web3Eip2930Parsed& out)
{
    auto list = std::make_shared<Eip2930AccessList>();
    list->reserve(src.size());
    for (auto const& e : src)
    {
        list->emplace_back(e.accountHex, e.storageKeys);
    }
    out.accessList = std::move(list);
}

bool accessListsEqual(
    bcos::protocol::Web3AccessList const& protocol, Eip2930AccessList const& decoded)
{
    if (protocol.size() != decoded.size())
    {
        return false;
    }
    for (size_t i = 0; i < protocol.size(); ++i)
    {
        auto const& p = protocol[i];
        auto const& d = decoded[i];
        if (p.accountHex != d.first)
        {
            return false;
        }
        if (p.storageKeys.size() != d.second.size())
        {
            return false;
        }
        for (size_t j = 0; j < p.storageKeys.size(); ++j)
        {
            if (p.storageKeys[j] != d.second[j])
            {
                return false;
            }
        }
    }
    return true;
}

void warnIfProtocolAccessListMismatch(protocol::Transaction const& tx,
    bcos::protocol::Web3AccessList const& protocol, Web3Eip2930Parsed const& fromExtra)
{
    if (protocol.empty() || !fromExtra.accessList || fromExtra.accessList->empty())
    {
        return;
    }
    if (accessListsEqual(protocol, *fromExtra.accessList))
    {
        return;
    }
    WEB3_EIP2930_LOG(WARNING)
        << LOG_DESC("Tars data.accessList disagrees with extraTransactionBytes RLP; using Tars")
        << LOG_KV("tarsEntries", protocol.size())
        << LOG_KV("extraEntries", fromExtra.accessList->size());
}

Web3Eip2930Parsed parseEip2930FromExtraBytes(protocol::Transaction const& tx)
{
    Web3Eip2930Parsed out;
    auto const extra = tx.extraTransactionBytes();
    if (extra.empty())
    {
        return out;
    }
    auto const envelope = static_cast<uint8_t>(extra[0]);
    if (envelope > 0 && envelope < bcos::codec::rlp::BYTES_HEAD_BASE &&
        envelope != static_cast<uint8_t>(bcos::rpc::TransactionType::EIP2930) &&
        envelope != static_cast<uint8_t>(bcos::rpc::TransactionType::EIP1559) &&
        envelope != static_cast<uint8_t>(bcos::rpc::TransactionType::EIP4844))
    {
        out.web3TypedTxKind = envelope;
        return out;
    }

    bcos::bytesRef ref(const_cast<bcos::byte*>(extra.data()), static_cast<size_t>(extra.size()));
    bcos::rpc::Web3Transaction w3{};
    if (auto const decodeError = bcos::codec::rlp::decodeFromPayload(ref, w3);
        decodeError != nullptr)
    {
        WEB3_EIP2930_LOG(WARNING)
            << LOG_DESC("Failed to decode Web3 extraTransactionBytes for access list")
            << LOG_KV("extraLen", extra.size()) << LOG_KV("msg", decodeError->errorMessage());
        return out;
    }
    fillAccessListFromWeb3(w3, out);
    return out;
}
}  // namespace

Web3Eip2930Parsed parseEip2930FromWeb3Transaction(protocol::Transaction const& tx)
{
    Web3Eip2930Parsed out;
    if (tx.type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return out;
    }

    auto const kind = tx.web3TypedTxKind();
    if (kind != 0)
    {
        out.web3TypedTxKind = kind;
        auto const& list = tx.web3AccessList();
        if (!list.empty())
        {
            // Path A: Tars has both kind and access list — fast path, no extra-bytes fallback
            // needed.
            fillAccessListFromProtocol(list, out);
            auto const fromExtra = parseEip2930FromExtraBytes(tx);
            warnIfProtocolAccessListMismatch(tx, list, fromExtra);
            return out;
        }
        // Path B: kind is known from Tars but access list is empty in Tars.
        // Two possible reasons:
        //   1. The access list is genuinely empty (EIP-1559, or EIP-2930 with no pre-warmed
        //   entries).
        //   2. An older peer stripped the Tars accessList field — recover it from
        //   extraTransactionBytes.
        // Either way, keep the kind we already know and supplement with extra bytes if available.
        auto fromExtra = parseEip2930FromExtraBytes(tx);
        if (fromExtra.accessList && !fromExtra.accessList->empty())
        {
            out.accessList = std::move(fromExtra.accessList);
        }
        return out;
    }

    // Path C: kind == 0 means Tars fields were not populated (older node / missing field).
    // Fall back entirely to extraTransactionBytes RLP decoding.
    return parseEip2930FromExtraBytes(tx);
}

}  // namespace bcos::executor
