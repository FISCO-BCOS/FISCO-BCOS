#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "P2PClientImpl.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace bcos::ledger
{

class LedgerClientImpl : public bcos::concepts::ledger::LedgerBase<LedgerClientImpl>
{
    friend bcos::concepts::ledger::LedgerBase<LedgerClientImpl>;

public:
    LedgerClientImpl(std::shared_ptr<p2p::P2PClientImpl> p2p) : m_p2p(std::move(p2p)) {}

private:
    auto& p2p() { return bcos::concepts::getRef(m_p2p); }

    template <bcos::concepts::ledger::DataFlag... Flags>
    void impl_getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        bcostars::RequestBlock request;
        request.blockNumber = blockNumber;
        request.onlyHeader = true;

        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        auto nodeID = p2p().randomSelectNode();
        auto responseBuffer = p2p().sendMessageByNodeID(
            bcos::protocol::LIGHTNODE_GETBLOCK, nodeID, bcos::ref(requestBuffer));

        bcostars::ResponseBlock response;
        bcos::concepts::serialize::decode(responseBuffer, response);
        std::swap(response.block, block);
    }

    void impl_getTransactionsOrReceipts(RANGES::range auto const& hashes, RANGES::range auto& out)
    {
        using DataType = RANGES::range_value_t<std::remove_cvref_t<decltype(out)>>;
        using RequestType = std::conditional_t<bcos::concepts::transaction::Transaction<DataType>,
            bcostars::RequestTransactions, bcostars::RequestReceipts>;
        using ResponseType = std::conditional_t<bcos::concepts::transaction::Transaction<DataType>,
            bcostars::ResponseTransactions, bcostars::ResponseReceipts>;

        RequestType request;
        request.hashes.reserve(RANGES::size(hashes));
        for (auto& hash : hashes)
        {
            request.hashes.emplace_back(std::vector<char>(hash.begin(), hash.end()));
        }
        request.withProof = true;

        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        auto nodeID = p2p().randomSelectNode();
        auto responseBuffer = p2p().sendMessageByNodeID(
            protocol::LIGHTNODE_GETTRANSACTIONS, std::move(nodeID), bcos::ref(requestBuffer));

        ResponseType response;
        bcos::concepts::serialize::decode(responseBuffer, response);
        if constexpr (bcos::concepts::transaction::Transaction<DataType>)
        {
            bcos::concepts::resizeTo(out, response.transactions.size());
            std::move(RANGES::begin(response.transactions), RANGES::end(response.transactions),
                RANGES::begin(out));
        }
        else
        {
            bcos::concepts::resizeTo(out, response.receipts.size());
            std::move(RANGES::begin(response.receipts), RANGES::end(response.receipts),
                RANGES::begin(out));
        }
    }

    bcos::concepts::ledger::Status impl_getStatus()
    {
        bcostars::RequestGetStatus request;
        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        auto nodeID = p2p().randomSelectNode();
        auto responseBuffer = p2p().sendMessageByNodeID(
            protocol::LIGHTNODE_GETSTATUS, std::move(nodeID), bcos::ref(requestBuffer));

        bcostars::ResponseGetStatus response;
        bcos::concepts::serialize::decode(responseBuffer, response);

        bcos::concepts::ledger::Status status;
        status.total = response.total;
        status.failed = response.failed;
        status.blockNumber = response.blockNumber;

        return status;
    }

    std::shared_ptr<p2p::P2PClientImpl> m_p2p;
};
}  // namespace bcos::ledger