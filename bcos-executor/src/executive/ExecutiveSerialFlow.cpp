
#include "ExecutiveSerialFlow.h"
#include "TransactionExecutive.h"

using namespace bcos;
using namespace bcos::executor;

void ExecutiveSerialFlow::submit(CallParameters::UniquePtr txInput)
{
    WriteGuard lock(x_lock);

    auto contextID = txInput->contextID;

    if (m_txInputs == nullptr)
    {
        m_txInputs = std::make_shared<std::vector<CallParameters::UniquePtr>>();
    }

    if (m_txInputs->size() <= contextID)
    {
        m_txInputs->resize(contextID + 1);
    }

    (*m_txInputs)[contextID] = std::move(txInput);
}

void ExecutiveSerialFlow::submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    WriteGuard lock(x_lock);

    m_txInputs = txInputs;
}

void ExecutiveSerialFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    asyncTo([this, onTxReturn = std::move(onTxReturn), onFinished = std::move(onFinished)]() {
        run(onTxReturn, onFinished);
    });
}

void ExecutiveSerialFlow::run(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    try
    {
        std::shared_ptr<std::vector<CallParameters::UniquePtr>> blockTxs = nullptr;

        {
            bcos::WriteGuard lock(x_lock);
            blockTxs = std::move(m_txInputs);
        }


        for (size_t id = 0; id < blockTxs->size(); id++)
        {
            auto& txInput = (*blockTxs)[id];
            if (!txInput)
            {
                EXECUTIVE_LOG(WARNING) << "Ignore tx[" << id << "] with empty message";
                continue;
            }

            auto contextID = txInput->contextID;
            auto seq = txInput->seq;
            // build executive
            auto executive = m_executiveFactory->build(
                txInput->codeAddress, txInput->contextID, txInput->seq, false);


            // run evm
            CallParameters::UniquePtr output = executive->start(std::move(txInput));

            // set result
            output->contextID = contextID;
            output->seq = seq;

            // call back
            onTxReturn(std::move(output));
        }

        onFinished(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << "ExecutiveSerialFlow run error: "
                             << boost::diagnostic_information(e);
        onFinished(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "ExecutiveSerialFlow run error", e));
    }
}
