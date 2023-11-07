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
    auto message = TransactionExecutive::execute(std::move(input));

    if (currentSeq == 0)
    {
        CallParameters::UniquePtr callParam4AccountPre =
            std::make_unique<CallParameters>(CallParameters::MESSAGE);
        auto codec = CodecWrapper(m_blockContext.hashHandler(), m_blockContext.isWasm());
        bytes getBalanceIn = codec.encodeWithSig("getAccountBalance()");
        callParam4AccountPre->data = getBalanceIn;
        callParam4AccountPre->senderAddress = input->senderAddress;
        callParam4AccountPre->receiveAddress = ACCOUNT_ADDRESS;
        auto balanceRet = callPrecompiled(std::move(callParam4AccountPre));
        if(balanceRet->type == CallParameters::REVERT)
        {
            message->type = balanceRet->type;
            message->status = balanceRet->status;
            message->message = balanceRet->message;
            return message;
        }
        u256 balance;
        codec.decode(ref(balanceRet->data), balance);

        CallParameters::UniquePtr callParam4SystemConfig =
            std::make_unique<CallParameters>(CallParameters::MESSAGE);
        bytes gasPriceInput = codec.encodeWithSig("getValueByKey(string)", std::string("gasPrice"));
        callParam4SystemConfig->data = gasPriceInput;
        callParam4SystemConfig->senderAddress = input->senderAddress;
        callParam4SystemConfig->receiveAddress = SYS_CONFIG_ADDRESS;
        auto gasPriceRet = callPrecompiled(std::move(callParam4SystemConfig));
        if(gasPriceRet->type == CallParameters::REVERT)
        {
            message->type = gasPriceRet->type;
            message->status = gasPriceRet->status;
            message->message = gasPriceRet->message;
            return message;
        }

        u256 gasPrice;
        codec.decode(ref(gasPriceRet->data), gasPrice);

        if(balance < message->gas * gasPrice)
        {
            revert();
        }

        bytes subBalanceIn = codec.encodeWithSig("subAccountBalance(uint256)", message->gas * gasPrice);
        callParam4AccountPre->data = subBalanceIn;
        auto subBalanceRet = callPrecompiled(std::move(callParam4AccountPre));
        if(subBalanceRet->type == CallParameters::REVERT)
        {
            message->type = subBalanceRet->type;
            message->status = subBalanceRet->status;
            message->message = subBalanceRet->message;
            return message;
        }
    }

    EXECUTIVE_LOG(TRACE) << "Execute finish\t" << message->toFullString();

    return message;
}