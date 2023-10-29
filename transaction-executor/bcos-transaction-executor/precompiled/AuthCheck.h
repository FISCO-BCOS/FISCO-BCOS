#pragma once

#include "PrecompiledManager.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Task.h"
#include "bcos-transaction-executor/Common.h"
#include "bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/throw_exception.hpp>

namespace bcos::transaction_executor
{

struct UnexpectedExternalCall : public bcos::Exception
{
};

inline task::Task<void> buildAuthTables(auto& storage)
{
    co_return;
}

inline task::Task<bool> checkCreateAuth(PrecompiledManager const& precompiledManager, auto& storage,
    const protocol::BlockHeader& blockHeader, std::string_view newContractAddress,
    const evmc_address& origin, crypto::Hash::Ptr hashImpl)
{
    static const auto authManagerAddress =
        boost::lexical_cast<unsigned long>(precompiled::AUTH_CONTRACT_MGR_ADDRESS);
    static const auto evmAuthManagerAddress = toEvmC(precompiled::AUTH_CONTRACT_MGR_ADDRESS);

    const auto* authPrecompiled = precompiledManager.getPrecompiled(authManagerAddress);
    if (!authPrecompiled)
    {
        co_return true;
    }
    auto codec = CodecWrapper(hashImpl, false);
    auto input = codec.encodeWithSig("hasDeployAuth(address)",
        bcos::Address(bcos::bytesConstRef(origin.bytes, sizeof(origin.bytes))));

    evmc_message evmcMessage = {.kind = EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = TRANSACTION_GAS,
        .recipient = evmAuthManagerAddress,
        .destination_ptr = nullptr,
        .destination_len = 0,
        .sender = toEvmC(newContractAddress),
        .sender_ptr = nullptr,
        .sender_len = 0,
        .input_data = input.data(),
        .input_size = input.size(),
        .value = {},
        .create2_salt = {},
        .code_address = {}};

    auto result = authPrecompiled->call(
        storage, blockHeader, evmcMessage, origin, [](const evmc_message& message) -> EVMCResult {
            BOOST_THROW_EXCEPTION(UnexpectedExternalCall{});
        });
    bool code = true;
    codec.decode(bcos::bytesConstRef(result.output_data, result.output_size), code);

    co_return code;
}

inline task::Task<bool> checkCallAuth(auto& storage)
{
    co_return true;
}

}  // namespace bcos::transaction_executor