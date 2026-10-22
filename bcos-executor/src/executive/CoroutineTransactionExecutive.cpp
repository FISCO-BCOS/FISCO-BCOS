#include "CoroutineTransactionExecutive.h"
#include "bcos-framework/executor/ExecuteError.h"

using namespace bcos::executor;

CoroutineTransactionExecutive::ResumeHandler::ResumeHandler(
    CoroutineTransactionExecutive& executive)
  : m_executive(executive)
{}

void CoroutineTransactionExecutive::ResumeHandler::operator()()
{
    COROUTINE_TRACE_LOG(TRACE, m_executive.contextID(), m_executive.seq())
        << "Context switch to executive coroutine, from ResumeHandler";
    (*m_executive.m_pullMessage)();
}

CoroutineTransactionExecutive::CoroutineTransactionExecutive(
    const BlockContext& blockContext, std::string contractAddress, int64_t contextID, int64_t seq)
  : TransactionExecutive(std::move(blockContext), std::move(contractAddress), contextID, seq),
    m_syncStorageWrapper(std::make_shared<SyncStorageWrapper>(
        m_blockContext.storage(),
        [this](auto&& PH1) { externalAcquireKeyLocks(std::forward<decltype(PH1)>(PH1)); },
        m_recoder))
{
    // R5 F1: before the first scheduler hand-back, the derivation base is the entry
    // seq — the same value the scheduler's empty-to arm derived with on the base.
    m_schedulerLastSeq = seq;
    m_resumedWithoutSchedulerHandback = false;
    m_storageWrapper = m_syncStorageWrapper.get();
}

void CoroutineTransactionExecutive::setExchangeMessage(CallParameters::UniquePtr callParameters)
{
    getExchangeMessageRef() = std::move(callParameters);
}

std::string CoroutineTransactionExecutive::getExchangeMessageStr()
{
    if (getExchangeMessageRef())
    {
        return getExchangeMessageRef()->toString();
    }

    return "[empty exchange message]";
}

void CoroutineTransactionExecutive::appendResumeKeyLocks(std::vector<std::string> keyLocks)
{
    std::copy(
        keyLocks.begin(), keyLocks.end(), std::back_inserter(getExchangeMessageRef()->keyLocks));
}

CallParameters::UniquePtr CoroutineTransactionExecutive::resume()
{
    EXECUTOR_LOG(TRACE) << "Context switch to executive coroutine, from resume";
    (*m_pullMessage)();

    return dispatcher();
}

std::optional<CoroutineTransactionExecutive::Coroutine::pull_type>&
CoroutineTransactionExecutive::getPullMessage()
{
    return m_pullMessage;
}

std::optional<CoroutineTransactionExecutive::Coroutine::push_type>&
CoroutineTransactionExecutive::getPushMessage()
{
    return m_pushMessage;
}

CallParameters::UniquePtr& CoroutineTransactionExecutive::getExchangeMessageRef()
{
    return m_exchangeMessage;
}

std::shared_ptr<SyncStorageWrapper> CoroutineTransactionExecutive::getSyncStorageWrapper()
{
    return m_syncStorageWrapper;
}

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
            getExchangeMessageRef() = waitingFinish(std::move(getExchangeMessageRef()));
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
        for (auto it = ::ranges::begin(*getPullMessage()); it != ::ranges::end(*getPullMessage());
            ++it)
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

    auto output = suspendAndWait(std::move(input));

    onExternalCallResume(*output);

    // After coroutine switch, set the recoder
    m_syncStorageWrapper->setRecoder(m_recoder);

    // Set the keyLocks
    m_syncStorageWrapper->importExistsKeyLocks(output->keyLocks);

    return output;
}

