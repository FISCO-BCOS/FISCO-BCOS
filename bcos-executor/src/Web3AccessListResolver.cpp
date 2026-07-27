#include "Web3AccessListResolver.h"
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
#define WEB3_ACCESS_LIST_RESOLVER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("WEB3_ACCESS_LIST")

void buildAccessListFromWeb3(bcos::rpc::Web3Transaction const& w3, Web3AccessListResolved& out)
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
        list->emplace_back(e.account, std::move(keys));
    }
    out.accessList = std::move(list);
}

void buildAccessListFromProtocol(
    bcos::protocol::Web3AccessList const& src, Web3AccessListResolved& out)
{
    auto list = std::make_shared<Eip2930AccessList>();
    list->reserve(src.size());
    for (auto const& e : src)
    {
        list->emplace_back(e.account, e.storageKeys);
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
        if (p.account != d.first)
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

void warnIfProtocolAccessListMismatch(bcos::protocol::Web3AccessList const& protocol,
    Web3AccessListResolved const& fromExtra, uint8_t tarsKind)
{
    bool const kindMismatch =
        tarsKind != 0 && fromExtra.web3TypedTxKind != 0 && tarsKind != fromExtra.web3TypedTxKind;
    bool listMismatch = false;
    if (!protocol.empty())
    {
        if (!fromExtra.accessList || fromExtra.accessList->empty() ||
            !accessListsEqual(protocol, *fromExtra.accessList))
        {
            listMismatch = true;
        }
    }
    if (!kindMismatch && !listMismatch)
    {
        return;
    }
    WEB3_ACCESS_LIST_RESOLVER_LOG(WARNING)
        << LOG_DESC(
               "Tars data.accessList/web3TypedTxKind disagrees with extraTransactionBytes RLP; "
               "using RLP")
        << LOG_KV("tarsKind", tarsKind) << LOG_KV("rlpKind", fromExtra.web3TypedTxKind)
        << LOG_KV("tarsEntries", protocol.size())
        << LOG_KV("extraEntries", fromExtra.accessList ? fromExtra.accessList->size() : 0);
}

Web3AccessListResolved resolveWeb3AccessListFromExtraBytes(protocol::Transaction const& tx)
{
    Web3AccessListResolved out;
    auto const extra = tx.extraTransactionBytes();
    if (extra.empty())
    {
        return out;
    }
    auto const envelope = static_cast<uint8_t>(extra[0]);
    // NOTE: Only typed tx kinds 1 (EIP-2930), 2 (EIP-1559), and 3 (EIP-4844) carry an
    // EIP-2930 access list and need RLP decoding below. Unknown kinds in (0,
    // BYTES_HEAD_BASE) that do NOT carry access lists (or whose RLP format is unknown)
    // return with kind-only. If a future EIP adds a new typed tx WITH an access list
    // (kind >= 4), add its kind to the exclusion list below.
    if (envelope > 0 && envelope < bcos::codec::rlp::BYTES_HEAD_BASE &&
        envelope != static_cast<uint8_t>(bcos::rpc::TransactionType::EIP2930) &&
        envelope != static_cast<uint8_t>(bcos::rpc::TransactionType::EIP1559) &&
        envelope != static_cast<uint8_t>(bcos::rpc::TransactionType::EIP4844))
    {
        out.web3TypedTxKind = envelope;
        return out;
    }

    bcos::bytes extraCopy(extra.begin(), extra.end());
    bcos::bytesRef ref(extraCopy.data(), extraCopy.size());
    bcos::rpc::Web3Transaction w3{};
    if (auto const decodeError = bcos::codec::rlp::decodeFromPayload(ref, w3);
        decodeError != nullptr)
    {
        WEB3_ACCESS_LIST_RESOLVER_LOG(WARNING)
            << LOG_DESC("Failed to decode Web3 extraTransactionBytes for access list")
            << LOG_KV("extraLen", extra.size()) << LOG_KV("msg", decodeError->errorMessage());
        return out;
    }
    buildAccessListFromWeb3(w3, out);
    return out;
}
}  // namespace

Web3AccessListResolved resolveWeb3AccessList(protocol::Transaction const& tx)
{
    Web3AccessListResolved out;
    if (tx.type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return out;
    }

    // Signed RLP (extraTransactionBytes) is authoritative. Tars data.accessList /
    // web3TypedTxKind are a cache that may diverge across peers for the same txHash.
    auto const fromExtra = resolveWeb3AccessListFromExtraBytes(tx);
    auto const tarsKind = tx.web3TypedTxKind();
    auto const& tarsList = tx.web3AccessList();

    if (fromExtra.web3TypedTxKind != 0 ||
        (fromExtra.accessList && !fromExtra.accessList->empty()))
    {
        if (tarsKind != 0 || !tarsList.empty())
        {
            warnIfProtocolAccessListMismatch(tarsList, fromExtra, tarsKind);
        }
        return fromExtra;
    }

    // Fallback when extra bytes are missing or undecodable: use the Tars cache.
    if (tarsKind != 0)
    {
        out.web3TypedTxKind = tarsKind;
        if (!tarsList.empty())
        {
            buildAccessListFromProtocol(tarsList, out);
        }
        return out;
    }
    return out;
}

}  // namespace bcos::executor
