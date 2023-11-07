#include "VMInstance.h"
#include <evmone/evmone.h>
#include <evmone/vm.hpp>

void bcos::transaction_executor::VMInstance::ReleaseEVMC::operator()(evmc_vm* ptr) const noexcept
{
    if (ptr != nullptr)
    {
        ptr->destroy(ptr);
    }
}

bcos::transaction_executor::EVMCResult bcos::transaction_executor::VMInstance::execute(
    const struct evmc_host_interface* host, struct evmc_host_context* context, evmc_revision rev,
    const evmc_message* msg, const uint8_t* code, size_t codeSize)
{
    return std::visit(overloaded{[&](EVMC_VM const& instance) {
                                     return EVMCResult(instance->execute(
                                         instance.get(), host, context, rev, msg, code, codeSize));
                                 },
                          [&](EVMC_ANALYSIS_RESULT const& instance) {
                              auto state = evmone::advanced::AdvancedExecutionState(*msg, rev,
                                  *host, context, std::basic_string_view<uint8_t>(code, codeSize));
                              auto* evm = evmc_create_evmone();
                              return EVMCResult(evmone::baseline::execute(
                                  *static_cast<evmone::VM*>(evm), msg->gas, state, *instance));
                          }},
        m_instance);
}

void bcos::transaction_executor::VMInstance::enableDebugOutput() {}