void CoroutineTransactionExecutive::onExternalCallResume(CallParameters& output)
{
    // Both coroutine and Promise receivers must adopt the scheduler hand-back.
    // A missing (-1) stamp marks the frame so the next CREATE derivation refuses
    // to guess; zero is a valid last-consumed seq.
    adoptSchedulerSeqHandback(output.schedulerSeq);
    normalizeExternalCallResult(output);
}

void CoroutineTransactionExecutive::normalizeExternalCallResult(CallParameters& output) const
{
    // PR #5689 F1: preserve the child host-fault signal through legacy revert
    // normalization (including DELEGATECALL), so the parent can fail the whole frame.
    if (output.evmStatus == EVMC_INTERNAL_ERROR &&
        output.status == (int32_t)bcos::protocol::TransactionStatus::RevertInstruction)
    {
        return;
    }
    if (m_blockContext.features().get(ledger::Features::Flag::bugfix_call_noaddr_return))
    {
        if (output.delegateCall &&
            output.status == (int32_t)bcos::protocol::TransactionStatus::CallAddressError)
        {
            // This is eth's bug, but we still need to compat with it :)
            // https://docs.soliditylang.org/en/v0.8.17/control-structures.html#error-handling-assert-require-revert-and-exceptions
            output.data = bytes();
            output.type = CallParameters::FINISHED;
            output.status = (int32_t)bcos::protocol::TransactionStatus::None;
            output.evmStatus = EVMC_SUCCESS;

            EXECUTIVE_LOG(DEBUG) << "Could not getCode during DMC externalCall, but return success"
                                 << LOG_KV("codeAddress", output.codeAddress)
                                 << LOG_KV("status", output.status)
                                 << LOG_KV("evmStatus", output.evmStatus);
        }
    }
    else
    {
        if (output.delegateCall && output.type != CallParameters::FINISHED)
        {
            output.data = bytes();
            output.status = (int32_t)bcos::protocol::TransactionStatus::RevertInstruction;
            output.evmStatus = EVMC_REVERT;

            EXECUTIVE_LOG(DEBUG) << "Could not getCode during DMC externalCall"
                                 << LOG_KV("codeAddress", output.codeAddress)
                                 << LOG_KV("status", output.status)
                                 << LOG_KV("evmStatus", output.evmStatus);
        }
    }

    if (versionCompareTo(m_blockContext.blockVersion(), protocol::BlockVersion::V3_3_VERSION) >= 0)
    {
        if (output.type == CallParameters::REVERT)
        {
            // fix the bug here
            output.evmStatus = EVMC_REVERT;
        }
    }
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

    input->type = CallParameters::PRE_FINISH;
    input->keyLocks = m_syncStorageWrapper->exportKeyLocks();

    // This frame has finished; no further CREATE derivation needs the resume stamp.
    auto output = suspendAndWait(std::move(input));
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

    auto output = suspendAndWait(std::move(callParameters));

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

CallParameters::UniquePtr CoroutineTransactionExecutive::suspendAndWait(
    CallParameters::UniquePtr input)
{
    spawnAndCall([this, inputPtr = input.release()](ResumeHandler) {
        getExchangeMessageRef() = CallParameters::UniquePtr(inputPtr);
    });
    return std::move(getExchangeMessageRef());
}

void CoroutineTransactionExecutive::spawnAndCall(std::function<void(ResumeHandler)> function)
{
    (*getPushMessage())(std::move(function));
}

int64_t CoroutineTransactionExecutive::createDerivationSeq() const
{
    if (m_resumedWithoutSchedulerHandback)
    {
        // R6 F2: an un-upgraded scheduler paired with this executor cannot provide
        // the seq the base derived from; refuse loudly (the create hook translates
        // the throw into a halting host fault — the node stops, it does not guess).
        BOOST_THROW_EXCEPTION(BCOS_ERROR(-1,
            "DMC resumed frame carries no schedulerSeq hand-back: the paired scheduler "
            "is older than this executor and the base's nested-create derivation "
            "cannot be reproduced"));
    }
    return m_schedulerLastSeq;
}
