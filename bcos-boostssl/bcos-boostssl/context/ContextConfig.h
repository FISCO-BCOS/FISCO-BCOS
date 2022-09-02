
/** @file ContextConfig.h
 *  @author octopus
 *  @date 2021-06-14
 */

#pragma once
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <memory>

namespace bcos
{
namespace boostssl
{
namespace context
{
class ContextConfig
{
public:
    using Ptr = std::shared_ptr<ContextConfig>;
    using ConstPtr = std::shared_ptr<const ContextConfig>;

    ContextConfig() = default;
    ~ContextConfig() = default;

public:
    // cert for ssl connection
    struct CertConfig
    {
        std::string caCert;
        std::string nodeKey;
        std::string nodeCert;
    };

    // cert for sm ssl connection
    struct SMCertConfig
    {
        std::string caCert;
        std::string nodeCert;
        std::string nodeKey;
        std::string enNodeCert;
        std::string enNodeKey;
    };

public:
    /**
     * @brief: loads configuration items from the boostssl.ini
     * @param _configPath:
     * @return void
     */
    void initConfig(std::string const& _configPath);
    // loads ca configuration items from the configuration file
    void initCertConfig(const boost::property_tree::ptree& _pt);
    // loads sm ca configuration items from the configuration file
    void initSMCertConfig(const boost::property_tree::ptree& _pt);
    // check if file exist, exception will be throw if the file not exist
    void checkFileExist(const std::string& _path);

public:
    bool isCertPath() const { return m_isCertPath; }
    void setIsCertPath(bool _isCertPath) { m_isCertPath = _isCertPath; }

    std::string sslType() const { return m_sslType; }
    void setSslType(const std::string _sslType) { m_sslType = _sslType; }

    const CertConfig& certConfig() const { return m_certConfig; }
    void setCertConfig(const CertConfig& _certConfig) { m_certConfig = _certConfig; }

    const SMCertConfig& smCertConfig() const { return m_smCertConfig; }
    void setSmCertConfig(const SMCertConfig& _smCertConfig) { m_smCertConfig = _smCertConfig; }

    std::string moduleName() { return m_moduleName; }
    void setModuleName(std::string _moduleName) { m_moduleName = _moduleName; }


private:
    // is the cert path or cert file content
    bool m_isCertPath = true;
    // ssl type, support ssl && sm_ssl
    std::string m_sslType;
    // cert config for ssl
    CertConfig m_certConfig;
    SMCertConfig m_smCertConfig;
    std::string m_moduleName = "DEFAULT";
};

}  // namespace context
}  // namespace boostssl
}  // namespace bcos