#include "TransactionResponse.h"
#include "bcos-rpc/web3jsonrpc/model/DepositTransaction.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-rpc/jsonrpc/Common.h>  // WEB3_LOG

void bcos::rpc::combineTxResponse(Json::Value& result, const bcos::protocol::Transaction& tx,
    const protocol::TransactionReceipt& receipt, const crypto::HashType& blockHash)
{
    combineTxResponse(result, tx, receipt.transactionIndex(), receipt.blockNumber(), blockHash);
    // TODO: Check
    if (!receipt.effectiveGasPrice().empty())
    {
        auto gasPrice = receipt.effectiveGasPrice();
        result["gasPrice"] = std::string{gasPrice.empty() ? "0x0" : gasPrice};
    }
}

void bcos::rpc::combineTxResponse(Json::Value& result, const bcos::protocol::Transaction& tx,
    size_t transactionIndex, protocol::BlockNumber blockNumber, const crypto::HashType& blockHash)
{
    if (!result.isObject())
    {
        return;
    }
    result["blockHash"] = blockHash.hexPrefixed();
    result["blockNumber"] = toQuantity(blockNumber);
    result["transactionIndex"] = toQuantity(transactionIndex);
    auto from = toHex(tx.sender());
    toChecksumAddress(from, bcos::crypto::keccak256Hash(bcos::bytesConstRef(from)).hex());
    result["from"] = "0x" + std::move(from);
    if (tx.to().empty())
    {
        result["to"] = Json::nullValue;
    }
    else
    {
        auto toView = tx.to();
        auto to = std::string(toView.starts_with("0x") ? toView.substr(2) : toView);
        toChecksumAddress(to, bcos::crypto::keccak256Hash(bcos::bytesConstRef(to)).hex());
        result["to"] = "0x" + std::move(to);
    }
    result["gas"] = toQuantity(tx.gasLimit());
    auto gasPrice = tx.gasPrice();
    result["gasPrice"] = toQuantity(gasPrice.value_or(0));
    result["hash"] = tx.hash().hexPrefixed();
    result["input"] = toHexStringWithPrefix(tx.input());

    // OP Stack deposit (0x7e): extraTransactionBytes carries the full raw envelope, and
    // the geth-shaped deposit fields (sourceHash/mint/isSystemTx, zero nonce/gasPrice and
    // zero v/r/s — deposits are unsigned) come from decoding it. Return early: the
    // signature-derived fields below have no deposit semantics.
    if (auto extraBytes = tx.extraTransactionBytes();
        !extraBytes.empty() && extraBytes[0] == c_depositTxType)
    {
        DepositTransaction deposit;
        auto rawCursor = bcos::bytesRef(const_cast<byte*>(extraBytes.data()), extraBytes.size());
        if (auto error = decodeDepositTransaction(rawCursor, deposit); error == nullptr)
        {
            combineDepositTxResponse(result, deposit);
        }
        else
        {
            // Undecodable deposit bytes: keep the generic fields already filled and mark
            // the type; never fall through to the Web3 payload decoder (wrong format) or
            // to the signature fields (deposits carry none).
            result["type"] = toQuantity(static_cast<uint64_t>(c_depositTxType));
            result["nonce"] = "0x0";
            result["v"] = "0x0";
            result["r"] = "0x0";
            result["s"] = "0x0";
        }
        return;
    }

    if (tx.type() == bcos::protocol::TransactionType::BCOSTransaction) [[unlikely]]
    {
        result["type"] = toQuantity(0);
        // web3 tools do not compatible with too long hex
        result["nonce"] = "0x" + std::string(tx.nonce());
        result["value"] = toQuantity(tx.value());
        result["maxPriorityFeePerGas"] = toQuantity(tx.maxPriorityFeePerGas().value_or(0));
        result["maxFeePerGas"] = toQuantity(tx.maxFeePerGas().value_or(0));
        result["chainId"] = "0x0";
    }
    else [[likely]]
    {
        Web3Transaction web3Tx;
        auto extraBytesRef = bcos::bytesRef(const_cast<byte*>(tx.extraTransactionBytes().data()),
            tx.extraTransactionBytes().size());
        if (auto error = codec::rlp::decodeFromPayload(extraBytesRef, web3Tx); error != nullptr)
        {
            // Undecodable web3 payload (corrupt extraTransactionBytes, or a tars mirror that
            // diverged from the envelope): never serialize half-decoded state. Emit zeroed
            // web3 fields instead (same posture as the deposit fallback above) and log once.
            WEB3_LOG(WARNING) << LOG_DESC("TransactionResponse: undecodable web3 payload")
                              << LOG_KV("hash", tx.hash().hexPrefixed())
                              << LOG_KV("reason", error->errorMessage());
            result["nonce"] = "0x0";
            result["type"] = toQuantity(0);
            result["value"] = "0x0";
            result["chainId"] = toQuantity(0);
            result["v"] = "0x0";
            result["r"] = "0x0";
            result["s"] = "0x0";
            return;
        }
        result["nonce"] = toQuantity(web3Tx.nonce);
        result["type"] = toQuantity(static_cast<uint8_t>(web3Tx.type));
        result["value"] = toQuantity(web3Tx.value);
        // Use explicit range checks rather than `>=` so that Deposit (0x7e), which is numerically
        // larger than all EIP types, is excluded from EIP-specific field output. EIP-7702 is a
        // fee-market type with an access list, so the upper bound is EIP7702 (matching
        // takeToTarsTransaction on the write side).
        if (web3Tx.type >= TransactionType::EIP2930 && web3Tx.type <= TransactionType::EIP7702)
        {
            result["accessList"] = Json::arrayValue;
            for (auto& accessList : web3Tx.accessList)
            {
                Json::Value access = Json::objectValue;
                access["address"] = accessList.account.hexPrefixed();
                access["storageKeys"] = Json::arrayValue;
                for (const auto& j : accessList.storageKeys)
                {
                    Json::Value storageKey = j.hexPrefixed();
                    access["storageKeys"].append(std::move(storageKey));
                }
                result["accessList"].append(std::move(access));
            }
        }
        if (web3Tx.type >= TransactionType::EIP1559 && web3Tx.type <= TransactionType::EIP7702)
        {
            result["maxPriorityFeePerGas"] = toQuantity(web3Tx.maxPriorityFeePerGas);
            result["maxFeePerGas"] = toQuantity(web3Tx.maxFeePerGas);
        }
        result["chainId"] = toQuantity(web3Tx.chainId.value_or(0));
        if (web3Tx.type == TransactionType::EIP4844)
        {
            result["maxFeePerBlobGas"] = toQuantity(web3Tx.maxFeePerBlobGas);
            result["blobVersionedHashes"] = Json::arrayValue;
            for (const auto& blobVersionedHashe : web3Tx.blobVersionedHashes)
            {
                Json::Value hash = blobVersionedHashe.hexPrefixed();
                result["blobVersionedHashes"].append(std::move(hash));
            }
        }
        if (web3Tx.type == TransactionType::EIP7702)
        {
            // geth parity: each entry serializes as
            // {chainId, address, nonce, yParity, r, s} with hex-quantity scalars.
            result["authorizationList"] = Json::arrayValue;
            for (const auto& auth : web3Tx.authorizationList)
            {
                Json::Value entry = Json::objectValue;
                entry["chainId"] = toQuantity(auth.chainId);
                entry["address"] = auth.address.hexPrefixed();
                entry["nonce"] = toQuantity(auth.nonce);
                entry["yParity"] = toQuantity(auth.yParity);
                entry["r"] = toQuantity(auth.r);
                entry["s"] = toQuantity(auth.s);
                result["authorizationList"].append(std::move(entry));
            }
        }
    }
    result["r"] = toQuantity(tx.signatureData().getCroppedData(0, 32));
    result["s"] = toQuantity(tx.signatureData().getCroppedData(32, 32));
    result["v"] = toQuantity(tx.signatureData().getCroppedData(64, 1));
}
