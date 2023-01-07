//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ExecutiveDagFlow.h"
using namespace bcos::executor;

std::shared_ptr<TransactionExecutive> ExecutiveDagFlow::buildExecutive(
    CallParameters::UniquePtr& input)
{
    bool isSharding = true;
    return m_executiveFactory->build(
        input->codeAddress, input->contextID, input->seq, true, isSharding);
}
