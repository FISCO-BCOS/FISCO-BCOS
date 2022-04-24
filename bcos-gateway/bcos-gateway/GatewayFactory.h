/** @file GatewayFactory.h
 *  @author octopus
 *  @date 2021-05-17
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/libamop/AMOPImpl.h>
#include <boost/asio/ssl.hpp>

namespace bcos
{
namespace gateway
{
class GatewayFactory
{
public:
    using Ptr = std::shared_ptr<GatewayFactory>;
    GatewayFactory(std::string const& _chainID, std::string const& _rpcServiceName)
      : m_chainID(_chainID), m_rpcServiceName(_rpcServiceName)
    {}

    virtual ~GatewayFactory() = default;

    /**
     * @brief: construct Gateway
     * @param _configPath: config.ini path
     * @return void
     */
    Gateway::Ptr buildGateway(const std::string& _configPath, bool _airVersion);
    /**
     * @brief: construct Gateway
     * @param _config: config parameter object
     * @return void
     */
    Gateway::Ptr buildGateway(GatewayConfig::Ptr _config, bool _airVersion);

protected:
    virtual bcos::amop::AMOPImpl::Ptr buildAMOP(
        bcos::gateway::P2PInterface::Ptr _network, bcos::gateway::P2pID const& _p2pNodeID);
    virtual bcos::amop::AMOPImpl::Ptr buildLocalAMOP(
        bcos::gateway::P2PInterface::Ptr _network, bcos::gateway::P2pID const& _p2pNodeID);

private:
    std::string m_chainID;
    std::string m_rpcServiceName;
};
}  // namespace gateway
}  // namespace bcos
