#pragma once

#include "libinitializer/FrontServiceInitializer.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/tars/FrontService.h>
#include <bcos-utilities/Common.h>
#include <servant/Communicator.h>
#include <servant/Global.h>
#include <boost/core/ignore_unused.hpp>
namespace bcostars
{
struct FrontServiceParam
{
    bcos::initializer::FrontServiceInitializer::Ptr frontServiceInitializer;
};
class FrontServiceServer : public FrontService
{
public:
    FrontServiceServer(FrontServiceParam const& _param)
      : m_frontServiceInitializer(_param.frontServiceInitializer)
    {}

    ~FrontServiceServer() override {}
    void initialize() override {}
    void destroy() override {}

    bcostars::Error asyncGetGroupNodeInfo(
        GroupNodeInfo& groupNodeInfo, tars::TarsCurrentPtr current) override;

    void asyncSendBroadcastMessage(tars::Int32 _nodeType, tars::Int32 moduleID,
        const vector<tars::Char>& data, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncSendMessageByNodeID(tars::Int32 moduleID, const vector<tars::Char>& nodeID,
        const vector<tars::Char>& data, tars::UInt32 timeout, tars::Bool requireRespCallback,
        vector<tars::Char>& responseNodeID, vector<tars::Char>& responseData, std::string& seq,
        tars::TarsCurrentPtr current) override;

    void asyncSendMessageByNodeIDs(tars::Int32 moduleID, const vector<vector<tars::Char>>& nodeIDs,
        const vector<tars::Char>& data, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncSendResponse(const std::string& id, tars::Int32 moduleID,
        const vector<tars::Char>& nodeID, const vector<tars::Char>& data,
        tars::TarsCurrentPtr current) override;
    bcostars::Error onReceiveBroadcastMessage(const std::string& groupID,
        const vector<tars::Char>& nodeID, const vector<tars::Char>& data,
        tars::TarsCurrentPtr current) override;

    bcostars::Error onReceiveMessage(const std::string& groupID, const vector<tars::Char>& nodeID,
        const vector<tars::Char>& data, tars::TarsCurrentPtr current) override;

    bcostars::Error onReceiveGroupNodeInfo(const std::string& groupID,
        const bcostars::GroupNodeInfo& groupNodeInfo, tars::TarsCurrentPtr current) override;

private:
    bcos::initializer::FrontServiceInitializer::Ptr m_frontServiceInitializer;
};
}  // namespace bcostars