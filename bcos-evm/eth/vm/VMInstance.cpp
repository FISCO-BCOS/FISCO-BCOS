#include "VMInstance.h"
#include <evmone/evmone.h>
#include <evmone/vm.hpp>

bcos::evm::VMInstance::VMInstance(std::shared_ptr<EvmoneCodeAnalysis const> analysis) noexcept
  : m_analysis(std::move(analysis))
{
    assert(m_analysis != nullptr);
}

bcos::evm::EVMCResult bcos::evm::VMInstance::execute(const struct evmc_host_interface* host,
    struct evmc_host_context* context, evmc_revision rev, const evmc_message* msg,
    const uint8_t* code, size_t codeSize)
{
    (void)code;  // execute uses pre-analyzed m_analysis
    (void)codeSize;
    // Reuse thread_local VM: evmone 0.21 ExecutionState::reset() clears all critical fields
    // (gas_refund, memory, msg, host, rev, return_data, status). The 0.11-era dirty-stack bug
    // (EVMC_BAD_JUMP_DESTINATION) was fixed in 0.21. Internal ExecutionState pool grows to max
    // nesting depth then stops allocating — eliminating per-call malloc/free.
    thread_local evmc::VM t_vm{evmc_create_evmone()};
    return EVMCResult(evmone::baseline::execute(
        *static_cast<evmone::VM*>(t_vm.get_raw_pointer()), *host, context, rev, *msg, *m_analysis));
}

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
