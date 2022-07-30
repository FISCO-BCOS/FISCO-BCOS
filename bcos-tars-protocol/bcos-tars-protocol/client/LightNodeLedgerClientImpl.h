#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "bcos-concepts/Basic.h"
#include "bcos-concepts/Serialize.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-front/FrontService.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <iterator>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace bcos::ledger
{

class LightNodeLedgerClientImpl
  : public bcos::concepts::ledger::LedgerBase<LightNodeLedgerClientImpl>
{
    friend bcos::concepts::ledger::LedgerBase<LightNodeLedgerClientImpl>;

public:
    LightNodeLedgerClientImpl(
        bcos::front::FrontServiceInterface::Ptr front, bcos::crypto::KeyFactoryImpl::Ptr keyFactory)
      : m_front(std::move(front)),
        m_keyFactory(std::move(keyFactory)),
        m_rng(std::random_device{}())
    {}

private:
    template <bcos::concepts::ledger::DataFlag... Flags>
    void impl_getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        bcostars::RequestBlock request;
        request.blockNumber = blockNumber;
        request.onlyHeader = true;

        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        auto nodeID = selectNode();
        auto responseBuffer = sendMessageByNodeID(
            bcos::protocol::LIGHTNODE_GETBLOCK, nodeID, bcos::ref(requestBuffer));

        bcostars::ResponseBlock response;
        bcos::concepts::serialize::decode(responseBuffer, response);
        std::swap(response.block, block);
    }

    void getTransactionsOrReceipts(RANGES::range auto const& hashes, RANGES::range auto& out)
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

        auto nodeID = selectNode();
        auto responseBuffer = sendMessageByNodeID(
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

        auto nodeID = selectNode();
        auto responseBuffer = sendMessageByNodeID(
            protocol::LIGHTNODE_GETSTATUS, std::move(nodeID), bcos::ref(requestBuffer));

        bcostars::ResponseGetStatus response;
        bcos::concepts::serialize::decode(responseBuffer, response);

        bcos::concepts::ledger::Status status;
        status.total = response.total;
        status.failed = response.failed;
        status.blockNumber = response.blockNumber;

        return status;
    }

    crypto::NodeIDPtr selectNode()
    {
        std::promise<std::tuple<bcos::Error::Ptr, std::string>> promise;
        m_front->asyncGetGroupNodeInfo([this, &promise](bcos::Error::Ptr _error,
                                           bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            if (_error || !_groupNodeInfo)
            {
                promise.set_value(std::make_tuple(std::move(_error), std::string{}));
                return;
            }
            
            auto& nodeIDList = _groupNodeInfo->nodeIDList();
            if (nodeIDList.empty())
            {
                promise.set_value(std::make_tuple(std::move(_error), std::string{}));
                return;
            }

            std::uniform_int_distribution<size_t> distribution{0u, nodeIDList.size()};
            auto& nodeID = nodeIDList[distribution(m_rng)];
            promise.set_value(std::make_tuple(std::move(_error), std::move(nodeID)));
        });

        auto [error, nodeIDStr] = promise.get_future().get();
        if (error)
            BOOST_THROW_EXCEPTION(*error);

        if (nodeIDStr.empty())
            BOOST_THROW_EXCEPTION(std::runtime_error{"No node available"});

        auto nodeID = m_keyFactory->createKey(nodeIDStr);
        return nodeID;
    }

    bcos::bytes sendMessageByNodeID(
        int moduleID, crypto::NodeIDPtr nodeID, bcos::bytesConstRef buffer)
    {
        std::promise<std::tuple<bcos::Error::Ptr, bcos::bytes>> promise;
        m_front->asyncSendMessageByNodeID(bcos::protocol::LIGHTNODE_GETBLOCK, std::move(nodeID),
            buffer, 0,
            [&promise](Error::Ptr _error, bcos::crypto::NodeIDPtr, bytesConstRef _data,
                const std::string&, front::ResponseFunc) {
                promise.set_value(
                    std::make_tuple(std::move(_error), bcos::bytes(_data.begin(), _data.end())));
            });

        auto future = promise.get_future();
        auto [error, responseBuffer] = future.get();
        if (error)
            BOOST_THROW_EXCEPTION(*error);

        return responseBuffer;
    }

    bcos::front::FrontServiceInterface::Ptr m_front;
    bcos::crypto::KeyFactoryImpl::Ptr m_keyFactory;
    std::mt19937 m_rng;
};
}  // namespace bcos::ledger