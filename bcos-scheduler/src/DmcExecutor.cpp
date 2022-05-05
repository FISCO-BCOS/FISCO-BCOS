#include "DmcExecutor.h"
#include "ChecksumAddress.h"
#include <boost/format.hpp>

using namespace bcos::scheduler;

void DmcExecutor::releaseOutdatedLock()
{
    for (auto contextID : m_needPrepare)
    {
        auto& message = m_pendingPool[contextID]->message;
        if (message->type() == bcos::protocol::ExecutionMessage::FINISHED ||
            message->type() == bcos::protocol::ExecutionMessage::REVERT)
        {
            m_keyLocks->releaseKeyLocks(message->contextID(), message->seq());
        }
    }
}

void DmcExecutor::prepare()
{
    // clear env
    removeOutdatedStatus();

#ifdef DMC_TRACE_LOG_ENABLE
    // logging
    for (auto& it : m_pendingPool)
    {
        DMC_LOG(TRACE) << " 1.NeedPrepare: \t [--] " << it.second->toString() << std::endl;
    }
#endif

    if (m_needPrepare.empty())
    {
        return;  // finished
    }

    // get what contextID that need to prepare: prepareSet
    std::set<ContextID> needScheduleOut;
    std::set<ContextID, std::less<>> prepareSet;
    for (auto contextID : m_needPrepare)
    {
        prepareSet.insert(contextID);
    }
    m_needPrepare.clear();

    // prepare all contextID in prepareSet
    for (auto contextID : prepareSet)
    {
        auto executiveState = m_pendingPool[contextID];

        auto hint = handleExecutiveMessage(executiveState);
        switch (hint)
        {
        case NEED_SEND:
        {
            // bcos::WriteGuard lock1(x_concurrentLock);
            m_needSendPool.insert(contextID);
            m_lockingPool.unsafe_erase(contextID);
            break;
        }
        case SCHEDULER_OUT:
        {
            needScheduleOut.insert(contextID);
            break;
        }
        case LOCKED:
        {
            // bcos::WriteGuard lock1(x_concurrentLock);
            if (m_lockingPool.count(contextID) == 0)
            {
                m_lockingPool.insert(contextID);
            }
            break;
        }
        case END:
        {
            // bcos::WriteGuard lock1(x_concurrentLock);
            m_needRemove.insert(contextID);
            break;
        }
        }
    }

    // handle schedule out message
    for (auto contextID : needScheduleOut)
    {
        scheduleOut(contextID);
    }

    // clear env
    removeOutdatedStatus();
}

bool DmcExecutor::unlockPrepare()
{
    // generate lockingPool ordered by contxtID less<>
    std::set<ContextID, std::less<>> orderedLockingPool;  // TODO: remove all this
    for (auto contextID : m_lockingPool)
    {
        orderedLockingPool.insert(contextID);
    }

    for (auto contextID : orderedLockingPool)
    {
        auto executiveState = m_pendingPool[contextID];
        auto& message = executiveState->message;
        auto seq = message->seq();

        if (message->type() != protocol::ExecutionMessage::KEY_LOCK)
        {
            DMC_LOG(FATAL) << "Handle a normal message in locking pool" << message->toString();
            assert(false);
        }

        // Try acquire key lock
        if (!m_keyLocks->acquireKeyLock(
                message->from(), message->keyLockAcquired(), contextID, seq))
        {
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << "Waiting key, contract: " << contextID << " | " << seq << " | "
                           << message->from()
                           << " keyLockAcquired: " << toHex(message->keyLockAcquired())
                           << std::endl;
#endif
        }
        else
        {
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << "Wait key lock success, " << contextID << " | " << seq << " | "
                           << message->from()
                           << " keyLockAcquired: " << toHex(message->keyLockAcquired())
                           << std::endl;
#endif

            m_needSendPool.insert(contextID);
            m_lockingPool.unsafe_erase(contextID);
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " 3.AfterPrepare: \t [..] " << executiveState->toString() << " UNLOCK"
                           << std::endl;
#endif
        }
    }

    return m_needSendPool.empty();  // has locked tx but nothing to send, need to detect deadlock
}

