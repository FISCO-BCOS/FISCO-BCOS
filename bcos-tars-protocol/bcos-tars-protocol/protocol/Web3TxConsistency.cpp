/**
 * @file Web3TxConsistency.cpp
 */
#include "Web3TxConsistency.h"
#include <bcos-codec/rlp/Web3Transaction.h>
#include <bcos-utilities/BoostLog.h>

namespace bcostars::protocol
{
namespace
{
#define WEB3_TX_CONSISTENCY_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("WEB3_TX_CONSISTENCY")

bool accessListsEqual(bcos::protocol::Web3AccessList const& tars,
    std::vector<bcos::rpc::AccessListEntry> const& rlp)
{
    if (tars.size() != rlp.size())
    {
        return false;
    }
    for (size_t i = 0; i < tars.size(); ++i)
    {
        if (tars[i].account != rlp[i].account)
        {
            return false;
        }
        if (tars[i].storageKeys != rlp[i].storageKeys)
        {
            return false;
        }
    }
    return true;
}
}  // namespace

bool web3TarsFieldsMatchSignedExtra(bcos::protocol::Transaction const& tx)
{
    if (tx.type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return true;
    }

    auto extra = tx.extraTransactionBytes();
    if (extra.empty())
    {
        WEB3_TX_CONSISTENCY_LOG(WARNING) << LOG_DESC("Web3 tx missing extraTransactionBytes")
                                         << LOG_KV("hash", tx.hash().abridged());
        return false;
    }

    bcos::bytes extraCopy(extra.begin(), extra.end());
    bcos::bytesRef ref(extraCopy.data(), extraCopy.size());
    bcos::rpc::Web3Transaction w3{};
    if (auto const decodeError = bcos::codec::rlp::decodeFromPayload(ref, w3);
        decodeError != nullptr)
    {
        WEB3_TX_CONSISTENCY_LOG(WARNING)
            << LOG_DESC("Failed to decode Web3 extraTransactionBytes")
            << LOG_KV("hash", tx.hash().abridged()) << LOG_KV("msg", decodeError->errorMessage());
        return false;
    }

    auto const tarsKind = tx.web3TypedTxKind();
    auto const rlpKind = static_cast<uint8_t>(w3.type);
    if (tarsKind != rlpKind)
    {
        WEB3_TX_CONSISTENCY_LOG(WARNING)
            << LOG_DESC("web3TypedTxKind disagrees with signed RLP")
            << LOG_KV("hash", tx.hash().abridged()) << LOG_KV("tarsKind", tarsKind)
            << LOG_KV("rlpKind", rlpKind);
        return false;
    }

    auto const& tarsList = tx.web3AccessList();
    if (!accessListsEqual(tarsList, w3.accessList))
    {
        WEB3_TX_CONSISTENCY_LOG(WARNING)
            << LOG_DESC("data.accessList disagrees with signed RLP")
            << LOG_KV("hash", tx.hash().abridged()) << LOG_KV("tarsEntries", tarsList.size())
            << LOG_KV("rlpEntries", w3.accessList.size());
        return false;
    }
    return true;
}
}  // namespace bcostars::protocol
