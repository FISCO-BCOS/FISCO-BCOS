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
#include <sw/redis++/redis++.h>
#include <boost/asio/ssl.hpp>
#include <utility>

namespace bcos::gateway
{
class GatewayFactory
{
public:
    using Ptr = std::shared_ptr<GatewayFactory>;
    GatewayFactory(std::string const& _chainID, std::string const& _rpcServiceName,
        bcos::security::DataEncryptInterface::Ptr _dataEncrypt = nullptr)
      : m_chainID(_chainID), m_rpcServiceName(_rpcServiceName), m_dataEncrypt(_dataEncrypt)
    {
        // For compatibility, p2p communication between nodes still uses the old public key analysis
        // method
        initSSLContextPubHexHandler();
        // the new old public key analysis method is used for black white list
        initSSLContextPubHexHandlerWithoutExtInfo();
        initCert2PubHexHandler();
    }

    virtual ~GatewayFactory() = default;

    // init the function calc public key from the ssl context
    // in this way, the public key will be parsed in front of a string of prefixes: 3082010a02820101
    // and suffixes: 0203010001 for rsa certificate
    void initSSLContextPubHexHandler();
    // init the function calc public key from the ssl context
    void initSSLContextPubHexHandlerWithoutExtInfo();
    // init the function calc public hex from the cert
    void initCert2PubHexHandler();

    std::function<bool(X509* cert, std::string& pubHex)> sslContextPubHandler()
    {
        return m_sslContextPubHandler;
    }

    std::function<bool(X509* cert, std::string& pubHex)> sslContextPubHandlerWithoutExtInfo()
    {
        return m_sslContextPubHandlerWithoutExtInfo;
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

    // build Service
    std::shared_ptr<Service> buildService(const GatewayConfig::Ptr& _config);

    /**
     * @brief construct Gateway for air
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
     * @brief construct Gateway for pro
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
    std::function<bool(X509* cert, std::string& pubHex)> m_sslContextPubHandlerWithoutExtInfo;

    std::function<bool(const std::string& priKey, std::string& pubHex)> m_certPubHexHandler;

    void initFailOver(std::shared_ptr<Gateway> _gateWay,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint);

private:
    std::string m_chainID;
    std::string m_rpcServiceName;

    bcos::security::DataEncryptInterface::Ptr m_dataEncrypt{nullptr};
};
}  // namespace bcos::gateway