bool DmcExecutor::detectLockAndRevert()
{
    if (m_lockingPool.empty())
    {
        return false;  // no detected
    }

    // generate lockingPool ordered by contxtID less<>
    std::set<ContextID, std::less<>> orderedLockingPool;  // TODO: remove this
    for (auto contextID : m_lockingPool)
    {
        orderedLockingPool.insert(contextID);
    }

    for (auto contextID : orderedLockingPool)
    {
        if (m_keyLocks->detectDeadLock(contextID))
        {
            auto executiveState = m_pendingPool[contextID];
            auto& message = executiveState->message;

            SCHEDULER_LOG(INFO) << "Detected dead lock at " << contextID << " | " << message->seq()
                                << " , revert";

            message->setType(protocol::ExecutionMessage::REVERT);
            message->setCreate(false);
            message->setKeyLocks({});

            m_needSendPool.insert(contextID);
            m_lockingPool.unsafe_erase(contextID);
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " 3.AfterPrepare: \t [..] " << executiveState->toString() << " REVERT"
                           << std::endl;
#endif
            return true;  // just detect one TODO: detect and unlock more deadlock
        }
    }

    return false;  // no detected
}

void DmcExecutor::removeOutdatedStatus()
{
    for (auto contextID : m_needRemove)
    {
        m_pendingPool.unsafe_erase(contextID);
        m_lockingPool.unsafe_erase(contextID);
        m_needPrepare.unsafe_erase(contextID);
    }
    m_needRemove.clear();
}

void DmcExecutor::submit(protocol::ExecutionMessage::UniquePtr message, bool withDAG)
{
    auto contextID = message->contextID();
    /*
        DMC_LOG(TRACE) << std::endl
                  << " ////// " << message->contextID() << " | " << message->seq() << " | " <<
       std::hex
                  << message->transactionHash() << " | " << message->to();
                  */
    {
        // bcos::ReadGuard lock(x_concurrentLock);
        m_pendingPool.insert(
            {contextID, std::make_shared<ExecutiveState>(contextID, std::move(message), withDAG)});
        m_needPrepare.insert(contextID);
    }
}

void DmcExecutor::scheduleIn(ExecutiveState::Ptr executive)
{
    assert(executive->message->to() == m_contractAddress);  // message must belongs to this executor
    auto contextID = executive->message->contextID();
    {
        // bcos::ReadGuard lock(x_concurrentLock);
        m_pendingPool.insert({contextID, executive});
        m_needPrepare.insert(contextID);
    }
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

    if (m_needSendPool.empty())
    {
        callback(nullptr, PAUSED);
        return;
    }

    assert(f_onSchedulerOut != nullptr);

    auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>();

    // dump messages
    {
        // bcos::ReadGuard lock(x_concurrentLock);
        std::set<ContextID, std::less<>> needSendPool;
        for (auto contextID : m_needSendPool)
        {
            needSendPool.insert(contextID);
        }
        for (auto contextID : needSendPool)
        {
            auto& executiveState = m_pendingPool[contextID];
            auto& message = executiveState->message;

            auto keyLocks = m_keyLocks->getKeyLocksNotHoldingByContext(message->to(), contextID);
            message->setKeyLocks(std::move(keyLocks));  // TODO: consider thread safe
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " 4.SendToExecutor:\t >>>> " << executiveState->toString()
                           << " >>>> flowAddr:" << m_contractAddress << std::endl;
#endif

            messages->push_back(std::move(message));
        }
        m_needSendPool.clear();
    }

    if (messages->size() == 1 && (*messages)[0]->staticCall())
    {
        // is static call
        m_executor->call(std::move((*messages)[0]),
            [this, callback = std::move(callback)](
                bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr output) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Call error: " << boost::diagnostic_information(*error);

                    callback(std::move(error), ERROR);
                }
                else
                {
                    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs;
                    outputs.push_back(std::move(output));
                    handleExecutiveOutputs(std::move(outputs));
                    callback(nullptr, PAUSED);
                }
            });
    }
    else
    {
        // is transaction
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
                    callback(nullptr, PAUSED);
                }
            });
    }
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
            // TODO: add a function for all this
            auto newSeq = executiveState->currentSeq;
            if (message->createSalt())
            {
                // TODO: Add sender in this process
                message->setTo(
                    newEVMAddress(message->from(), message->data(), *(message->createSalt())));
            }
            else
            {
                // TODO: Add sender in this process
                message->setTo(
                    newEVMAddress(m_block->blockHeaderConst()->number(), contextID, newSeq));
            }
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " 2.AfterPrepare: \t [..] " << executiveState->toString()
                           << " SCHEDULER_OUT" << std::endl;
