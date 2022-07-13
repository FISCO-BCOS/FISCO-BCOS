#pragma once

#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>

namespace bcos::rpc
{

class LightNodeRPC : public bcos::rpc::JsonRpcInterface
{
    void call(std::string_view _groupID, std::string_view _nodeName, std::string_view _to,
        std::string_view _data, RespFunc _respFunc) override;

    void sendTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _data, bool _requireProof, RespFunc _respFunc) override;

    void getTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getTransactionReceipt(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getBlockByHash(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _blockHash, bool _onlyHeader, bool _onlyTxHash,
        RespFunc _respFunc) override;

    void getBlockByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) override;

    void getBlockHashByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) override;

    void getBlockNumber(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getCode(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override;

    void getABI(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override;

    void getSealerList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getObserverList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getPbftView(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getPendingTxSize(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getSyncStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;
    void getConsensusStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getSystemConfigByKey(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _keyValue, RespFunc _respFunc) override;

    void getTotalTransactionCount(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getGroupPeers(std::string_view _groupID, RespFunc _respFunc) override;
    void getPeers(RespFunc _respFunc) override;
    // get all the groupID list
    void getGroupList(RespFunc _respFunc) override;
    // get the group information of the given group
    void getGroupInfo(std::string_view _groupID, RespFunc _respFunc) override;
    // get all the group info list
    void getGroupInfoList(RespFunc _respFunc) override;
    // get the information of a given node
    void getGroupNodeInfo(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getGroupBlockNumber(RespFunc _respFunc) override;
};
}  // namespace bcos::rpc