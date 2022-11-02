#pragma once

#include <bcos-concepts/protocol/Transaction.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-task/Task.h>

namespace bcos::txpool::sync
{
class Sync
{
public:
    task::Task<void> broadcastTransaction(
        concepts::transaction::Transaction auto const& transaction)
    {
        // m_front->asyncSendBroadcastMessage(uint16_t _type, int _moduleID, bytesConstRef _data)
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_front;
};
}  // namespace bcos::txpool::sync