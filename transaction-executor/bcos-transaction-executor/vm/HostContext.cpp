#include "HostContext.h"

evmc_bytes32 bcos::transaction_executor::evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(executor::GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}
bcos::executor::VMSchedule const& bcos::transaction_executor::vmSchedule()
{
    bcos::ledger::Features features;
    // TODO: according to featrue_evm_cancun to choose the schedule
    if (features.get(ledger::Features::Flag::feature_evm_cancun))
    {
        return executor::FiscoBcosScheduleCancun;
    }
    return executor::FiscoBcosScheduleV320;
}
