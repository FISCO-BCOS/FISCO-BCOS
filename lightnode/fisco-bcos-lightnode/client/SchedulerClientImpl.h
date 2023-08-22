#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "P2PClientImpl.h"
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/scheduler/Scheduler.h>
#include <bcos-tars-protocol/tars/LightNode.h>

namespace bcos::scheduler
{
class SchedulerClientImpl : public bcos::concepts::scheduler::SchedulerBase<SchedulerClientImpl>
{
    friend bcos::concepts::scheduler::SchedulerBase<SchedulerClientImpl>;

public:
    SchedulerClientImpl(std::shared_ptr<p2p::P2PClientImpl> p2p) : m_p2p(std::move(p2p)) {}

private:
    auto& p2p() { return bcos::concepts::getRef(m_p2p); }

    task::Task<void> impl_call(bcos::concepts::transaction::Transaction auto const& transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        bcostars::RequestSendTransaction request;
        request.transaction = std::move(transaction);

        bcostars::ResponseSendTransaction response;
        auto nodeIDs = co_await p2p().getAllNodeID();
        for(auto const& nodeID : nodeIDs)
        {
            co_await p2p().sendMessageByNodeID(
                    bcos::protocol::LIGHTNODE_CALL, nodeID, request, response);

            if (response.error.errorCode)
            {
                LIGHTNODE_LOG(WARNING) << "call failed, request nodeID: " << nodeID->hex()
                                       << "response errorCode: " << response.error.errorCode
                                       << " " << response.error.errorMessage;
                continue;
            }
            else
            {
                std::swap(response.receipt, receipt);
                co_return;
            }
        }
        LIGHTNODE_LOG(ERROR) << "lightNode implement call with allNode failed!"
                             << LOG_KV("response code", response.error.errorCode)
                             << LOG_KV("response message", response.error.errorMessage);
        BOOST_THROW_EXCEPTION(std::runtime_error(response.error.errorMessage));
    }

    std::shared_ptr<p2p::P2PClientImpl> m_p2p;
};
}  // namespace bcos::scheduler