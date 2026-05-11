#include "VMInstance.h"
#include <evmc/evmc.hpp>

bcos::executor_v1::VMInstance::VMInstance(
    std::shared_ptr<evmone::baseline::CodeAnalysis const> instance) noexcept
  : m_instance(std::move(instance))
{}

bcos::executor_v1::EVMCResult bcos::executor_v1::VMInstance::execute(
    const struct evmc_host_interface* host, struct evmc_host_context* context, evmc_revision rev,
    const evmc_message* msg, const uint8_t* code, size_t codeSize)
{
    evmc::VM evm{evmc_create_evmone()};
    // TODO: code/codeSize are currently unused because evmone 0.21 baseline::execute() takes a
    // pre-analyzed CodeAnalysis (m_instance) rather than raw bytecode. These parameters exist for
    // interface compatibility (e.g. WASM/external VM paths). When aligning with full EVM semantics
    // (e.g. supporting EVMC_EOFCREATE inline code, or switching to an interpreter that takes raw
    // code), wire code/codeSize through to the underlying execute call.
    (void)code;
    (void)codeSize;
    return EVMCResult(evmone::baseline::execute(
        *static_cast<evmone::VM*>(evm.get_raw_pointer()), *host, context, rev, *msg, *m_instance));
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
