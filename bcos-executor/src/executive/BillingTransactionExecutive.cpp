#include "BillingTransactionExecutive.h"
#include <bcos-codec/bcos-codec/wrapper/CodecWrapper.h>

using namespace bcos::executor;
using namespace bcos::codec;
using namespace bcos::precompiled;

CallParameters::UniquePtr BillingTransactionExecutive::start(CallParameters::UniquePtr input)
{
    int64_t originGas = input->gas;
    uint64_t currentSeq = input->seq;
    std::string balanceSpenderAddr = input->origin;
    bool staticCall = input->staticCall;
    u256 gasPrice = input->gasPrice;
    auto message = TransactionExecutive::execute(std::move(input));

    if ((currentSeq == 0) && !staticCall)
    {
        CallParameters::UniquePtr callParam4AccountPre =
            std::make_unique<CallParameters>(CallParameters::MESSAGE);
        auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
        callParam4AccountPre->origin = BALANCE_PRECOMPILED_ADDRESS;
        callParam4AccountPre->senderAddress =
            contractAddress();  // because seq = 0, not delegatecall
        callParam4AccountPre->receiveAddress = ACCOUNT_ADDRESS;

        int64_t gasUsed = originGas - message->gas;  // TODO: consider revert
        u256 balanceUsed = gasUsed * gasPrice;
        bytes subBalanceIn = codec.encodeWithSig("subAccountBalance(uint256)", balanceUsed);
        std::vector<std::string> codeParameters{getContractTableName(balanceSpenderAddr)};
        auto newParams = codec.encode(codeParameters, subBalanceIn);
        callParam4AccountPre->data = std::move(newParams);

        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Billing") << "Try to subAccount balance: "
                             << LOG_KV("subBalance", gasUsed * gasPrice)
                             << LOG_KV("gasUsed", gasUsed) << LOG_KV("gasPrice", gasPrice)
                             << LOG_KV("balanceSpenderAddr", balanceSpenderAddr);
        auto subBalanceRet = callPrecompiled(std::move(callParam4AccountPre));
        /* leave this code for future use
        auto subBalanceRet = externalRequest(shared_from_this(), ref(callParam4AccountPre->data),
                                             balanceSpenderAddr,
        callParam4AccountPre->senderAddress, callParam4AccountPre->receiveAddress, false, false,
                                                message->gas, true);
                                             */
        if (subBalanceRet->status != (int32_t)protocol::TransactionStatus::None)
        {
            message->type = subBalanceRet->type;
            message->status = subBalanceRet->status;
            message->evmStatus = subBalanceRet->evmStatus;
            message->message = subBalanceRet->message;
            EXECUTIVE_LOG(TRACE) << LOG_BADGE("Billing") << "SubAccount balance failed: "
                                 << LOG_KV("subBalance", gasUsed * gasPrice)
                                 << LOG_KV("gasUsed", gasUsed) << LOG_KV("gasPrice", gasPrice)
                                 << LOG_KV("status", message->status) << LOG_KV("message", message);
            return message;
        }
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Billing") << "SubAccount balance success: "
                             << LOG_KV("subBalance", gasUsed * gasPrice)
                             << LOG_KV("gasUsed", gasUsed) << LOG_KV("gasPrice", gasPrice)
                             << LOG_KV("status", message->status) << LOG_KV("message", message);
    }

    EXECUTIVE_LOG(TRACE) << "Execute billing executive finish\t" << message->toFullString();

    return message;
}