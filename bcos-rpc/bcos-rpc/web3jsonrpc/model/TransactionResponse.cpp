#include "TransactionResponse.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"

void bcos::rpc::combineTxResponse(Json::Value& result, const bcos::protocol::Transaction& tx,
    const protocol::TransactionReceipt* receipt, const bcos::protocol::Block* block)
{
    if (!result.isObject())
    {
        return;
    }
    size_t transactionIndex = 0;
    crypto::HashType blockHash;
    uint64_t blockNumber = 0;
    if (block != nullptr)
    {
        blockHash = block->blockHeaderConst()->hash();
        blockNumber = block->blockHeaderConst()->number();
        auto transactionHashes = block->transactionHashes();
        auto transactions = block->transactions();
        if (auto it = ::ranges::find(transactionHashes, tx.hash()); it != transactionHashes.end())
        {
            transactionIndex = ::ranges::distance(transactionHashes.begin(), it);
        }
        else if (auto it = ::ranges::find_if(
                     transactions, [&](const auto& tx2) { return tx2->hash() == tx.hash(); });
            it != transactions.end())
        {
            transactionIndex = ::ranges::distance(transactions.begin(), it);
        }
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
    if (receipt != nullptr && !receipt->effectiveGasPrice().empty())
    {
        gasPrice = receipt->effectiveGasPrice();
    }
    // FIXME)): return will case coredump in executor
    result["gasPrice"] = std::string(gasPrice.empty() ? "0x0" : gasPrice);
    result["hash"] = tx.hash().hexPrefixed();
    result["input"] = toHexStringWithPrefix(tx.input());

    if (tx.type() == bcos::protocol::TransactionType::BCOSTransaction) [[unlikely]]
    {
        result["type"] = toQuantity(0);
        // web3 tools do not compatible with too long hex
        result["nonce"] = "0x" + std::string(tx.nonce());
        result["value"] = std::string(tx.value().empty() ? "0x0" : tx.value());
        result["maxPriorityFeePerGas"] =
            std::string(tx.maxPriorityFeePerGas().empty() ? "0x0" : tx.maxPriorityFeePerGas());
        result["maxFeePerGas"] = std::string(tx.maxFeePerGas().empty() ? "0x0" : tx.maxFeePerGas());
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
