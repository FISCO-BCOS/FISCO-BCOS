#include "VMInstance.h"
#include <evmone/evmone.h>
#include <evmone/vm.hpp>

namespace bcos::executor_v1::hostcontext
{
evmc_bytes32 evm_hash_fn(struct evmc_host_context* context, const uint8_t* data, size_t size);
}  // namespace bcos::executor_v1::hostcontext

bcos::executor_v1::VMInstance::VMInstance(
    std::shared_ptr<EvmoneCodeAnalysis const> analysis) noexcept
  : m_analysis(std::move(analysis))
{
    assert(m_analysis != nullptr);
}

bcos::executor_v1::EVMCResult bcos::executor_v1::VMInstance::execute(
    const struct evmc_host_interface* host, struct evmc_host_context* context, evmc_revision rev,
    const evmc_message* msg, const uint8_t* code, size_t codeSize)
{
    (void)code;  // execute uses pre-analyzed m_analysis
    (void)codeSize;
    // Fresh VM per execute: evmone 0.21 pools ExecutionState inside VM; thread_local reuse leaves
    // dirty stack after reset() (EVMC_BAD_JUMP_DESTINATION).
    evmc::VM evm{evmc_create_evmone()};
    auto* evmoneVm = static_cast<evmone::VM*>(evm.get_raw_pointer());
    evmoneVm->hash_fn = hostcontext::evm_hash_fn;
    return EVMCResult(evmone::baseline::execute(*evmoneVm, *host, context, rev, *msg, *m_analysis));
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
