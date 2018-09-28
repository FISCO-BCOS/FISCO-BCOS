#pragma once

#include "ExecutiveContext.h"
#include <libstorage/Storage.h>
#include <libweb3jsonrpc/ChannelRPCServer.h>

namespace dev
{
namespace blockverifier
{
class ExecutiveContextFactory : public std::enable_shared_from_this<ExecutiveContextFactory>
{
public:
    typedef std::shared_ptr<ExecutiveContextFactory> Ptr;

    virtual ~PrecompiledContextFactory(){};

    virtual void initExecutiveContext(BlockInfo blockInfo, ExecutiveContext::Ptr context);

    virtual void setStateStorage(dev::storage::Storage::Ptr stateStorage);
    virtual void setOverlayDB(OverlayDB const& db);

private:
    dev::storage::Storage::Ptr m_stateStorage;
    OverlayDB const& m_db;
};

}  // namespace blockverifier

}  // namespace dev
