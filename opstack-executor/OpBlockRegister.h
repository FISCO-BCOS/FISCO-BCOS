#pragma once

#include <bcos-concepts/ByteBuffer.h>                      // bcos::concepts::bytebuffer::toView
#include <bcos-framework/engine/Errors.h>                  // OpExecutionInternalError
#include <bcos-framework/ledger/LedgerTypeDef.h>           // SYS_* 表常量(LedgerTypeDef.h:106-112)
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/storage/Entry.h>                  // bcos::storage::Entry
#include <bcos-framework/storage2/Storage.h>               // storage2::writeOne
#include <bcos-framework/transaction-executor/StateKey.h>  // bcos::executor_v1::StateKey
#include <bcos-tars-protocol/protocol/TransactionImpl.h>   // bcostars::Transaction + TransactionImpl(自带 tars Transaction.h)
#include <bcos-task/Task.h>
#include <opstack-executor/OpErrors.h>                     // OpExecuteBlockResult
#include <boost/lexical_cast.hpp>
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm::engine
{
/// raw EIP-2718 envelope -> tars Transaction。签名与 engine 的
/// `bcos::engine::detail::opEnvelopeToTars` 一致;由 composition root(Initializer)注入 lambda
/// 调用之——opstack-executor 不 link bcos-rpc / bcos-engine。
using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(
    bcos::bytes const&, bcos::crypto::HashType const&)>;

/// 写块表:等效 ethereum 的 ledger::prewriteBlockToBuffer。从 engine 原 registerOpBlock
/// 逐行搬移,数据来源改为显式参数。5 张表:
///   SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
///   SYS_HASH_2_RECEIPT / SYS_HASH_2_TX
/// 失败分类:receipt 数量不变量 / null receipt -> OpExecutionInternalError;写失败原样上抛
/// (engine 屏障分类 -32603)。blockHash 由调用方显式传入(engine step 2 已校验
/// == header.opHeaderHash(opHeaderConst()),不在此重算,避免常量漂移)。
template <class ViewType>
inline bcos::task::Task<void> opstackRegisterBlock(ViewType& view,
    bcos::protocol::BlockHeader const& header, bcos::crypto::HashType const& blockHash,
    std::vector<bcos::bytes> const& rawTxBytes, OpExecuteBlockResult const& result,
    bcos::protocol::BlockFactory& blockFactory, EnvelopeToTarsConverter const& envelopeToTars)
{
    const auto blockNumberStr = boost::lexical_cast<std::string>(header.number());

    bcos::storage::Entry numberToHashEntry;
    numberToHashEntry.set(blockHash.asBytes());
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr},
        std::move(numberToHashEntry));

    bcos::storage::Entry hashToNumberEntry;
    hashToNumberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(hashToNumberEntry));

    // OP header 以 tars BlockHeader 落标准 s_number_2_header 表(与普通 FISCO 块同表同格式)。
    // dataHash 为空 -> header.hash() 抛 EmptyBlockHeaderHash;本路径不调用它。encode() 是
    // `void encode(bytes&)` out-param(BlockHeader.h:50)——先建 buffer 再取。
    bcos::storage::Entry headerEntry;
    bcos::bytes headerBuffer;
    header.encode(headerBuffer);
    headerEntry.set(std::move(headerBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));

    auto& hashImpl = *blockFactory.cryptoSuite()->hashImpl();
    // processOpBlock 每 tx 恰产一 receipt;数量分叉是执行层坏不变量,响亮失败(内部错误,非对块的裁决)。
    if (rawTxBytes.size() != result.receipts.size())
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                              << bcos::errinfo_comment{
                                  "OP block execution returned a receipt count differing from "
                                  "the transaction count"});
    }
    for (std::size_t index = 0; index < rawTxBytes.size(); ++index)
    {
        auto const& receipt = result.receipts[index];
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{}
                                  << bcos::errinfo_comment{
                                      "OP block execution returned a null receipt"});
        }
        bcos::bytes encodedReceipt;
        receipt->encode(encodedReceipt);
        const auto txHash = hashImpl.hash(rawTxBytes[index]);

        bcos::storage::Entry receiptEntry;
        receiptEntry.set(std::move(encodedReceipt));
        co_await bcos::storage2::writeOne(view,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)},
            std::move(receiptEntry));

        // 转换失败(畸形/未枚举 envelope)-> 行跳过,块仍 VALID、该 tx 不可按 hash 查。
        if (auto tarsTx = envelopeToTars(rawTxBytes[index], txHash))
        {
            bcostars::protocol::TransactionImpl txImpl(
                [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
            bcos::bytes encodedTx;
            txImpl.encode(encodedTx);
            bcos::storage::Entry txEntry;
            txEntry.set(std::move(encodedTx));
            co_await bcos::storage2::writeOne(view,
                bcos::executor_v1::StateKey{
                    bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)},
                std::move(txEntry));
        }
    }
}
}  // namespace bcos::evm::engine
