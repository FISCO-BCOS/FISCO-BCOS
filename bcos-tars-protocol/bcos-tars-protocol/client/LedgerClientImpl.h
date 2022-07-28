#pragma once

#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-front/FrontService.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <boost/throw_exception.hpp>
#include <random>

namespace bcos::ledger
{
class LedgerClientImpl : public bcos::concepts::ledger::LedgerBase<LedgerClientImpl>
{
    friend bcos::concepts::ledger::LedgerBase<LedgerClientImpl>;

public:
    LedgerClientImpl(bcos::front::FrontServiceInterface::Ptr front)
      : m_front(std::move(front)), m_rng(std::random_device{}())
    {}

private:
    template <bcos::concepts::ledger::DataFlag... Flags>
    void impl_getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        bcostars::RequestBlock request;
        request.blockNumber = blockNumber;
        request.onlyHeader = true;

        // select a node
    }

    template <concepts::ledger::DataFlag Flag>
    auto impl_getTransactionsOrReceipts(RANGES::range auto const& hashes, RANGES::range auto& out)
    {}

    bcos::concepts::ledger::Status impl_getStatus() {}

    void selectNode()
    {
        std::promise<std::tuple<bcos::Error::Ptr, std::string>> promise;
        m_front->asyncGetGroupNodeInfo([this, &promise](bcos::Error::Ptr _error,
                                           bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            auto& nodeIDList = _groupNodeInfo->nodeIDList();
            std::uniform_int_distribution<size_t> distribution{0u, nodeIDList.size()};
            auto& nodeID = nodeIDList[distribution(m_rng)];
            promise.set_value(std::make_tuple(std::move(_error), std::move(nodeID)));
        });

        auto [error, nodeID] = promise.get_future().get();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        
    }

    bcos::front::FrontServiceInterface::Ptr m_front;
    std::mt19937 m_rng;
};
}  // namespace bcos::ledger