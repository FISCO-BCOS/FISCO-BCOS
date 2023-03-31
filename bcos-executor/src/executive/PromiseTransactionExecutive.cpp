#include "PromiseTransactionExecutive.h"
#include <bcos-framework/executor/ExecuteError.h>

using namespace bcos::executor;
CallParameters::UniquePtr PromiseTransactionExecutive::start(CallParameters::UniquePtr input)
{
    auto spawnCall = [this, &input]() {
        auto& blockContext = m_blockContext;
        m_syncStorageWrapper = std::make_unique<SyncStorageWrapper>(blockContext.storage(),
            std::bind(
                &PromiseTransactionExecutive::externalAcquireKeyLocks, this, std::placeholders::_1),
            m_recoder);

        m_storageWrapper = m_syncStorageWrapper;  // must set to base class

        if (!input->keyLocks.empty())
        {
            m_syncStorageWrapper->importExistsKeyLocks(input->keyLocks);
        }

        auto exchangeMessage = execute(std::move(input));
        if (exchangeMessage->type == CallParameters::Type::FINISHED)
        {
            // Execute is finished, erase the key locks
            exchangeMessage->keyLocks.clear();
        }
        return exchangeMessage;
    };

    CallParameters::UniquePtr exchangeMessage;
    auto waitAndDo = [&exchangeMessage](CallParameters::UniquePtr message) {
        exchangeMessage = std::move(message);
    };

    m_messageSwapper->spawnAndCall(spawnCall, waitAndDo);

    return exchangeMessage;
}

CallParameters::UniquePtr PromiseTransactionExecutive::externalCall(CallParameters::UniquePtr input)
{
    input->keyLocks = m_syncStorageWrapper->exportKeyLocks();

    CallParameters::UniquePtr output;
    m_messageSwapper->spawnAndCall([&input]() { return std::move(input); },
        [&output](CallParameters::UniquePtr message) {
            // When resume, exchangeMessage set to output
            output = std::move(message);
        });

    if (output->delegateCall && output->type != CallParameters::FINISHED)
    {
        EXECUTIVE_LOG(DEBUG) << "Could not getCode during DMC externalCall"
                             << LOG_KV("codeAddress", output->codeAddress);
        output->data = bytes();
        output->status = (int32_t)bcos::protocol::TransactionStatus::RevertInstruction;
        output->evmStatus = EVMC_REVERT;
    }

    // After coroutine switch, set the recoder
    m_syncStorageWrapper->setRecoder(m_recoder);

    // Set the keyLocks
    m_syncStorageWrapper->importExistsKeyLocks(output->keyLocks);

    return output;
}

void PromiseTransactionExecutive::externalAcquireKeyLocks(std::string acquireKeyLock)
{
    EXECUTOR_LOG(TRACE) << "Executor acquire key lock: " << acquireKeyLock;

    auto callParameters = std::make_unique<CallParameters>(CallParameters::KEY_LOCK);
    callParameters->senderAddress = m_contractAddress;
    callParameters->keyLocks = m_syncStorageWrapper->exportKeyLocks();
    callParameters->acquireKeyLock = std::move(acquireKeyLock);

    CallParameters::UniquePtr output;
    m_messageSwapper->spawnAndCall([&callParameters]() { return std::move(callParameters); },
        [&output](CallParameters::UniquePtr message) {
            // When resume, exchangeMessage set to output
            output = std::move(message);
        });

    // After coroutine switch, set the recoder, before the exception throw
    m_syncStorageWrapper->setRecoder(m_recoder);

    if (output->type == CallParameters::REVERT)
    {
        // Deadlock, revert
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            ExecuteError::DEAD_LOCK, "Dead lock detected, revert transaction: " +
                                         boost::lexical_cast<std::string>(output->type)));
    }

    // Set the keyLocks
    m_syncStorageWrapper->importExistsKeyLocks(output->keyLocks);
}
