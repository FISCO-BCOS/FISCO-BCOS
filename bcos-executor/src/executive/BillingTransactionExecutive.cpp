#include "BillingTransactionExecutive.h"
#include <bcos-codec/bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-framework/bcos-framework/executor/PrecompiledTypeDef.h>

#ifdef WITH_WASM
#include "../vm/gas_meter/GasInjector.h"
#endif

using namespace bcos::executor;
using namespace bcos::codec;
using namespace bcos::precompiled;

CallParameters::UniquePtr BillingTransactionExecutive::start(CallParameters::UniquePtr input)
{
    uint64_t currentSeq = input->seq;
    std::string currentSenderAddr = input->senderAddress;
    auto message = TransactionExecutive::execute(std::move(input));

    if (currentSeq == 0)
    {
        CallParameters::UniquePtr callParam4AccountPre =
            std::make_unique<CallParameters>(CallParameters::MESSAGE);
        auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
        callParam4AccountPre->senderAddress = currentSenderAddr;
        callParam4AccountPre->receiveAddress = ACCOUNT_ADDRESS;

        //Todo: need to get from block.
        u256 gasPrice = 1;

        bytes subBalanceIn = codec.encodeWithSig("subAccountBalance(uint256)", message->gas * gasPrice);
        std::vector<std::string> codeParameters{currentSenderAddr};
        auto newParams = codec.encode(codeParameters, subBalanceIn);
        callParam4AccountPre->data = std::move(newParams);
        
        auto subBalanceRet = callPrecompiled(std::move(callParam4AccountPre));
        if(subBalanceRet->type == CallParameters::REVERT)
        {
            message->type = subBalanceRet->type;
            message->status = subBalanceRet->status;
            message->message = subBalanceRet->message;
            return message;
        }
    }

    EXECUTIVE_LOG(TRACE) << "Execute billing executive finish\t" << message->toFullString();

    return message;
}