#include "VMInstance.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
using bytes_view = std::basic_string_view<uint8_t>;

bcos::transaction_executor::VMInstance::VMInstance(
    std::shared_ptr<evmone::baseline::CodeAnalysis const> instance) noexcept
  : m_instance(std::move(instance))
{}

bcos::transaction_executor::EVMCResult bcos::transaction_executor::VMInstance::execute(
    const struct evmc_host_interface* host, struct evmc_host_context* context, evmc_revision rev,
    const evmc_message* msg, const uint8_t* code, size_t codeSize)
{
    static auto const* evm = evmc_create_evmone();
    static thread_local std::unique_ptr<evmone::ExecutionState> localExecutionState;

    auto executionState = std::move(localExecutionState);
    if (!executionState)
    {
        executionState = std::make_unique<evmone::ExecutionState>();
    }
    executionState->reset(
        *msg, rev, *host, context, std::basic_string_view<uint8_t>(code, codeSize), {});
    auto result = EVMCResult(evmone::baseline::execute(
        *static_cast<evmone::VM const*>(evm), msg->gas, *executionState, *m_instance));
    if (!localExecutionState)
    {
        localExecutionState = std::move(executionState);
    }
    return result;
}

void bcos::transaction_executor::VMInstance::enableDebugOutput() {}

bool std::equal_to<evmc_address>::operator()(
    const evmc_address& lhs, const evmc_address& rhs) const noexcept
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}
size_t boost::hash<evmc_address>::operator()(const evmc_address& address) const noexcept
{
    return boost::hash_range(address.bytes, address.bytes + sizeof(address.bytes));
}
size_t std::hash<evmc_address>::operator()(const evmc_address& address) const noexcept
{
    return boost::hash_range(address.bytes, address.bytes + sizeof(address.bytes));
}