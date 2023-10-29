#pragma once

#include "PrecompiledManager.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Task.h"
#include "bcos-transaction-executor/Common.h"
#include "bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "bcos-utilities/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <utility>

namespace bcos::transaction_executor
{

#define AUTH_CHECK_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("AUTH_CHECK")

inline static const auto authManagerAddress =
    boost::lexical_cast<unsigned long>(precompiled::AUTH_CONTRACT_MGR_ADDRESS);
inline static const auto evmAuthManagerAddress = toEvmC(precompiled::AUTH_CONTRACT_MGR_ADDRESS);

struct UnexpectedExternalCall : public bcos::Exception
{
};

inline task::Task<void> createAuthTables(auto& storage)
{
    co_return;
}

inline bool checkCreateAuth(PrecompiledManager const& precompiledManager, auto& storage,
    const protocol::BlockHeader& blockHeader, evmc_address const& newContractAddress,
    const evmc_address& origin, crypto::Hash::Ptr hashImpl)
{
    const auto* authPrecompiled = precompiledManager.getPrecompiled(authManagerAddress);
    if (!authPrecompiled)
    {
        return true;
    }
    auto codec = CodecWrapper(std::move(hashImpl), false);
    auto input = codec.encodeWithSig("hasDeployAuth(address)",
        bcos::Address(bcos::bytesConstRef(origin.bytes, sizeof(origin.bytes))));

    evmc_message evmcMessage = {.kind = EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = TRANSACTION_GAS,
        .recipient = evmAuthManagerAddress,
        .destination_ptr = nullptr,
        .destination_len = 0,
        .sender = newContractAddress,
        .sender_ptr = nullptr,
        .sender_len = 0,
        .input_data = input.data(),
        .input_size = input.size(),
        .value = {},
        .create2_salt = {},
        .code_address = evmAuthManagerAddress};

    auto result = authPrecompiled->call(
        storage, blockHeader, evmcMessage, origin, [](const evmc_message& message) -> EVMCResult {
            BOOST_THROW_EXCEPTION(UnexpectedExternalCall{});
        });
    bool code = true;
    codec.decode(bcos::bytesConstRef(result.output_data, result.output_size), code);

    return code;
}

// bytesRef func = ref(callParameters->data).getCroppedData(0, 4);
//         result = contractAuthPrecompiled->checkMethodAuth(
//             shared_from_this(), callParameters->receiveAddress, func, callParameters->origin);
//         if (versionCompareTo(m_blockContext.blockVersion(), BlockVersion::V3_2_VERSION) >= 0 &&
//             callParameters->origin != callParameters->senderAddress)
//         {
//             auto senderCheck = contractAuthPrecompiled->checkMethodAuth(shared_from_this(),
//                 callParameters->receiveAddress, func, callParameters->senderAddress);
//             result = result && senderCheck;
//         }
inline bool checkCallAuth(PrecompiledManager const& precompiledManager, auto& storage,
    const protocol::BlockHeader& blockHeader, evmc_message const& message,
    const evmc_address& origin, crypto::Hash::Ptr hashImpl)
{
    return true;
}

}  // namespace bcos::transaction_executor