//
// Created by Jimmy Shi on 2022/6/11.
//

#include "DuplicateTransactionFactory.h"


using namespace bcos::rpc;
using namespace bcos::protocol;

void DuplicateTransactionFactory::multiBuild(
    bcos::protocol::TransactionFactory::Ptr transactionFactory, bytes const& seedTxData,
    bcos::crypto::KeyPairInterface::Ptr keyPair, int64_t num,
    std::function<void(bcos::protocol::Transaction::Ptr)> onTxBuild)
{
    auto seedTx = transactionFactory->createTransaction(seedTxData, false);
    int32_t version = seedTx->version();
    const std::string_view to = seedTx->to();
    const bytes input = seedTx->input().toBytes();
    auto seedBlockLimit = seedTx->blockLimit();
    const std::string chainId = std::string(seedTx->chainId());
    const std::string groupId = std::string(seedTx->groupId());
    std::atomic<int64_t> sentNum = 0;

    do
    {
        const u256 nonce = seedTx->nonce() + utcTimeUs();
        int64_t blockLimit =
            seedBlockLimit + sentNum.fetch_add(1) / 10000;  //  maybe this block limit is not so
                                                            //  fast nor so slow
        int64_t importTime = utcTime();
        Transaction::Ptr tx = transactionFactory->createTransaction(
            version, to, input, nonce, blockLimit, chainId, groupId, importTime, keyPair);

        onTxBuild(tx);
    } while (num > sentNum);
    BCOS_LOG(TRACE) << LOG_BADGE("FAKE") << "Generate " << num << " tx finished";
}
