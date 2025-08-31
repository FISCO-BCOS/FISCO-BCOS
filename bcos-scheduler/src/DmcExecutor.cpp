#include "DmcExecutor.h"
#include "bcos-crypto/bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/executor/ExecuteError.h"
#include <bcos-protocol/TransactionStatus.h>


using namespace bcos::scheduler;

void DmcExecutor::releaseOutdatedLock()
{
    m_executivePool.forEach(
        MessageHint::NEED_PREPARE, [this](int64_t, ExecutiveState::Ptr executiveState) {
            auto& message = executiveState->message;
            if (message->type() == bcos::protocol::ExecutionMessage::FINISHED ||
                (message->type() == bcos::protocol::ExecutionMessage::REVERT &&
                    !executiveState->isRevertStackMessage))
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
    if (c_fileLogLevel == LogLevel::TRACE)
    {
        m_executivePool.forEach(MessageHint::ALL, [](int64_t, ExecutiveState::Ptr executiveState) {
            DMC_LOG(TRACE) << " 1.PendingMsg: \t\t [--] " << executiveState->toString();
            return true;
        });
    }

    // prepare all that need to
    m_executivePool.forEachAndClear(ExecutivePool::MessageHint::NEED_PREPARE,
        [this](int64_t contextID, ExecutiveState::Ptr executiveState) {
            DmcExecutor::MessageHint hint;
            if (m_enablePreFinishType)
            {
                hint = handleExecutiveMessageV2(executiveState);
            }
            else
            {
                hint = handleExecutiveMessage(executiveState);
            }

            m_executivePool.markAs(contextID, hint);
            DMC_LOG(TRACE) << " 2.AfterPrepare: \t [..] " << executiveState->toString() << " "
                           << ExecutivePool::toString(hint);
            return true;
        });

    bool hasScheduleOut = !m_executivePool.empty(MessageHint::NEED_SCHEDULE_OUT) ||
                          !m_executivePool.empty(MessageHint::NEED_PREPARE);

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

            // Try to acquire key lock
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
                // just detect one
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
    // assert(executive->message->to() == m_contractAddress);  // message must belongs to this
    // executor
    auto contextID = executive->message->contextID();
    m_executivePool.add(contextID, executive);
}

void DmcExecutor::executorCall(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    m_executor->dmcCall(std::move(input), std::move(callback));
}

void DmcExecutor::executorExecuteTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,

    // called every time at all tx stop( pause or finish)
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    m_executor->dmcExecuteTransactions(
        std::move(contractAddress), std::move(inputs), std::move(callback));
}

void DmcExecutor::go(std::function<void(bcos::Error::UniquePtr, Status)> callback)
{
    /*
     this code may lead to inconsistency, because in parallel for go(),
     some message sent by other DMCExecutor will be executed in executor and return before this
     instance go(), so some needs send messages will be ignored in the code below

    if (!m_executivePool.empty(MessageHint::NEED_PREPARE))
    {
        callback(nullptr, NEED_PREPARE);
        return;
    }
    */

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
            if (!message)
            {
                return true;
            }

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

    if (isCall())
    {
        DMC_LOG(TRACE) << "send call request, address:" << m_contractAddress
                       << LOG_KV("executor", m_name) << LOG_KV("to", (*messages)[0]->to())
                       << LOG_KV("contextID", (*messages)[0]->contextID())
                       << LOG_KV("internalCall", (*messages)[0]->internalCall())
                       << LOG_KV("type", (*messages)[0]->type());
        // is static call
        executorCall(std::move((*messages)[0]),
            [this, callback = std::move(callback)](
                bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr output) {
                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Call error: " << boost::diagnostic_information(*error);

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }
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
        DMC_LOG(DEBUG) << LOG_BADGE("Stat") << "DMCExecute.3:\t --> Send to executor\t\t"
                       << LOG_KV("round", m_dmcRecorder->getRound()) << LOG_KV("name", m_name)
                       << LOG_KV("contract", m_contractAddress) << LOG_KV("txNum", messages->size())
                       << LOG_KV("blockNumber", m_block->blockHeader()->number())
                       << LOG_KV("cost", utcTime() - lastT);

        executorExecuteTransactions(m_contractAddress, *messages,
            [this, lastT, messages, callback = std::move(callback)](bcos::Error::UniquePtr error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
                // update batch
                DMC_LOG(DEBUG) << LOG_BADGE("Stat") << "DMCExecute.4:\t <-- Receive from executor\t"
                               << LOG_KV("round", m_dmcRecorder ? m_dmcRecorder->getRound() : 0)
                               << LOG_KV("name", m_name) << LOG_KV("contract", m_contractAddress)
                               << LOG_KV("txNum", messages->size())
                               << LOG_KV("blockNumber", m_block && m_block->blockHeader() ?
                                                            m_block->blockHeader()->number() :
                                                            0)
                               << LOG_KV("cost", utcTime() - lastT);

                if (error)
                {
                    SCHEDULER_LOG(ERROR) << "Execute transaction error: " << error->errorMessage();

                    if (error->errorCode() == bcos::executor::ExecuteError::SCHEDULER_TERM_ID_ERROR)
                    {
                        triggerSwitch();
                    }

                    callback(std::move(error), Status::ERROR);
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

    handleCreateMessage(message, executiveState->currentSeq);
}

void DmcExecutor::handleCreateMessage(
    protocol::ExecutionMessage::UniquePtr& message, int64_t currentSeq)
{
    auto contextID = message->contextID();


    switch (message->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    {
        if (message->to().empty())
        {
            auto newSeq = currentSeq;
            if (message->createSalt())
            {
                message->setTo(
                    newCreate2EVMAddress(message->from(), message->data(), *(message->createSalt())));
            }
            else
            {
                // Note: no need to compatible with EIP-86, because dmc not compatible to eth
                message->setTo(
                    newEVMAddress(m_block->blockHeader()->number(), contextID, newSeq));
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

    if (f_getAddr(message->to()) != m_contractAddress)
    {
        return MessageHint::NEED_SCHEDULE_OUT;
    }

    switch (message->type())
    {
        // Request type, push stack
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    {
        if (executiveState->message->data().toBytes() == bcos::protocol::GET_CODE_INPUT_BYTES)
        {
            auto newSeq = executiveState->currentSeq++;
            executiveState->callStack.push(newSeq);
            executiveState->message->setSeq(newSeq);

            // getCode
            DMC_LOG(DEBUG) << "Get external code in scheduler"
                           << LOG_KV("codeAddress", executiveState->message->delegateCallAddress());
            bytes code = f_onGetCodeEvent(executiveState->message->delegateCallAddress());
            DMC_LOG(TRACE) << "Get external code success in scheduler"
                           << LOG_KV("codeAddress", executiveState->message->delegateCallAddress())
                           << LOG_KV("codeSize", code.size());
            executiveState->message->setData(code);
            executiveState->message->setType(protocol::ExecutionMessage::FINISHED);
            return MessageHint::NEED_PREPARE;
        }

        // update my key locks in m_keyLocks
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

        auto newSeq = executiveState->currentSeq++;
        executiveState->callStack.push(newSeq);
        executiveState->message->setSeq(newSeq);

        if (executiveState->message->delegateCall())
        {
            bytes code = f_onGetCodeEvent(message->delegateCallAddress());
            if (code.empty())
            {
                DMC_LOG(DEBUG)
                    << "Could not getCode() from correspond executor during delegateCall: "
                    << message->toString();
                message->setType(protocol::ExecutionMessage::REVERT);
                message->setStatus((int32_t)protocol::TransactionStatus::CallAddressError);
                message->setCreate(false);
                message->setKeyLocks({});
                return MessageHint::NEED_PREPARE;
            }

            executiveState->message->setDelegateCallCode(code);
        }

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

        message->setSeq(executiveState->callStack.top());
        message->setCreate(false);
        return MessageHint::NEED_SEND;
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

DmcExecutor::MessageHint DmcExecutor::handleExecutiveMessageV2(ExecutiveState::Ptr executiveState)
{
    // handle create message first, create address and convert to normal message
    handleCreateMessage(executiveState);

    // handle normal message
    auto& message = executiveState->message;

    if (f_getAddr(message->to()) != m_contractAddress)
    {
        return MessageHint::NEED_SCHEDULE_OUT;
    }

    switch (message->type())
    {
        // Request type, push stack
    case protocol::ExecutionMessage::MESSAGE:
    case protocol::ExecutionMessage::TXHASH:
    {
        if (executiveState->message->data().toBytes() == bcos::protocol::GET_CODE_INPUT_BYTES)
        {
            // getCode
            DMC_LOG(DEBUG) << "Get external code in scheduler"
                           << LOG_KV("codeAddress", executiveState->message->delegateCallAddress());
            bytes code = f_onGetCodeEvent(executiveState->message->delegateCallAddress());
            DMC_LOG(TRACE) << "Get external code success in scheduler"
                           << LOG_KV("codeAddress", executiveState->message->delegateCallAddress())
                           << LOG_KV("codeSize", code.size());
            executiveState->message->setData(code);
            executiveState->message->setType(protocol::ExecutionMessage::PRE_FINISH);

            executiveState->isRevertStackMessage = true;

            return MessageHint::NEED_PREPARE;
        }

        // update my key locks in m_keyLocks
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

        auto newSeq = executiveState->currentSeq++;
        if (newSeq > 0 && executiveState->callStack.empty())
        {
            // sharding optimize by ignore pushing seq = 0 to stack, we need to push here
            executiveState->callStack.push(0);
        }
        executiveState->callStack.push(newSeq);
        executiveState->revertStack.push({newSeq, std::string(message->to())});
        executiveState->message->setSeq(newSeq);

        if (executiveState->message->delegateCall())
        {
            bytes code = f_onGetCodeEvent(message->delegateCallAddress());
            if (code.empty())
            {
                DMC_LOG(DEBUG)
                    << "Could not getCode() from correspond executor during delegateCall: "
                    << message->toString();
                message->setType(protocol::ExecutionMessage::REVERT);
                message->setStatus((int32_t)protocol::TransactionStatus::CallAddressError);
                message->setCreate(false);
                message->setKeyLocks({});

                executiveState->isRevertStackMessage = true;

                return MessageHint::NEED_PREPARE;
            }

            executiveState->message->setDelegateCallCode(code);
        }

        return MessageHint::NEED_SEND;
    }
        // Return type, pop stack
    case protocol::ExecutionMessage::PRE_FINISH:
    {
        if (executiveState->isRevertStackMessage)
        {
            // handle schedule in message
            executiveState->isRevertStackMessage = false;
            return MessageHint::NEED_SEND;
        }

        // update my key locks in m_keyLocks
        m_keyLocks->batchAcquireKeyLock(
            message->from(), message->keyLocks(), message->contextID(), message->seq());

        if (!executiveState->callStack.empty())
        {
            executiveState->callStack.pop();
        }

        assert(!executiveState->callStack.empty());

        message->setSeq(executiveState->callStack.top());
        message->setCreate(false);
        return MessageHint::NEED_SEND;
    }
    case protocol::ExecutionMessage::FINISHED:
    {
        if (executiveState->isRevertStackMessage)
        {
            // handle schedule in message
            executiveState->isRevertStackMessage = false;
            return MessageHint::NEED_SEND;
        }

        if (executiveState->revertStack.empty()
            // no need to consider
            || executiveState->revertStack.top().first == 0)
        {
            // Empty stack, execution is finished
            f_onTxFinished(std::move(message));
            return MessageHint::END;
        }

        // generate new finish message and scheduler out
        auto [seq, to] = executiveState->revertStack.top();
        executiveState->revertStack.pop();
        executiveState->isRevertStackMessage = true;
        message->setSeq(seq);
        message->setFrom(std::string(message->to()));  // record parent address
        message->setTo(std::string(to));
        message->setCreate(false);
        return MessageHint::NEED_PREPARE;
    }
    case protocol::ExecutionMessage::REVERT:
    {
        if (executiveState->isRevertStackMessage)
        {
            // handle schedule in message
            executiveState->isRevertStackMessage = false;
            return MessageHint::NEED_SEND;
        }

        if (!executiveState->revertStack.empty())
        {
            if (executiveState->callStack.top() < executiveState->revertStack.top().first)
            {
                // need revert child executive
                auto [seq, to] = executiveState->revertStack.top();
                executiveState->revertStack.pop();
                executiveState->isRevertStackMessage = true;
                message->setSeq(seq);
                message->setFrom(std::string(message->to()));  // record parent address
                message->setTo(std::string(to));
                message->setCreate(false);
                return MessageHint::NEED_PREPARE;
            }
            else if (executiveState->callStack.top() == executiveState->revertStack.top().first)
            {
                // just pop this revertStack for this executive has been reverted
                executiveState->revertStack.pop();
            }
        }

        executiveState->callStack.pop();
        if (executiveState->callStack.empty())
        {
            // Empty stack, execution is finished
            f_onTxFinished(std::move(message));
            return MessageHint::END;
        }

        message->setSeq(executiveState->callStack.top());
        message->setCreate(false);
        return MessageHint::NEED_SEND;
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
        if (output->hasContractTableChanged()) [[unlikely]]
        {
            m_hasContractTableChanged = true;
        }

        std::string to = {output->to().data(), output->to().size()};
        auto contextID = output->contextID();

        // bcos::ReadGuard lock(x_concurrentLock);
        ExecutiveState::Ptr executiveState = m_executivePool.get(contextID);
        executiveState->message = std::move(output);
        DMC_LOG(TRACE) << " 5.RecvFromExecutor: <<<< [" << m_name << "]:" << m_contractAddress
                       << " <<<< " << executiveState->toString();
        if (f_getAddr(to) == m_contractAddress)
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

std::string DmcExecutor::newEVMAddress(int64_t blockNumber, int64_t contextID, int64_t seq)
{
    return bcos::newEVMAddress(m_hashImpl, blockNumber, contextID, seq);
}

std::string DmcExecutor::newCreate2EVMAddress(
    const std::string_view& _sender, bytesConstRef _init, u256 const& _salt)
{
    return bcos::newCreate2EVMAddress(m_hashImpl, _sender, _init, _salt);
}
