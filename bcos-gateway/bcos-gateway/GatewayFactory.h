/** @file GatewayFactory.h
 *  @author octopus
 *  @date 2021-05-17
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/election/LeaderEntryPointInterface.h>
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
    {
        initCert2PubHexHandler();
        initSSLContextPubHexHandler();
    }

    virtual ~GatewayFactory() = default;

    // init the function calc public hex from the cert
    void initCert2PubHexHandler();
    // init the function calc public key from the ssl context
    void initSSLContextPubHexHandler();

    std::function<bool(X509* cert, std::string& pubHex)> sslContextPubHandler()
    {
        return m_sslContextPubHandler;
    }

    std::function<bool(const std::string& priKey, std::string& pubHex)> certPubHexHandler()
    {
        return m_certPubHexHandler;
    }

    // build ssl context
    std::shared_ptr<boost::asio::ssl::context> buildSSLContext(
        const GatewayConfig::CertConfig& _certConfig);
    // build sm ssl context
    std::shared_ptr<boost::asio::ssl::context> buildSSLContext(
        const GatewayConfig::SMCertConfig& _smCertConfig);

    /**
     * @brief: construct Gateway
     * @param _configPath: config.ini path
     * @return void
     */
    Gateway::Ptr buildGateway(const std::string& _configPath, bool _airVersion,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint);
    /**
     * @brief: construct Gateway
     * @param _config: config parameter object
     * @return void
     */
    Gateway::Ptr buildGateway(GatewayConfig::Ptr _config, bool _airVersion,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint);

protected:
    virtual bcos::amop::AMOPImpl::Ptr buildAMOP(
        bcos::gateway::P2PInterface::Ptr _network, bcos::gateway::P2pID const& _p2pNodeID);
    virtual bcos::amop::AMOPImpl::Ptr buildLocalAMOP(
        bcos::gateway::P2PInterface::Ptr _network, bcos::gateway::P2pID const& _p2pNodeID);

private:
    std::function<bool(X509* cert, std::string& pubHex)> m_sslContextPubHandler;

    std::function<bool(const std::string& priKey, std::string& pubHex)> m_certPubHexHandler;

    void initFailOver(std::shared_ptr<Gateway> _gateWay,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint);

private:
    std::string m_chainID;
    std::string m_rpcServiceName;
};
}  // namespace gateway
}  // namespace bcos
