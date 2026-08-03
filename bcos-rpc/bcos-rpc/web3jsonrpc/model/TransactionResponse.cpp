#include "TransactionResponse.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"

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
        codec::rlp::decodeFromPayload(extraBytesRef, web3Tx);
        result["nonce"] = toQuantity(web3Tx.nonce);
        result["type"] = toQuantity(static_cast<uint8_t>(web3Tx.type));
        result["value"] = toQuantity(web3Tx.value);
        if (web3Tx.type >= TransactionType::EIP2930)
        {
            result["accessList"] = Json::arrayValue;
            result["accessList"].resize(web3Tx.accessList.size());
            for (auto& accessList : web3Tx.accessList)
            {
                Json::Value access = Json::objectValue;
                access["address"] = accessList.account.hexPrefixed();
                access["storageKeys"] = Json::arrayValue;
                access["storageKeys"].resize(accessList.storageKeys.size());
                for (const auto& j : accessList.storageKeys)
                {
                    Json::Value storageKey = j.hexPrefixed();
                    access["storageKeys"].append(std::move(storageKey));
                }
                result["accessList"].append(std::move(access));
            }
        }
        if (web3Tx.type >= TransactionType::EIP1559)
        {
            result["maxPriorityFeePerGas"] = toQuantity(web3Tx.maxPriorityFeePerGas);
            result["maxFeePerGas"] = toQuantity(web3Tx.maxFeePerGas);
        }
        result["chainId"] = toQuantity(web3Tx.chainId.value_or(0));
        if (web3Tx.type >= TransactionType::EIP4844)
        {
            result["maxFeePerBlobGas"] = web3Tx.maxFeePerBlobGas.str();
            result["blobVersionedHashes"] = Json::arrayValue;
            result["blobVersionedHashes"].resize(web3Tx.blobVersionedHashes.size());
            for (const auto& blobVersionedHashe : web3Tx.blobVersionedHashes)
            {
                Json::Value hash = blobVersionedHashe.hexPrefixed();
                result["blobVersionedHashes"].append(std::move(hash));
            }
        }
    }
    result["r"] = toQuantity(tx.signatureData().getCroppedData(0, 32));
    result["s"] = toQuantity(tx.signatureData().getCroppedData(32, 32));
    result["v"] = toQuantity(tx.signatureData().getCroppedData(64, 1));
}

/// Transaction response for an OP-block transaction, built from a decoded Web3Transaction (OP
/// blocks carry raw EIP-2718 envelopes, not tars Transaction objects). Mirrors the Web3 branch of
/// combineTxResponse above (the same field set), except it cannot output the yParity-agnostic
/// `v`/`r`/`s` crop from a tars Transaction — the envelope decoder fills signatureV/R/S already.
void bcos::rpc::combineTxResponseFromWeb3(Json::Value& result, const Web3Transaction& web3Tx,
    size_t transactionIndex, protocol::BlockNumber blockNumber, const crypto::HashType& blockHash)
{
    if (!result.isObject())
    {
        return;
    }
    result["blockHash"] = blockHash.hexPrefixed();
    result["blockNumber"] = toQuantity(blockNumber);
    result["transactionIndex"] = toQuantity(transactionIndex);
    auto from = toHex(web3Tx.sender());
    toChecksumAddress(from, bcos::crypto::keccak256Hash(bcos::bytesConstRef(from)).hex());
    result["from"] = "0x" + std::move(from);
    if (!web3Tx.to.has_value())
    {
        result["to"] = Json::nullValue;
    }
    else
    {
        auto to = web3Tx.to.value().hex();
        toChecksumAddress(to, bcos::crypto::keccak256Hash(bcos::bytesConstRef(to)).hex());
        result["to"] = "0x" + std::move(to);
    }
    result["gas"] = toQuantity(web3Tx.gasLimit);
    result["gasPrice"] = toQuantity(web3Tx.maxFeePerGas);
    result["hash"] = web3Tx.txHash().hexPrefixed();
    result["input"] = toHexStringWithPrefix(web3Tx.data);
    result["nonce"] = toQuantity(web3Tx.nonce);
    result["type"] = toQuantity(static_cast<uint8_t>(web3Tx.type));
    result["value"] = toQuantity(web3Tx.value);
    if (web3Tx.type >= TransactionType::EIP2930)
    {
        result["accessList"] = Json::arrayValue;
        result["accessList"].resize(web3Tx.accessList.size());
        for (auto& accessList : web3Tx.accessList)
        {
            Json::Value access = Json::objectValue;
            access["address"] = accessList.account.hexPrefixed();
            access["storageKeys"] = Json::arrayValue;
            access["storageKeys"].resize(accessList.storageKeys.size());
            for (const auto& j : accessList.storageKeys)
            {
                access["storageKeys"].append(j.hexPrefixed());
            }
            result["accessList"].append(std::move(access));
        }
    }
    if (web3Tx.type >= TransactionType::EIP1559)
    {
        result["maxPriorityFeePerGas"] = toQuantity(web3Tx.maxPriorityFeePerGas);
        result["maxFeePerGas"] = toQuantity(web3Tx.maxFeePerGas);
    }
    result["chainId"] = toQuantity(web3Tx.chainId.value_or(0));
    if (web3Tx.type >= TransactionType::EIP4844)
    {
        result["maxFeePerBlobGas"] = web3Tx.maxFeePerBlobGas.str();
        result["blobVersionedHashes"] = Json::arrayValue;
        result["blobVersionedHashes"].resize(web3Tx.blobVersionedHashes.size());
        for (const auto& blobVersionedHashe : web3Tx.blobVersionedHashes)
        {
            result["blobVersionedHashes"].append(blobVersionedHashe.hexPrefixed());
        }
    }
    result["r"] = toQuantity(web3Tx.signatureR);
    result["s"] = toQuantity(web3Tx.signatureS);
    result["v"] = toQuantity(web3Tx.signatureV);
}
