#include "DmcExecutor.h"
#include "ChecksumAddress.h"
#include <boost/format.hpp>

using namespace bcos::scheduler;

void DmcExecutor::prepare()
{
    if (m_needPrepare.size() == 0)
    {
        return;
    }

    bcos::WriteGuard lock(x_prepareLock);
    if (m_needPrepare.size() == 0)  // check again after get lock
    {
        return;
    }

    m_needPrepare.merge(m_lockingPool);  // locked executive always need to prepare
    std::set<ContextID> needSchedulerOut;
    for (auto contextID : m_needPrepare)
    {
        auto executiveState = m_pendingPool[contextID];
        auto hint = handleExecutiveMessage(executiveState);
        switch (hint)
        {
        case NEED_SEND:
        {
            m_needSendPool.insert(contextID);
            m_lockingPool.erase(contextID);
            break;
        }
        case SCHEDULER_OUT:
        {
            needSchedulerOut.insert(contextID);
            break;
        }
        case LOCKED:
        {
            if (m_lockingPool.count(contextID) == 0)
            {
                m_lockingPool.insert(contextID);
            }
            break;
        }
        case END:
        {
            m_pendingPool.erase(contextID);
            m_needSendPool.erase(contextID);
            m_lockingPool.erase(contextID);
            break;
        }
        }
    }

    for (auto contextID : needSchedulerOut)
    {
        schedulerOut(contextID);
    }

    m_needPrepare.clear();
}

void DmcExecutor::submit(protocol::ExecutionMessage::UniquePtr message, bool withDAG)
{
    auto contextID = message->contextID();
    m_pendingPool[contextID] =
        std::make_shared<ExecutiveState>(contextID, std::move(message), withDAG);
    m_needPrepare.insert(contextID);
}

void DmcExecutor::schedulerIn(ExecutiveState::Ptr executive)
{
    assert(executive->message->to() == m_contractAddress);  // message must belongs to this executor
    auto contextID = executive->message->contextID();
    // bcos::WriteGuard lock(x_poolLock);
    m_pendingPool[contextID] = executive;
    m_needPrepare.insert(contextID);
}

void DmcExecutor::go(std::function<void(bcos::Error::UniquePtr, Status)> callback)
{
    if (m_needPrepare.size() > 0)
    {
        callback(nullptr, NEED_PREPARE);
        return;
    }

    if (hasFinished())
    {
        callback(nullptr, FINISHED);
        return;
    }

    assert(f_onSchedulerOut != nullptr);

    auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>();

    // dump messages
    for (auto contextID : m_needSendPool)
    {
        messages->push_back(std::move(m_pendingPool[contextID]->message));
    }

    // call executor
    m_executor->dmcExecuteTransactions(m_contractAddress, *messages,
        [this, messages, callback = std::move(callback)](bcos::Error::UniquePtr error,
            std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
            if (error)
            {
                SCHEDULER_LOG(ERROR)
                    << "Execute transaction error: " << boost::diagnostic_information(*error);

                callback(std::move(error), ERROR);
            }
            else
            {
                handleExecutiveOutputs(std::move(outputs));
                if (hasFinished())
                {
                    callback(nullptr, FINISHED);
                }
                else
                {
                    callback(nullptr, PAUSED);
                }
            }
        });
}


