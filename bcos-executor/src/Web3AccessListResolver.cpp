#include "Web3AccessListResolver.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/Web3AccessList.h"

namespace bcos::executor
{
Web3AccessListResolved resolveWeb3AccessList(protocol::Transaction const& tx)
{
    Web3AccessListResolved out;
    if (tx.type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return out;
    }

    // Admission (TxValidator / TransactionSync) rejects Tars vs signed-RLP disagreement.
    // Tars fields are therefore authoritative at execution time.
    out.web3TypedTxKind = tx.web3TypedTxKind();
    auto const& list = tx.web3AccessList();
    if (list.empty())
    {
        return out;
    }
    auto resolved = std::make_shared<Eip2930AccessList>();
    resolved->reserve(list.size());
    for (auto const& e : list)
    {
        resolved->emplace_back(e.account, e.storageKeys);
    }
    out.accessList = std::move(resolved);
    return out;
}
}  // namespace bcos::executor
