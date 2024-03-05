#include "CoroutineTransactionExecutive.h"
#include <bcos-framework/executor/ExecuteError.h>

using namespace bcos::executor;
CallParameters::UniquePtr CoroutineTransactionExecutive::start(CallParameters::UniquePtr input)
{
    m_pullMessage.emplace([this, inputPtr = input.release()](Coroutine::push_type& push) {
        COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq) << "Create new coroutine";

        // Take ownership from input
        getPushMessage().emplace(std::move(push));

        auto callParameters = std::unique_ptr<CallParameters>(inputPtr);


        if (!callParameters->keyLocks.empty())
        {
            m_syncStorageWrapper->importExistsKeyLocks(callParameters->keyLocks);
        }

        getExchangeMessageRef() = execute(std::move(callParameters));

        if (m_blockContext.features().get(ledger::Features::Flag::bugfix_dmc_revert))
        {
            getExchangeMessageRef() =
                CoroutineTransactionExecutive::waitingFinish(std::move(getExchangeMessageRef()));
        }

        // Execute is finished, erase the key locks
        getExchangeMessageRef()->keyLocks.clear();

        // Return the ownership to input
        push = std::move(*getPushMessage());

        COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq) << "Finish coroutine executing";
    });

    return dispatcher();
}

CallParameters::UniquePtr CoroutineTransactionExecutive::dispatcher()
{
    try
    {
        for (auto it = RANGES::begin(*getPullMessage()); it != RANGES::end(*getPullMessage()); ++it)
        {
            if (*it)
            {
                COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq)
                    << "Context switch to main coroutine to call func";
                (*it)(ResumeHandler(*this));
            }

            if (getExchangeMessageRef())
            {
                COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq)
                    << "Context switch to main coroutine to return output";
                return std::move(getExchangeMessageRef());
            }
        }
    }
    catch (std::exception& e)
    {
        COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq)
            << "Error while dispatch, " << boost::diagnostic_information(e);
        BOOST_THROW_EXCEPTION(BCOS_ERROR_WITH_PREV(-1, "Error while dispatch", e));
    }

    COROUTINE_TRACE_LOG(TRACE, m_contextID, m_seq) << "Context switch to main coroutine, Finished!";
    return std::move(getExchangeMessageRef());
}

CallParameters::UniquePtr CoroutineTransactionExecutive::externalCall(
    CallParameters::UniquePtr input)
{
    input->keyLocks = m_syncStorageWrapper->exportKeyLocks();

    spawnAndCall([this, inputPtr = input.release()](ResumeHandler) {
        getExchangeMessageRef() = CallParameters::UniquePtr(inputPtr);
    });

    // When resume, exchangeMessage set to output
    auto output = std::move(getExchangeMessageRef());

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
            output->data = bytes();
            output->status = (int32_t)bcos::protocol::TransactionStatus::RevertInstruction;
            output->evmStatus = EVMC_REVERT;

            EXECUTIVE_LOG(DEBUG) << "Could not getCode during DMC externalCall"
                                 << LOG_KV("codeAddress", output->codeAddress)
                                 << LOG_KV("status", output->status)
                                 << LOG_KV("evmStatus", output->evmStatus);
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

CallParameters::UniquePtr CoroutineTransactionExecutive::waitingFinish(
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

    spawnAndCall([this, inputPtr = input.release()](ResumeHandler) {
        getExchangeMessageRef() = CallParameters::UniquePtr(inputPtr);
    });


    // When resume, exchangeMessage set to output
    auto output = std::move(getExchangeMessageRef());
    if (output->type == CallParameters::REVERT)
    {
        revert();
    }
    return output;
}

void CoroutineTransactionExecutive::externalAcquireKeyLocks(std::string acquireKeyLock)
{
    EXECUTOR_LOG(TRACE) << "Executor acquire key lock: " << acquireKeyLock;

    auto callParameters = std::make_unique<CallParameters>(CallParameters::KEY_LOCK);
    callParameters->senderAddress = m_contractAddress;
    callParameters->keyLocks = m_syncStorageWrapper->exportKeyLocks();
    callParameters->acquireKeyLock = std::move(acquireKeyLock);

    spawnAndCall([this, inputPtr = callParameters.release()](ResumeHandler) {
        getExchangeMessageRef() = CallParameters::UniquePtr(inputPtr);
    });

    // After coroutine switch, set the recoder, before the exception throw
    m_syncStorageWrapper->setRecoder(m_recoder);

    auto output = std::move(getExchangeMessageRef());
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

void CoroutineTransactionExecutive::spawnAndCall(std::function<void(ResumeHandler)> function)
{
    (*getPushMessage())(std::move(function));
}