DmcExecutor::MessageHint DmcExecutor::handleExecutiveMessage(ExecutiveState::Ptr executiveState)
{
    auto& message = executiveState->message;
    auto seq = message->seq();
    auto contextID = message->contextID();

    switch (message->type())
    {
        // Request type, push stack
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    {
        if (message->to().empty())
        {
            auto newSeq = executiveState->currentSeq;
            if (message->createSalt())
            {
                message->setTo(
                    newEVMAddress(message->from(), message->data(), *(message->createSalt())));
            }
            else
            {
                message->setTo(
                    newEVMAddress(m_block->blockHeaderConst()->number(), contextID, newSeq));
            }
            return SCHEDULER_OUT;
        }
        auto newSeq = executiveState->currentSeq++;
        executiveState->callStack.push(newSeq);
        executiveState->message->setSeq(newSeq);
        SCHEDULER_LOG(TRACE) << "Execute, " << message->contextID() << " | " << message->seq()
                             << " | " << std::hex << message->transactionHash() << " | "
                             << message->to();

        break;
    }
        // Return type, pop stack
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        executiveState->callStack.pop();

        // Empty stack, execution is finished
        if (executiveState->callStack.empty())
        {
            f_onTxFinished(std::move(message));

            // Remove executive state and continue
            SCHEDULER_LOG(TRACE) << "Erasing, " << message->contextID() << " | " << message->seq()
                                 << " | " << std::hex << message->transactionHash() << " | "
                                 << message->to();

            return END;
        }

        message->setSeq(executiveState->callStack.top());
        message->setCreate(false);

        SCHEDULER_LOG(TRACE) << "FINISHED/REVERT, " << message->contextID() << " | "
                             << message->seq() << " | " << std::hex << message->transactionHash()
                             << " | " << message->to();

        break;
    }
    case protocol::ExecutionMessage::REVERT_KEY_LOCK:
    {
        message->setType(protocol::ExecutionMessage::REVERT);
        message->setCreate(false);
        message->setKeyLocks({});
        SCHEDULER_LOG(TRACE) << "REVERT By key lock, " << message->contextID() << " | "
                             << message->seq() << " | " << std::hex << message->transactionHash()
                             << " | " << message->to();

        break;
    }
        // Retry type, send again
    case protocol::ExecutionMessage::KEY_LOCK:
    {
        // Try acquire key lock
        if (!m_keyLocks->acquireKeyLock(
                message->from(), message->keyLockAcquired(), contextID, seq))
        {
            SCHEDULER_LOG(TRACE) << "Waiting key, contract: " << contextID << " | " << seq << " | "
                                 << message->from()
                                 << " keyLockAcquired: " << toHex(message->keyLockAcquired());

            executiveState->message = std::move(message);
            return LOCKED;
        }

        SCHEDULER_LOG(TRACE) << "Wait key lock success, " << contextID << " | " << seq << " | "
                             << message->from()
                             << " keyLockAcquired: " << toHex(message->keyLockAcquired());
        break;
    }
        // Retry type, send again
    case protocol::ExecutionMessage::SEND_BACK:
    {
        SCHEDULER_LOG(TRACE) << "Send back, " << contextID << " | " << seq << " | "
                             << message->transactionHash() << LOG_KV("to", message->to());

        if (message->transactionHash() != h256(0))
        {
            message->setType(protocol::ExecutionMessage::TXHASH);
        }
        else
        {
            message->setType(protocol::ExecutionMessage::MESSAGE);
        }

        if (message->to().empty())
        {
            if (message->createSalt())
            {
                message->setTo(
                    newEVMAddress(message->from(), message->data(), *(message->createSalt())));
            }
            else
            {
                message->setTo(
                    newEVMAddress(m_block->blockHeaderConst()->number(), contextID, seq));
            }
            // creationContextIDs.insert(message->contextID());
        }

        break;
    }
    }

    // calledContract.emplace_hint(contractIt, message->to());

    // Set current key lock into message
    auto keyLocks = m_keyLocks->getKeyLocksNotHoldingByContext(message->to(), contextID);
    message->setKeyLocks(std::move(keyLocks));

    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
    {
        for (auto& keyIt : message->keyLocks())
        {
            SCHEDULER_LOG(TRACE) << boost::format(
                                        "Dispatch key lock type: %s, from: %s, to: %s, key: %s, "
                                        "contextID: %ld, seq: %ld") %
                                        message->type() % message->from() % message->to() %
                                        toHex(keyIt) % contextID % message->seq();
        }
    }

    return NEED_SEND;
}

void DmcExecutor::handleExecutiveOutputs(
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs)
{
    for (auto& output : outputs)
    {
        std::string to = {output->to().data(), output->to().size()};
        auto contextID = output->contextID();
        m_pendingPool[contextID]->message = std::move(output);

        if (to == m_contractAddress)
        {
            m_needPrepare.insert(contextID);
        }
        else
        {
            schedulerOut(contextID);
        }
    }
}

void DmcExecutor::schedulerOut(ContextID contextID)
{
    ExecutiveState::Ptr executiveState = m_pendingPool[contextID];
    m_lockingPool.erase(contextID);
    m_pendingPool.erase(contextID);
    f_onSchedulerOut(std::move(executiveState));  // scheduler out
}

inline void toChecksumAddress(std::string& _hexAddress, bcos::crypto::Hash::Ptr _hashImpl)
{
    boost::algorithm::to_lower(_hexAddress);
    bcos::toChecksumAddress(_hexAddress, _hashImpl->hash(_hexAddress).hex());
}

std::string DmcExecutor::newEVMAddress(int64_t blockNumber, int64_t contextID, int64_t seq)
{
    auto hash = m_hashImpl->hash(boost::lexical_cast<std::string>(blockNumber) + "_" +
                                 boost::lexical_cast<std::string>(contextID) + "_" +
                                 boost::lexical_cast<std::string>(seq));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_hashImpl);

    return hexAddress;
}

std::string DmcExecutor::newEVMAddress(
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    auto hash =
        m_hashImpl->hash(bytes{0xff} + _sender + toBigEndian(_salt) + m_hashImpl->hash(_init));

    std::string hexAddress;
    hexAddress.reserve(40);
    boost::algorithm::hex(hash.data(), hash.data() + 20, std::back_inserter(hexAddress));

    toChecksumAddress(hexAddress, m_hashImpl);

    return hexAddress;
}
