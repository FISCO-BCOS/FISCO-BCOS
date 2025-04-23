#include "HostContext.h"
#include "VMFactory.h"
#include "bcos-crypto/ChecksumAddress.h"
#include <fmt/format.h>

evmc_bytes32 bcos::executor_v1::hostcontext::evm_hash_fn(const uint8_t* data, size_t size)
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
        if (concepts::bytebuffer::equalTo(
                message.code_address.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            if (!web3Tx)
            {
                auto address = fmt::format(FMT_COMPILE("{}_{}_{}"), blockNumber, contextID, seq);
                auto hash = hashImpl.hash(address);
                std::copy_n(
                    hash.data(), sizeof(message.code_address.bytes), message.code_address.bytes);
            }
            else
            {
                message.code_address = newLegacyEVMAddress(
                    bytesConstRef(message.sender.bytes, sizeof(message.sender.bytes)), nonce);
            }
        }
        message.recipient = message.code_address;
        break;
    }
    case EVMC_CREATE2:
    {
        std::array<bcos::byte, 1 + sizeof(message.sender.bytes) + sizeof(message.create2_salt) +
                                   crypto::HashType::SIZE>
            buffer;
        uint8_t* ptr = buffer.data();
        *ptr++ = 0xff;
        ptr = std::uninitialized_copy_n(message.sender.bytes, sizeof(message.sender.bytes), ptr);
        auto salt = toBigEndian(fromEvmC(message.create2_salt));
        ptr = std::uninitialized_copy(salt.begin(), salt.end(), ptr);
        auto inputHash = hashImpl.hash(bytesConstRef(message.input_data, message.input_size));
        ptr = std::uninitialized_copy(inputHash.begin(), inputHash.end(), ptr);
        auto addressHash = hashImpl.hash(bytesConstRef(buffer.data(), buffer.size()));

        std::copy_n(addressHash.begin() + 12, sizeof(message.code_address.bytes),
            message.code_address.bytes);
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

bcos::executor_v1::hostcontext::Executable::Executable(storage::Entry code, evmc_revision revision)
  : m_code(std::make_optional(std::move(code))),
    m_vmInstance(VMFactory::create(VMKind::evmone,
        bytesConstRef(reinterpret_cast<const uint8_t*>(m_code->data()), m_code->size()), revision))
{}

bcos::executor_v1::hostcontext::Executable::Executable(bytesConstRef code, evmc_revision revision)
  : m_vmInstance(VMFactory::create(VMKind::evmone, code, revision))
{}