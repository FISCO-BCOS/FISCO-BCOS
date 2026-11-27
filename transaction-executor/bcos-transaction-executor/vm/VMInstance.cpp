#include "VMInstance.h"
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <evmone/vm.hpp>

DERIVE_BCOS_EXCEPTION(MissingCreateAddressHook);

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
    const evmc_message* msg, const uint8_t* code, size_t codeSize,
    evmc_address (*createAddressFn)(struct evmc_host_context*, const evmc_address*, uint64_t),
    evmc_address (*create2AddressFn)(struct evmc_host_context*, const evmc_address*,
        const evmc_bytes32*, const uint8_t*, size_t))
{
    // The no-defaults signature makes an omitted hook a compile error; an explicit
    // nullptr must fail loudly too (an assert would vanish under NDEBUG and silently
    // install nothing — evmone would then derive addresses with the upstream formulas
    // and fork every chain this lane executes).
    if (createAddressFn == nullptr || create2AddressFn == nullptr)
    {
        BOOST_THROW_EXCEPTION(MissingCreateAddressHook{} << bcos::errinfo_comment(
                                  "VMInstance::execute requires both create-address hooks"));
    }
    (void)code;  // execute uses pre-analyzed m_analysis
    (void)codeSize;
    // Fresh VM per execute: evmone pools one ExecutionState per depth inside the VM
    // (0.24: `std::vector<ExecutionState> m_execution_states`, `get_execution_state(depth)`)
    // and its `reset()` clears memory/return-data/gas-refund but NOT the stack
    // (execution_state.hpp), so reusing a VM across executions can start the next one on
    // the previous stack's content. The 0.21-era observation (EVMC_BAD_JUMP_DESTINATION
    // after reset-and-reuse) is consistent with that; the workaround is carried forward
    // as-is (review F5).
    evmc::VM evm{evmc_create_evmone()};
    auto* evmoneVm = static_cast<evmone::VM*>(evm.get_raw_pointer());
    evmoneVm->hash_fn = hostcontext::evm_hash_fn;
    // Without these, evmone derives nested CREATE/CREATE2 addresses
    // with keccak(rlp(sender,nonce)) / keccak256 — a different address than this lane stores
    // (and, on SM-crypto chains, a different hasher).
    evmoneVm->create_address_fn = createAddressFn;
    evmoneVm->create2_address_fn = create2AddressFn;
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