#endif
        }

        if (message->to() != m_contractAddress)
        {
            return SCHEDULER_OUT;
        }

        // update my key locks in m_keyLocks
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

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
            // Remove executive state and continue
            /*
                        DMC_LOG(TRACE) << std::endl
                                  << "Erasing " << message->contextID() << " | " << message->seq()
               << " | "
                                  << std::hex << message->transactionHash() << " | " <<
               message->to();
            */
            f_onTxFinished(std::move(message));
            // TODO: add words
#ifdef DMC_TRACE_LOG_ENABLE
            DMC_LOG(TRACE) << " 2.AfterPrepare: \t [..] " << executiveState->toString() << " END"
                           << std::endl;
#endif
            return END;
        }

        message->setSeq(executiveState->callStack.top());
        message->setCreate(false);

        SCHEDULER_LOG(TRACE) << "FINISHED/REVERT, " << message->contextID() << " | "
                             << message->seq() << " | " << std::hex << message->transactionHash()
                             << " | " << message->to();

        break;
    }
        // Retry type, send again
    case protocol::ExecutionMessage::KEY_LOCK:
    {
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

        executiveState->message = std::move(message);
#ifdef DMC_TRACE_LOG_ENABLE
        DMC_LOG(TRACE) << " 2.AfterPrepare: \t [..] " << executiveState->toString() << " LOCKED"
                       << std::endl;
#endif
        return LOCKED;
    }
        // Retry type, send again
    case protocol::ExecutionMessage::SEND_BACK:
    {
        // SCHEDULER_LOG(TRACE) << "Send back, " << contextID << " | " << seq << " | "
        //                     << message->transactionHash() << LOG_KV("to", message->to());

        if (message->transactionHash() != h256(0))
        {
            message->setType(protocol::ExecutionMessage::TXHASH);
        }
        else
        {
            message->setType(protocol::ExecutionMessage::MESSAGE);
        }

        // TODO: add a uniform function
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
        // TODO : scheduler out
        break;
    }
    }

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
#ifdef DMC_TRACE_LOG_ENABLE
    DMC_LOG(TRACE) << " 2.AfterPrepare: \t [..] " << executiveState->toString() << " NEED_SEND"
                   << std::endl;
#endif
    return NEED_SEND;
}

void DmcExecutor::handleExecutiveOutputs(
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs)
{
    for (auto& output : outputs)
    {
        std::string to = {output->to().data(), output->to().size()};
        ExecutiveState::Ptr executiveState;
        auto contextID = output->contextID();

        // bcos::ReadGuard lock(x_concurrentLock);
        executiveState = m_pendingPool[contextID];
        executiveState->message = std::move(output);
#ifdef DMC_TRACE_LOG_ENABLE
        DMC_LOG(TRACE) << " 5.RevFromExecutor: <<<< addr:" << m_contractAddress << " <<<< "
                       << executiveState->toString() << std::endl;
#endif

        if (to == m_contractAddress)
        {
            // bcos::ReadGuard lock(x_concurrentLock);
            // is my output
            m_needPrepare.insert(contextID);
        }
        else
        {
            scheduleOut(contextID);
        }
    }
}

void DmcExecutor::scheduleOut(ContextID contextID)
{
    ExecutiveState::Ptr executiveState;
    {
        // bcos::WriteGuard lock(x_concurrentLock);
        executiveState = m_pendingPool[contextID];
        m_needRemove.insert(contextID);
    }
    f_onSchedulerOut(std::move(executiveState));  // schedule out
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
