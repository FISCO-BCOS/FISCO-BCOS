//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ExecutiveDagFlow.h"
#include "../dag/ClockCache.h"
#include "../dag/ScaleUtils.h"
#include "../vm/Precompiled.h"
#include "TransactionExecutive.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>


using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace std;


void ExecutiveDagFlow::submit(CallParameters::UniquePtr txInput)
{
    // m_inputs must has been initialized
    assert(m_inputs);

    bcos::RecursiveGuard lock(x_lock);
    if (m_pausedExecutive)
    {
        DAGFLOW_LOG(DEBUG) << "submit: resume tx" << txInput->toString();
        // is tx to resume
        assert(m_pausedExecutive->getContextID() == txInput->contextID);
        m_pausedExecutive->setResumeParam(std::move(txInput));
        return;
    }

    DAGFLOW_LOG(DEBUG) << "submit: new tx" << txInput->toString();
    // is new tx
    auto contextID = txInput->contextID;

    // contextID must valid
    assert(contextID < (int64_t)m_inputs->size());

    (*m_inputs)[contextID] = std::move(txInput);
}


void ExecutiveDagFlow::submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    bcos::RecursiveGuard lock(x_lock);
    if (m_dagFlow)
    {
        // m_dagFlow has been set before this function call, just use it
    }
    else
    {
        // generate m_dagFlow
        m_dagFlow = prepareDagFlow(*m_executiveFactory->getBlockContext().lock(),
            m_executiveFactory, *txInputs, m_abiCache);
    }

    if (!m_inputs)
    {
        // is first in
        m_inputs = txInputs;
    }
    else
    {
        // is dmc resume input
        for (auto& txInput : *txInputs)
        {
            submit(std::move(txInput));
        }
    }
}

void ExecutiveDagFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    try
    {
        auto self = std::weak_ptr<ExecutiveDagFlow>(shared_from_this());
        asyncTo([self, onTxReturn = std::move(onTxReturn), onFinished = std::move(onFinished)]() {
            try
            {
                auto flow = self.lock();
                if (flow)
                {
                    flow->run(onTxReturn, onFinished);
                }
            }
            catch (std::exception& e)
            {
                onFinished(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                    "ExecutiveDagFlow asyncRun exception:" + std::string(e.what())));
            }
        });
    }
    catch (std::exception const& e)
    {
        onFinished(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
            "ExecutiveDagFlow asyncTo exception:" + std::string(e.what())));
    }
}

void ExecutiveDagFlow::run(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    try
    {
        if (!m_isRunning)
        {
            DAGFLOW_LOG(DEBUG) << "ExecutiveDagFlow has stopped during running";
            onFinished(BCOS_ERROR_UNIQUE_PTR(
                ExecuteError::STOPPED, "ExecutiveDagFlow has stopped during running"));
            return;
        }


        m_dagFlow->setExecuteTxFunc([this](ID id) {
            try
            {
                DAGFLOW_LOG(DEBUG) << LOG_DESC("Execute tx: start") << LOG_KV("id", id);
                if (!m_isRunning)
                {
                    return;
                }

                CallParameters::UniquePtr output;
                bool isDagTx = m_dagFlow->isDagTx(id);

                if (isDagTx) [[likely]]
                {
                    auto& input = (*m_inputs)[id];
                    DAGFLOW_LOG(TRACE) << LOG_DESC("Execute tx: start DAG tx") << input->toString()
                                       << LOG_KV("to", input->receiveAddress)
                                       << LOG_KV("data", toHexStringWithPrefix(input->data));

                    // dag tx no need to use coroutine
                    auto executive = m_executiveFactory->build(
                        input->codeAddress, input->contextID, input->seq, false);

                    output = executive->start(std::move(input));

                    DAGFLOW_LOG(DEBUG) << "execute tx finish DAG " << output->toString();
                }
                else
                {
                    // run normal tx
                    ExecutiveState::Ptr executiveState;
                    if (!m_pausedExecutive)
                    {
                        auto& input = (*m_inputs)[id];
                        DAGFLOW_LOG(TRACE)
                            << LOG_DESC("Execute tx: start normal tx") << input->toString()
                            << LOG_KV("to", input->receiveAddress)
                            << LOG_KV("data", toHexStringWithPrefix(input->data));

                        executiveState =
                            std::make_shared<ExecutiveState>(m_executiveFactory, std::move(input));
                    }
                    else
                    {
                        executiveState = std::move(m_pausedExecutive);

                        DAGFLOW_LOG(TRACE)
                            << LOG_DESC("execute tx resume ") << executiveState->getContextID()
                            << " | " << executiveState->getSeq();
                    }

                    auto seq = executiveState->getSeq();
                    output = executiveState->go();

                    // set result
                    output->contextID = id;
                    output->seq = seq;

                    if (output->type == CallParameters::MESSAGE ||
                        output->type == CallParameters::KEY_LOCK)
                    {
                        m_pausedExecutive = std::move(executiveState);
                        // call back
                        DAGFLOW_LOG(DEBUG)
                            << "Execute tx: normal externalCall " << output->toString();
                        m_dagFlow->pause();
                    }
                    else
                    {
                        DAGFLOW_LOG(DEBUG) << "Execute tx: normal finish " << output->toString();
                    }
                }
                f_onTxReturn(std::move(output));
            }
            catch (std::exception& e)
            {
                DAGFLOW_LOG(ERROR) << "executeTransactionsWithCriticals error: "
                                   << boost::diagnostic_information(e);
            }
        });

        f_onTxReturn = std::move(onTxReturn);
        m_dagFlow->run(m_DAGThreadNum);
        f_onTxReturn = nullptr;  // must deconstruct for ptr holder release

        if (m_dagFlow->hasFinished())
        {
            // clear input data
            m_inputs = nullptr;
            m_dagFlow = nullptr;
        }

        onFinished(nullptr);
    }
    catch (std::exception& e)
    {
        DAGFLOW_LOG(ERROR) << "ExecutiveDagFlow run error: " << boost::diagnostic_information(e);
        f_onTxReturn = nullptr;
        onFinished(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "ExecutiveDagFlow run error", e));
    }
}
//*/


critical::CriticalFieldsInterface::Ptr ExecutiveDagFlow::generateDagCriticals(
    BlockContext& blockContext, ExecutiveFactory::Ptr executiveFactory,
    std::vector<CallParameters::UniquePtr>& inputs,
    std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> abiCache)
{
    return nullptr;
}


TxDAGFlow::Ptr ExecutiveDagFlow::generateDagFlow(
    const BlockContext& blockContext, critical::CriticalFieldsInterface::Ptr criticals)
{
    auto dagFlow = std::make_shared<TxDAGFlow>();

    dagFlow->init(criticals);
    return dagFlow;
}


bytes ExecutiveDagFlow::getComponentBytes(
    size_t index, const std::string& typeName, const bytesConstRef& data)
{
    return {};
}

std::shared_ptr<std::vector<bytes>> ExecutiveDagFlow::extractConflictFields(
    const FunctionAbi& functionAbi, const CallParameters& params, const BlockContext& _blockContext)
{
    return nullptr;
}
