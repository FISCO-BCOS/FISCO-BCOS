#include "BillingTransactionExecutive.h"
#include <bcos-codec/bcos-codec/wrapper/CodecWrapper.h>

using namespace bcos::executor;
using namespace bcos::codec;
using namespace bcos::precompiled;

CallParameters::UniquePtr BillingTransactionExecutive::start(CallParameters::UniquePtr input)
{
    int64_t originGas = input->gas;
    uint64_t currentSeq = input->seq;
    std::string currentSenderAddr = input->senderAddress;
    bool staticCall = input->staticCall;
    auto message = TransactionExecutive::execute(std::move(input));

    if ((currentSeq == 0) && !staticCall)
    {
        CallParameters::UniquePtr callParam4AccountPre =
            std::make_unique<CallParameters>(CallParameters::MESSAGE);
        auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
        callParam4AccountPre->senderAddress = TXEXEC_GAS_CONSUMER_ADDRESS;
        callParam4AccountPre->receiveAddress = ACCOUNT_ADDRESS;

        // Todo: need to get from block.
        u256 gasPrice = 1;

        int64_t gasUsed = originGas - message->gas;
        bytes subBalanceIn = codec.encodeWithSig("subAccountBalance(uint256)", gasUsed * gasPrice);
        std::vector<std::string> codeParameters{getContractTableName(currentSenderAddr)};
        auto newParams = codec.encode(codeParameters, subBalanceIn);
        callParam4AccountPre->data = std::move(newParams);

        auto subBalanceRet = callPrecompiled(std::move(callParam4AccountPre));
        if (subBalanceRet->type == CallParameters::REVERT)
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