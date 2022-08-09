
#include "ExecutiveSerialFlow.h"
#include "TransactionExecutive.h"
#include <bcos-framework/executor/ExecuteError.h>

using namespace bcos;
using namespace bcos::executor;

void ExecutiveSerialFlow::submit(CallParameters::UniquePtr txInput)
{
    WriteGuard lock(x_lock);

    auto contextID = txInput->contextID;

    if (m_txInputs == nullptr)
    {
        m_txInputs = std::make_shared<SerialMap>();
    }

    (*m_txInputs)[contextID] = std::move(txInput);
}

void ExecutiveSerialFlow::submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    WriteGuard lock(x_lock);
    if (m_txInputs == nullptr)
    {
        m_txInputs = std::make_shared<SerialMap>();
    }

    for (auto& txInput : *txInputs)
    {
        auto contextID = txInput->contextID;
        (*m_txInputs)[contextID] = std::move(txInput);
    }
}

void ExecutiveSerialFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    asyncTo([this, onTxReturn = std::move(onTxReturn), onFinished = std::move(onFinished)]() {
        try
        {
            run(onTxReturn, onFinished);
        }
        catch (std::exception& e)
        {
            onFinished(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                "ExecutiveSerialFlow asyncRun exception:" + std::string(e.what())));
        }
    });
}

void ExecutiveSerialFlow::run(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    try
    {
        std::shared_ptr<SerialMap> blockTxs = nullptr;

        {
            bcos::WriteGuard lock(x_lock);
            blockTxs = std::move(m_txInputs);
        }

        for (auto it = blockTxs->begin(); it != blockTxs->end(); it++)
        {
            auto contextID = it->first;
            auto& txInput = it->second;
            if (!txInput)
            {
                EXECUTIVE_LOG(WARNING) << "Ignore tx[" << contextID << "] with empty message";
                continue;
            }

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
