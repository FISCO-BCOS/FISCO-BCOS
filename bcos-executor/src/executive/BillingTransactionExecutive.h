#pragma once

#include "TransactionExecutive.h"

namespace bcos
{
namespace executor
{

class BillingTransactionExecutive : public TransactionExecutive
{
public:
    BillingTransactionExecutive(const BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector)
    : TransactionExecutive(blockContext, contractAddress, contextID, seq, gasInjector)
    {}

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override;
};

}  // namespace executor
}  // namespace bcos