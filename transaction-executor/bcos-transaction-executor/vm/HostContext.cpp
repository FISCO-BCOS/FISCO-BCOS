#include "HostContext.h"

evmc_bytes32 bcos::transaction_executor::evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(executor::GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}
