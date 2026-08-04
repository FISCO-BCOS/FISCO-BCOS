// bcos-rpc/bcos-rpc/web3jsonrpc/model/TxHandler.cpp
#include "TxHandler.h"
#include "Web3Transaction.h"

namespace bcos::rpc
{
namespace
{
struct LegacyTxHandler : TxHandler
{
    bcos::bytes encodeForSign(const Web3Transaction&) const override { return {}; }
    bcos::bytes encode(const Web3Transaction&) const override { return {}; }
    bcos::codec::rlp::Header header(const Web3Transaction&) const override;
    bcos::Error::UniquePtr decode(bcos::bytesRef&, Web3Transaction&, bool) const override
    {
        return nullptr;
    }
    void toJson(const Web3Transaction&, Json::Value&) const override {}
};
bcos::codec::rlp::Header LegacyTxHandler::header(const Web3Transaction&) const
{
    return {.isList = true, .payloadLength = 0};
}
LegacyTxHandler g_legacyHandler;
}  // namespace

TxHandler& handlerFor(TransactionType /*type*/)
{
    return g_legacyHandler;
}
}  // namespace bcos::rpc
