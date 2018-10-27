#pragma once

#include "ExecutiveContext.h"
#include <libdevcore/OverlayDB.h>
#include <libexecutive/StateFactoryInterface.h>
#include <libstorage/Storage.h>
namespace dev
{
namespace blockverifier
{
class ExecutiveContextFactory : public std::enable_shared_from_this<ExecutiveContextFactory>
{
public:
    typedef std::shared_ptr<ExecutiveContextFactory> Ptr;

    virtual ~ExecutiveContextFactory(){};

    virtual void initExecutiveContext(BlockInfo blockInfo, ExecutiveContext::Ptr context);

    virtual void setStateStorage(dev::storage::Storage::Ptr stateStorage);

    virtual void setStateFactory(
        std::shared_ptr<dev::executive::StateFactoryInterface> stateFactoryInterface);

    void setPrecompiledContract(
        std::unordered_map<Address, dev::eth::PrecompiledContract> const& precompiledContract);

private:
    dev::storage::Storage::Ptr m_stateStorage;
    std::shared_ptr<dev::executive::StateFactoryInterface> m_stateFactoryInterface;
    std::unordered_map<Address, dev::eth::PrecompiledContract> m_precompiledContract;
};

}  // namespace blockverifier

}  // namespace dev
