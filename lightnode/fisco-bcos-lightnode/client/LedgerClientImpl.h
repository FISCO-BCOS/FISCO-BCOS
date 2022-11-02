#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "P2PClientImpl.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-lightnode/Log.h>
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

    template <bcos::concepts::ledger::DataFlag Flag>
    void processGetBlockFlags(bool& onlyHeaderFlag)
    {
        if constexpr (std::is_same_v<Flag, bcos::concepts::ledger::HEADER>)
        {
            onlyHeaderFlag = true;
        }
    }

    template <bcos::concepts::ledger::DataFlag... Flags>
    task::Task<void> impl_getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        bcostars::RequestBlock request;
        request.blockNumber = blockNumber;
        request.onlyHeader = false;

        (processGetBlockFlags<Flags>(request.onlyHeader), ...);

        bcostars::ResponseBlock response;
        auto nodeID = co_await p2p().randomSelectNode();
        co_await p2p().sendMessageByNodeID(
            bcos::protocol::LIGHTNODE_GET_BLOCK, nodeID, request, response);

        if (response.error.errorCode)
            BOOST_THROW_EXCEPTION(std::runtime_error(response.error.errorMessage));

        std::swap(response.block, block);
    }

    task::Task<void> impl_getTransactions(RANGES::range auto const& hashes, RANGES::range auto& out)
    {
        using DataType = RANGES::range_value_t<std::remove_cvref_t<decltype(out)>>;
        using RequestType = std::conditional_t<bcos::concepts::transaction::Transaction<DataType>,
            bcostars::RequestTransactions, bcostars::RequestReceipts>;
        using ResponseType = std::conditional_t<bcos::concepts::transaction::Transaction<DataType>,
            bcostars::ResponseTransactions, bcostars::ResponseReceipts>;
        auto moduleID = bcos::concepts::transaction::Transaction<DataType> ?
                            protocol::LIGHTNODE_GET_TRANSACTIONS :
                            protocol::LIGHTNODE_GET_RECEIPTS;

        RequestType request;
        request.hashes.reserve(RANGES::size(hashes));
        for (auto& hash : hashes)
        {
            request.hashes.emplace_back(std::vector<char>(hash.begin(), hash.end()));
        }
        request.withProof = true;

        ResponseType response;
        auto nodeID = co_await p2p().randomSelectNode();
        co_await p2p().sendMessageByNodeID(moduleID, std::move(nodeID), request, response);

        if (response.error.errorCode)
            BOOST_THROW_EXCEPTION(std::runtime_error(response.error.errorMessage));

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

    task::Task<bcos::concepts::ledger::Status> impl_getStatus()
    {
        bcostars::RequestGetStatus request;
        bcostars::ResponseGetStatus response;

        auto nodeID = co_await p2p().randomSelectNode();

        co_await p2p().sendMessageByNodeID(
            protocol::LIGHTNODE_GET_STATUS, std::move(nodeID), request, response);

        if (response.error.errorCode)
        {
            LIGHTNODE_LOG(WARNING) << "Get status failed, errorCode: " << response.error.errorCode
                                   << " " << response.error.errorMessage;
            BOOST_THROW_EXCEPTION(std::runtime_error(response.error.errorMessage));
        }

        bcos::concepts::ledger::Status status;
        status.total = response.total;
        status.failed = response.failed;
        status.blockNumber = response.blockNumber;

        LIGHTNODE_LOG(DEBUG) << "Got status from remote: " << status.blockNumber << " "
                             << response.blockNumber;

        co_return status;
    }

    std::shared_ptr<p2p::P2PClientImpl> m_p2p;
};
}  // namespace bcos::ledger