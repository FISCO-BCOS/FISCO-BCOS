// bcos-rpc/bcos-rpc/web3jsonrpc/model/TxHandler.h
#pragma once
#include <bcos-codec/rlp/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/json.h>
#include <cstdint>

namespace bcos::rpc
{
class Web3Transaction;
// TransactionType is defined in Web3Transaction.h (enum class : uint8_t); forward-declare the
// underlying type so handlerFor can be declared without pulling in the whole header.
enum class TransactionType : uint8_t;
// NOTE: Header 真实类型是 bcos::codec::rlp::Header(Common.h:45),需 include 而非前向声明。
// 注意该 include 必须放在 namespace 之外:若在 namespace bcos::rpc 内展开 Common.h,
// 其 `namespace bcos::codec::rlp` 会被解析为嵌套的 bcos::rpc::bcos::codec::rlp,导致命名空间污染。

struct TxHandler
{
    virtual ~TxHandler() = default;
    // 签名预映像(RLP 无 type byte、无签名)
    virtual bcos::bytes encodeForSign(const Web3Transaction&) const = 0;
    // 完整 RLP(含 type byte, typed 交易)
    virtual bcos::bytes encode(const Web3Transaction&) const = 0;
    // RLP header(长度计算)
    virtual bcos::codec::rlp::Header header(const Web3Transaction&) const = 0;
    // 解码(填充 Web3Transaction; withSig 控制是否解析签名)。
    // ⚠️ 返回 Error::UniquePtr(而非 void):解码错误必须传播,不能被静默吞掉(校验发现)。
    virtual bcos::Error::UniquePtr decode(
        bcos::bytesRef&, Web3Transaction&, bool withSig) const = 0;
    // RPC JSON 输出(仅类型相关字段)
    virtual void toJson(const Web3Transaction&, Json::Value&) const = 0;
};

// 按类型查表分派。未知类型返回 Legacy handler(防御)。
TxHandler& handlerFor(TransactionType type);
}  // namespace bcos::rpc
