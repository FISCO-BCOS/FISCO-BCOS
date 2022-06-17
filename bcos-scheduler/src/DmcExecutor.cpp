#include "DmcExecutor.h"
#include "ChecksumAddress.h"
#include <boost/format.hpp>

using namespace bcos::scheduler;

void DmcExecutor::releaseOutdatedLock()
{
    m_executivePool.forEach(
        MessageHint::NEED_PREPARE, [this](int64_t, ExecutiveState::Ptr executiveState) {
            auto& message = executiveState->message;
            if (message->type() == bcos::protocol::ExecutionMessage::FINISHED ||
                message->type() == bcos::protocol::ExecutionMessage::REVERT)
            {
                m_keyLocks->releaseKeyLocks(message->contextID(), message->seq());
            }
            return true;
        });
}

bool DmcExecutor::prepare()
{
    // clear env
    m_executivePool.refresh();

    // logging
    m_executivePool.forEach(MessageHint::ALL, [](int64_t, ExecutiveState::Ptr executiveState) {
        DMC_LOG(TRACE) << " 1.PendingMsg: \t\t [--] " << executiveState->toString();
        return true;
    });

    // prepare all that need to
    m_executivePool.forEachAndClear(ExecutivePool::MessageHint::NEED_PREPARE,
        [this](int64_t contextID, ExecutiveState::Ptr executiveState) {
            auto hint = handleExecutiveMessage(executiveState);
            m_executivePool.markAs(contextID, hint);
            DMC_LOG(TRACE) << " 2.AfterPrepare: \t [..] " << executiveState->toString() << " "
                           << ExecutivePool::toString(hint);
            return true;
        });

    bool hasScheduleOut = !m_executivePool.empty(MessageHint::NEED_SCHEDULE_OUT);

    // handle schedule out message
    m_executivePool.forEach(ExecutivePool::MessageHint::NEED_SCHEDULE_OUT,
        [this](int64_t, ExecutiveState::Ptr executiveState) {
            scheduleOut(executiveState);
            return true;
        });

    // clear env
    m_executivePool.refresh();
    return hasScheduleOut;
}

bool DmcExecutor::unlockPrepare()
{
    m_executivePool.forEach(
        MessageHint::LOCKED, [this](int64_t contextID, ExecutiveState::Ptr executiveState) {
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
                DMC_LOG(TRACE) << " Waiting key, contract: " << contextID << " | " << seq << " | "
                               << message->from()
                               << " keyLockAcquired: " << toHex(message->keyLockAcquired());
            }
            else
            {
                DMC_LOG(TRACE) << " Wait key lock success, " << contextID << " | " << seq << " | "
                               << message->from()
                               << " keyLockAcquired: " << toHex(message->keyLockAcquired());

                m_executivePool.markAs(contextID, MessageHint::NEED_SEND);
                DMC_LOG(TRACE) << " 3.AfterPrepare: \t [..] " << executiveState->toString()
                               << " UNLOCK";
            }
            return true;
        });

    return m_executivePool.empty(MessageHint::NEED_SEND);  // has locked tx but nothing to send,
                                                           // need to detect deadlock
}

bool DmcExecutor::detectLockAndRevert()
{
    bool found = false;
    m_executivePool.forEach(
        MessageHint::LOCKED, [this, &found](int64_t contextID, ExecutiveState::Ptr executiveState) {
            if (m_keyLocks->detectDeadLock(contextID))
            {
                auto& message = executiveState->message;

                SCHEDULER_LOG(INFO) << "Detected dead lock at " << contextID << " | "
                                    << message->seq() << " , revert";

                message->setType(protocol::ExecutionMessage::REVERT);
                message->setCreate(false);
                message->setKeyLocks({});

                m_executivePool.markAs(contextID, MessageHint::NEED_SEND);
                DMC_LOG(TRACE) << " 3.AfterPrepare: \t [..] " << executiveState->toString()
                               << " REVERT";
                found = true;
                return false;  // break at once found a tx can be revert
                // just detect one TODO: detect and unlock more deadlock
            }
            else
            {
                return true;  // continue forEach
            }
        });

    return found;  // no detected
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
        m_executivePool.add(
            contextID, std::make_shared<ExecutiveState>(contextID, std::move(message), withDAG));
    }
}

void DmcExecutor::scheduleIn(ExecutiveState::Ptr executive)
{
    assert(executive->message->to() == m_contractAddress);  // message must belongs to this executor
    auto contextID = executive->message->contextID();
    m_executivePool.add(contextID, executive);
}

