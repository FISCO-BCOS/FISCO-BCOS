#include "HostContext.h"
#include "VMFactory.h"
#include <fmt/format.h>

evmc_bytes32 bcos::transaction_executor::hostcontext::evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(executor::GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

evmc_message bcos::transaction_executor::hostcontext::getMessage(evmc_message message,
    protocol::BlockNumber blockNumber, int64_t contextID, int64_t seq, crypto::Hash const& hashImpl)
{
    switch (message.kind)
    {
    case EVMC_CREATE:
    {
        if (concepts::bytebuffer::equalTo(
                message.code_address.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            auto address = fmt::format(FMT_COMPILE("{}_{}_{}"), blockNumber, contextID, seq);
            auto hash = hashImpl.hash(address);
            std::copy_n(
                hash.data(), sizeof(message.code_address.bytes), message.code_address.bytes);
        }
        message.recipient = message.code_address;
        break;
    }
    case EVMC_CREATE2:
    {
        auto& ref = message;
        std::array<bcos::byte,
            1 + sizeof(ref.sender.bytes) + sizeof(message.create2_salt) + crypto::HashType::SIZE>
            buffer;
        uint8_t* ptr = buffer.data();
        *ptr++ = 0xff;
        ptr = std::uninitialized_copy_n(ref.sender.bytes, sizeof(ref.sender.bytes), ptr);
        auto salt = toBigEndian(fromEvmC(message.create2_salt));
        ptr = std::uninitialized_copy(salt.begin(), salt.end(), ptr);
        auto inputHash = hashImpl.hash(bytesConstRef(ref.input_data, ref.input_size));
        ptr = std::uninitialized_copy(inputHash.begin(), inputHash.end(), ptr);
        auto addressHash = hashImpl.hash(bytesConstRef(buffer.data(), buffer.size()));

        std::copy_n(
            addressHash.begin() + 12, sizeof(ref.code_address.bytes), ref.code_address.bytes);
        ref.recipient = ref.code_address;
        break;
    }
    default:
    {
        break;
    }
    }
    return message;
}

bcos::transaction_executor::hostcontext::CacheExecutables&
bcos::transaction_executor::hostcontext::getCacheExecutables()
{
    struct CacheExecutables
    {
        bcos::transaction_executor::hostcontext::CacheExecutables m_cachedExecutables;

        CacheExecutables()
        {
            constexpr static auto maxContracts = 100;
            setMaxCapacity(m_cachedExecutables, sizeof(std::shared_ptr<Executable>) * maxContracts);
        }
    } static cachedExecutables;

    return cachedExecutables.m_cachedExecutables;
}

bcos::transaction_executor::hostcontext::Executable::Executable(
    storage::Entry code, evmc_revision revision)
  : m_code(std::make_optional(std::move(code))),
    m_vmInstance(VMFactory::create(VMKind::evmone,
        bytesConstRef(reinterpret_cast<const uint8_t*>(m_code->data()), m_code->size()), revision))
{}

bcos::transaction_executor::hostcontext::Executable::Executable(
    bytesConstRef code, evmc_revision revision)
  : m_vmInstance(VMFactory::create(VMKind::evmone, code, revision))
{}
