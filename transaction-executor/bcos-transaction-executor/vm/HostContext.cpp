#include "HostContext.h"
#include "VMFactory.h"
#include "bcos-crypto/ChecksumAddress.h"
#include <fmt/format.h>

evmc_bytes32 bcos::executor_v1::hostcontext::evm_hash_fn(
    evmc_host_context* /*context*/, const uint8_t* data, size_t size)
{
    return toEvmC(executor::GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

evmc_message bcos::executor_v1::hostcontext::getMessage(bool web3Tx,
    const evmc_message& inputMessage, protocol::BlockNumber blockNumber, int64_t contextID,
    int64_t seq, const u256& nonce, crypto::Hash const& hashImpl)
{
    evmc_message message = inputMessage;
    switch (message.kind)
    {
    case EVMC_CREATE:
    {
        // evmc ABI 19: nested creates (depth != 0) arrive with recipient already
        // computed — evmone derives it through this lane's create_address_fn hook
        // (HostContext::createAddressForOpcode: the FISCO
        // hash("<block>_<ctx>_<seq>") formula, or keccak(rlp(sender, nonce)) on
        // web3Tx chains) — and the CREATE instruction pushes THAT address onto the
        // EVM stack. Overwriting it with a different derivation
        // would split the account address from the stack address — the constructor's
        // stored address then points at nothing and every later call misses (observed as
        // contractDeploy revert). Depth-0 (transaction-level) creates keep the legacy
        // FISCO formula unconditionally: their executor-built messages may carry a
        // decorative recipient (e.g. precompile wrappers, eip2929 probes) that 0.21
        // always overwrote.
        if (message.depth != 0 && !concepts::bytebuffer::equalTo(
                                      message.recipient.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            message.code_address = message.recipient;
            break;
        }
        if (concepts::bytebuffer::equalTo(
                message.code_address.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            if (!web3Tx)
            {
                auto address = fmt::format("{}_{}_{}", blockNumber, contextID, seq);
                auto hash = hashImpl.hash(address);
                std::copy_n(
                    hash.data(), sizeof(message.code_address.bytes), message.code_address.bytes);
            }
            else
            {
                auto legacyAddr = newLegacyEVMAddress(bytesConstRef(message.sender.bytes), nonce);
                std::copy(legacyAddr.begin(), legacyAddr.end(), message.code_address.bytes);
            }
        }
        message.recipient = message.code_address;
        break;
    }
    case EVMC_CREATE2:
    {
        // evmc ABI 19: evmc_message no longer carries create2_salt. Nested CREATE2
        // messages arrive with recipient already computed — evmone derives it through
        // this lane's create2_address_fn hook (HostContext::create2AddressForOpcode:
        // the chain hasher over 0xff||sender||salt||chainHasher(initcode)) — only
        // transaction-level messages (recipient empty, salt historically always zero)
        // take this branch. The
        // depth != 0 guard mirrors the CREATE arm: a depth-0 message may carry a
        // decorative recipient (precompile wrappers, eip2929 probes) that this
        // branch must overwrite, not honour.
        if (message.depth != 0 && !concepts::bytebuffer::equalTo(
                                      message.recipient.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            message.code_address = message.recipient;
            break;
        }
        // Same helper the CREATE2 hook uses (review F1, round 2): the chain hasher over
        // 0xff || sender || salt || chainHasher(initcode), via bcos::newCreate2EVMAddress.
        constexpr evmc_bytes32 zero_salt{};
        auto addressHex = bcos::newCreate2EVMAddress(hashImpl,
            bcos::toHex(bytesConstRef(message.sender.bytes, sizeof(message.sender.bytes))),
            bytesConstRef(message.input_data, message.input_size), fromEvmC(zero_salt));
        auto addressBytes = bcos::fromHex(addressHex);
        std::copy_n(
            addressBytes.begin(), sizeof(message.code_address.bytes), message.code_address.bytes);
        message.recipient = message.code_address;
        break;
    }
    default:
        break;
    }
    return message;
}

bcos::executor_v1::hostcontext::CacheExecutables&
bcos::executor_v1::hostcontext::getCacheExecutables()
{
    struct CacheExecutables
    {
        bcos::executor_v1::hostcontext::CacheExecutables m_cachedExecutables;

        CacheExecutables()
        {
            constexpr static auto maxContracts = 100;
            m_cachedExecutables.setMaxCapacity(sizeof(std::shared_ptr<Executable>) * maxContracts);
        }
    } static cachedExecutables;

    return cachedExecutables.m_cachedExecutables;
}

bcos::executor_v1::hostcontext::Executable::Executable(storage::Entry code)
  : m_code(std::make_optional(std::move(code))),
    m_vmInstance(VMFactory::create(VMKind::evmone,
        bytesConstRef(reinterpret_cast<const uint8_t*>(m_code->data()), m_code->size())))
{}

bcos::executor_v1::hostcontext::Executable::Executable(bytesConstRef code)
  : m_vmInstance(VMFactory::create(VMKind::evmone, code))
{}