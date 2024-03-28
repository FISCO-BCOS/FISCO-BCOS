#include "PromiseTransactionExecutive.h"
#include <bcos-framework/executor/ExecuteError.h>

using namespace bcos::executor;
CallParameters::UniquePtr PromiseTransactionExecutive::start(CallParameters::UniquePtr input)
{
    auto spawnCall = [this, &input]() {
        auto& blockContext = m_blockContext;

        if (!input->keyLocks.empty())
        {
            m_syncStorageWrapper->importExistsKeyLocks(input->keyLocks);
        }

        auto exchangeMessage = execute(std::move(input));

        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_dmc_revert))
        {
            exchangeMessage =
                PromiseTransactionExecutive::waitingFinish(std::move(exchangeMessage));
        }

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

    if (m_blockContext.features().get(ledger::Features::Flag::bugfix_call_noaddr_return))
    {
        if (output->delegateCall &&
            output->status == (int32_t)bcos::protocol::TransactionStatus::CallAddressError)
        {
            // This is eth's bug, but we still need to compat with it :)
            // https://docs.soliditylang.org/en/v0.8.17/control-structures.html#error-handling-assert-require-revert-and-exceptions
            output->data = bytes();
            output->type = CallParameters::FINISHED;
            output->status = (int32_t)bcos::protocol::TransactionStatus::None;
            output->evmStatus = EVMC_SUCCESS;

            EXECUTIVE_LOG(DEBUG) << "Could not getCode during DMC externalCall, but return success"
                                 << LOG_KV("codeAddress", output->codeAddress)
                                 << LOG_KV("status", output->status)
                                 << LOG_KV("evmStatus", output->evmStatus);
        }
    }
    else
    {
        if (output->delegateCall && output->type != CallParameters::FINISHED)
        {
            EXECUTIVE_LOG(DEBUG) << "Could not getCode during DMC externalCall"
                                 << LOG_KV("codeAddress", output->codeAddress);
            output->data = bytes();
            output->status = (int32_t)bcos::protocol::TransactionStatus::RevertInstruction;
            output->evmStatus = EVMC_REVERT;
        }
    }

    if (versionCompareTo(m_blockContext.blockVersion(), protocol::BlockVersion::V3_3_VERSION) >= 0)
    {
        if (output->type == CallParameters::REVERT)
        {
            // fix the bug here
            output->evmStatus = EVMC_REVERT;
        }
    }


    // After coroutine switch, set the recoder
    m_syncStorageWrapper->setRecoder(m_recoder);

    // Set the keyLocks
    m_syncStorageWrapper->importExistsKeyLocks(output->keyLocks);

    return output;
}

CallParameters::UniquePtr PromiseTransactionExecutive::waitingFinish(
    CallParameters::UniquePtr input)
{
    if (input->type != CallParameters::FINISHED
        // seq == 0 no need to waiting, just return
        || input->seq == 0)
    {
        // only finish need to waiting
        return input;
    }

    std::string returnAddress = input->senderAddress;

    input->type = CallParameters::PRE_FINISH;
    input->keyLocks = m_syncStorageWrapper->exportKeyLocks();

    CallParameters::UniquePtr output;
    m_messageSwapper->spawnAndCall([&input]() { return std::move(input); },
        [&output](CallParameters::UniquePtr message) {
            // When resume, exchangeMessage set to output
            output = std::move(message);
        });

    if (output->type == CallParameters::REVERT)
    {
        revert();
    }
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
