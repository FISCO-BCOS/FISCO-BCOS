/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
#pragma once

#include <bcos-ops/OpsError.h>
#include <bcos-ops/RpcCall.h>
#include <json/json.h>
#include <map>
#include <string>

namespace bcos::ops::test
{
// Response samples in the shape the node emits (JsonRpcImpl_2_0): getSyncStatus and
// getConsensusStatus carry a JSON object encoded as a string, getGroupInfo's nodeList[].iniConfig
// is a string too. A healthy 4-node chain at height 128.
inline std::string const c_consensusStatusHealthy =
    R"({"blockNumber":128,"changeCycle":0,"connectedNodeList":4,"consensusNodeList":[{"index":0,"nodeID":"3a1f00","weight":1,"termWeight":1},{"index":1,"nodeID":"3a1f01","weight":1,"termWeight":1},{"index":2,"nodeID":"3a1f02","weight":1,"termWeight":1},{"index":3,"nodeID":"3a1f03","weight":1,"termWeight":1}],"consensusNodesNum":4,"hash":"7b00","index":0,"isConsensusNode":true,"leaderIndex":2,"maxFaultyQuorum":1,"minRequiredQuorum":3,"nodeID":"3a1f00","timeout":false,"view":0})";

inline std::string const c_syncStatusHealthy =
    R"({"archivedBlockNumber":0,"blockNumber":128,"genesisHash":"00","isSyncing":false,"knownHighestNumber":128,"knownLatestHash":"7b00","latestHash":"7b00","nodeID":"3a1f00","peers":[{"archivedBlockNumber":0,"blockNumber":128,"genesisHash":"00","latestHash":"7b00","nodeID":"3a1f01"},{"archivedBlockNumber":0,"blockNumber":128,"genesisHash":"00","latestHash":"7b00","nodeID":"3a1f02"},{"archivedBlockNumber":0,"blockNumber":128,"genesisHash":"00","latestHash":"7b00","nodeID":"3a1f03"}]})";

inline std::string const c_iniConfigSample =
    R"({"binaryInfo":{"buildTime":"20261010","gitCommitHash":"deadbeef","platform":"Darwin","version":"3.18.0"},"chainID":"chain0","gatewayServiceName":"","groupID":"group0","isAuthCheck":false,"isSerialExecute":false,"isWasm":false,"nodeID":"3a1f00","nodeName":"node0","rpcServiceName":"","smCryptoType":false})";

inline Json::Value jsonOf(std::string const& _text)
{
    return parseJson(_text, "fixture");
}

/// table-driven RpcCall: method → result. getSyncStatus/getConsensusStatus results are given as
/// strings exactly as the node returns them. Missing method → throws OpsError like a transport
/// failure would.
class FakeRpc
{
public:
    FakeRpc()
    {
        m_results["getConsensusStatus"] = c_consensusStatusHealthy;
        m_results["getSyncStatus"] = c_syncStatusHealthy;
        Json::Value groupInfo;
        groupInfo["chainID"] = "chain0";
        groupInfo["groupID"] = "group0";
        Json::Value node;
        node["name"] = "node0";
        node["nodeID"] = "3a1f00";
        node["iniConfig"] = c_iniConfigSample;
        groupInfo["nodeList"] = Json::Value(Json::arrayValue);
        groupInfo["nodeList"].append(node);
        m_results["getGroupInfo"] = groupInfo;
        m_results["getBlockNumber"] = 128;
        Json::Value header;
        header["hash"] = "0x7b00";
        header["number"] = 128;
        header["timestamp"] = Json::Int64(1760000000000);
        m_results["getBlockByNumber"] = header;
        m_results["getPendingTxSize"] = 0;
        Json::Value groupList;
        groupList["groupList"] = Json::Value(Json::arrayValue);
        groupList["groupList"].append("group0");
        m_results["getGroupList"] = groupList;
    }

    void set(std::string const& _method, Json::Value _result)
    {
        m_results[_method] = std::move(_result);
    }
    void remove(std::string const& _method) { m_results.erase(_method); }
    /// replaces one key inside the string-encoded consensus status
    void setConsensus(std::string const& _key, Json::Value _value)
    {
        auto status = jsonOf(m_results["getConsensusStatus"].asString());
        status[_key] = std::move(_value);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        m_results["getConsensusStatus"] = Json::writeString(builder, status);
    }
    void setSync(std::string const& _key, Json::Value _value)
    {
        auto status = jsonOf(m_results["getSyncStatus"].asString());
        status[_key] = std::move(_value);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        m_results["getSyncStatus"] = Json::writeString(builder, status);
    }

    RpcCall call()
    {
        return [this](std::string_view _method, Json::Value const& _params) -> Json::Value {
            m_calls.emplace_back(std::string(_method), _params);
            auto it = m_results.find(std::string(_method));
            if (it == m_results.end())
            {
                throw OpsError(c_exitUsage, std::string(_method) + " failed: no connection");
            }
            return unwrapStringResult(_method, it->second);
        };
    }
    std::vector<std::pair<std::string, Json::Value>> const& calls() const { return m_calls; }

private:
    std::map<std::string, Json::Value> m_results;
    std::vector<std::pair<std::string, Json::Value>> m_calls;
};
}  // namespace bcos::ops::test
