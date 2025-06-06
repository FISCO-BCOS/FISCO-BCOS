#include "VMInstance.h"
#include <evmone/advanced_analysis.hpp>
#include <evmone/advanced_execution.hpp>

bcos::executor_v1::VMInstance::VMInstance(
    std::shared_ptr<evmone::baseline::CodeAnalysis const> instance) noexcept
  : m_instance(std::move(instance))
{}

bcos::executor_v1::EVMCResult bcos::executor_v1::VMInstance::execute(
    const struct evmc_host_interface* host, struct evmc_host_context* context, evmc_revision rev,
    const evmc_message* msg, const uint8_t* code, size_t codeSize)
{
    static auto const* evm = evmc_create_evmone();
    static thread_local std::unique_ptr<evmone::ExecutionState> localExecutionState;

    auto executionState = localExecutionState ? std::move(localExecutionState) :
                                                std::make_unique<evmone::ExecutionState>();
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

void bcos::executor_v1::VMInstance::enableDebugOutput() {}

std::strong_ordering operator<=>(const evmc_address& lhs, const evmc_address& rhs) noexcept
{
    return std::memcmp(lhs.bytes, rhs.bytes, sizeof(evmc_address)) <=> 0;
}
bool operator==(const evmc_address& lhs, const evmc_address& rhs) noexcept
{
    return std::is_eq(lhs <=> rhs);
}
bool std::equal_to<evmc_address>::operator()(
    const evmc_address& lhs, const evmc_address& rhs) const noexcept
{
    return lhs == rhs;
}
size_t std::hash<evmc_address>::operator()(const evmc_address& address) const noexcept
{
    std::span view(address.bytes);
    return boost::hash_range(view.begin(), view.end());
}
