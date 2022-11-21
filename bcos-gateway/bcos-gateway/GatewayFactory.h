/** @file GatewayFactory.h
 *  @author octopus
 *  @date 2021-05-17
 */

#pragma once

#include "bcos-gateway/libratelimit/GatewayRateLimiter.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/election/LeaderEntryPointInterface.h>
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/libamop/AMOPImpl.h>
#include <bcos-gateway/libratelimit/GatewayRateLimiter.h>
#include <sw/redis++/redis++.h>
#include <boost/asio/ssl.hpp>

namespace bcos
{
namespace gateway
{
class GatewayFactory
{
public:
    using Ptr = std::shared_ptr<GatewayFactory>;
    GatewayFactory(std::string const& _chainID, std::string const& _rpcServiceName,
        bcos::security::DataEncryptInterface::Ptr _dataEncrypt = nullptr)
      : m_chainID(_chainID), m_rpcServiceName(_rpcServiceName), m_dataEncrypt(_dataEncrypt)
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
        bool _server, const GatewayConfig::CertConfig& _certConfig);
    // build sm ssl context
    std::shared_ptr<boost::asio::ssl::context> buildSSLContext(
        bool _server, const GatewayConfig::SMCertConfig& _smCertConfig);

    //
    std::shared_ptr<ratelimiter::RateLimiterManager> buildRateLimiterManager(
        const GatewayConfig::RateLimiterConfig& _rateLimiterConfig,
        std::shared_ptr<sw::redis::Redis> _redis);

    /**
     * @brief construct Gateway
     *
     * @param _configPath
     * @param _airVersion
     * @param _entryPoint
     * @param _gatewayServiceName
     * @return Gateway::Ptr
     */
    Gateway::Ptr buildGateway(const std::string& _configPath, bool _airVersion,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint,
        std::string const& _gatewayServiceName);

    /**
     * @brief construct Gateway
     *
     * @param _config
     * @param _airVersion
     * @param _entryPoint
     * @param _gatewayServiceName
     * @return Gateway::Ptr
     */
    Gateway::Ptr buildGateway(GatewayConfig::Ptr _config, bool _airVersion,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint,
        std::string const& _gatewayServiceName);

    /**
     * @brief
     *
     * @param _rateLimiterConfig
     * @param _redisConfig
     * @return std::shared_ptr<ratelimiter::GatewayRateLimiter>
     */
    std::shared_ptr<ratelimiter::GatewayRateLimiter> buildGatewayRateLimiter(
        const GatewayConfig::RateLimiterConfig& _rateLimiterConfig,
        const GatewayConfig::RedisConfig& _redisConfig);

    /**
     * @brief
     *
     * @param _redisConfig
     * @return std::shared_ptr<sw::redis::Redis>
     */
    std::shared_ptr<sw::redis::Redis> initRedis(const GatewayConfig::RedisConfig& _redisConfig);

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

    bcos::security::DataEncryptInterface::Ptr m_dataEncrypt{nullptr};
};
}  // namespace gateway
}  // namespace bcos