void DmcExecutor::go(std::function<void(bcos::Error::UniquePtr, Status)> callback)
{
    if (!m_executivePool.empty(MessageHint::NEED_PREPARE))
    {
        callback(nullptr, NEED_PREPARE);
        return;
    }

    if (hasFinished())
    {
        callback(nullptr, FINISHED);
        return;
    }

    if (m_executivePool.empty(MessageHint::NEED_SEND))
    {
        callback(nullptr, PAUSED);
        return;
    }

    assert(f_onSchedulerOut != nullptr);

    auto messages = std::make_shared<std::vector<protocol::ExecutionMessage::UniquePtr>>();

    m_executivePool.forEachAndClear(MessageHint::NEED_SEND,
        [this, messages](int64_t contextID, ExecutiveState::Ptr executiveState) {
            auto& message = executiveState->message;

            auto keyLocks = m_keyLocks->getKeyLocksNotHoldingByContext(message->to(), contextID);
            message->setKeyLocks(std::move(keyLocks));
            DMC_LOG(TRACE) << " 4.SendToExecutor:\t >>>> " << executiveState->toString()
                           << " >>>> [" << m_name << "]:" << m_contractAddress
                           << ", staticCall:" << message->staticCall();
            messages->push_back(std::move(message));

            return true;
        });

    // record all send message for debug
    m_dmcRecorder->recordSends(m_contractAddress, *messages);

    if (messages->size() == 1 && (*messages)[0]->staticCall())
    {
        DMC_LOG(TRACE) << "send call request, address:" << m_contractAddress
                       << LOG_KV("executor", m_name) << LOG_KV("to", message->to())
                       << LOG_KV("contextID", contextID)
                       << LOG_KV("internalCall", message->internalCall())
                       << LOG_KV("type", message->type());
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
        auto lastT = utcTime();
        DMC_LOG(TRACE) << LOG_DESC("dmcExecuteTransactions") << LOG_KV("executor", m_name)
                       << LOG_KV("address", m_contractAddress) << LOG_KV("size", messages->size())
                       << LOG_KV("number", m_block->blockHeader()->number());
        m_executor->dmcExecuteTransactions(m_contractAddress, *messages,
            [this, lastT, messages, callback = std::move(callback)](bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                // update batch
                DMC_LOG(DEBUG) << LOG_BADGE("Stat") << "DMCExecute:\t Executor execute finish"
                               << LOG_KV("name", m_name) << LOG_KV("contract", m_contractAddress)
                               << LOG_KV("txNum", messages->size())
                               << LOG_KV("round", m_dmcRecorder->getRound())
                               << LOG_KV("cost", utcTime() - lastT);

                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Execute transaction error: " << error->errorMessage();

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

void DmcExecutor::handleCreateMessage(ExecutiveState::Ptr executiveState)
{
    auto& message = executiveState->message;
    auto contextID = message->contextID();

    if (message->type() == protocol::ExecutionMessage::SEND_BACK)
    {
        // must clear callstack
        executiveState->callStack = std::stack<int64_t, std::list<int64_t>>();

        if (message->transactionHash() != h256(0))
        {
            message->setType(protocol::ExecutionMessage::TXHASH);
        }
        else
        {
            message->setType(protocol::ExecutionMessage::MESSAGE);
        }
    }

    switch (message->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    {
        if (message->to().empty())
        {
            auto newSeq = executiveState->currentSeq;
            if (message->createSalt())
            {
                // TODO: Add sender in this process(consider compat with ethereum)
                message->setTo(
                    newEVMAddress(message->from(), message->data(), *(message->createSalt())));
            }
            else
            {
                // TODO: Add sender in this process(consider compat with ethereum)
                message->setTo(
                    newEVMAddress(m_block->blockHeaderConst()->number(), contextID, newSeq));
            }
        }
        break;
    }
    default:
        break;
    }
}

DmcExecutor::MessageHint DmcExecutor::handleExecutiveMessage(ExecutiveState::Ptr executiveState)
{
    // handle create message first, create address and convert to normal message
    handleCreateMessage(executiveState);

    // handle normal message
    auto& message = executiveState->message;

    if (message->to() != m_contractAddress)
    {
        return MessageHint::NEED_SCHEDULE_OUT;
    }

    switch (message->type())
    {
        // Request type, push stack
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    {
        // update my key locks in m_keyLocks
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

        auto newSeq = executiveState->currentSeq++;
        executiveState->callStack.push(newSeq);
        executiveState->message->setSeq(newSeq);

        return MessageHint::NEED_SEND;
    }
        // Return type, pop stack
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        executiveState->callStack.pop();
        if (executiveState->callStack.empty())
        {
            // Empty stack, execution is finished
            f_onTxFinished(std::move(message));
            return MessageHint::END;
        }
        else
        {
            message->setSeq(executiveState->callStack.top());
            message->setCreate(false);
            return MessageHint::NEED_SEND;
        }
    }
        // Retry type, send again
    case protocol::ExecutionMessage::KEY_LOCK:
    {
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

        executiveState->message = std::move(message);

        return MessageHint::LOCKED;
    }
    default:
    {
        DMC_LOG(FATAL) << "Unrecognized message type: " << message->toString();
        assert(false);
        break;
    }
    }

    // never goes here
    DMC_LOG(FATAL) << "Message end with illegal manner" << message->toString();
    assert(false);
    return MessageHint::END;
}

void DmcExecutor::handleExecutiveOutputs(
    std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs)
{
    m_dmcRecorder->recordReceives(m_contractAddress, outputs);

    for (auto& output : outputs)
    {
        std::string to = {output->to().data(), output->to().size()};
        auto contextID = output->contextID();

        // bcos::ReadGuard lock(x_concurrentLock);
        ExecutiveState::Ptr executiveState = m_executivePool.get(contextID);
        executiveState->message = std::move(output);
        DMC_LOG(TRACE) << " 5.RevFromExecutor: <<<< [" << m_name << "]:" << m_contractAddress
                       << " <<<< " << executiveState->toString();
        if (to == m_contractAddress)
        {
            // bcos::ReadGuard lock(x_concurrentLock);
            // is my output
            m_executivePool.markAs(contextID, MessageHint::NEED_PREPARE);
        }
        else
        {
            m_executivePool.markAs(contextID, MessageHint::NEED_SCHEDULE_OUT);
            scheduleOut(executiveState);
        }
    }
}

void DmcExecutor::scheduleOut(ExecutiveState::Ptr executiveState)
{
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
