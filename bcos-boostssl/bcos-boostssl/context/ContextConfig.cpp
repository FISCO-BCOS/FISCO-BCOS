/** @file ContextConfig.cpp
 *  @author octopus
 *  @date 2021-06-14
 */

#include <bcos-boostssl/context/Common.h>
#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-boostssl/utilities/BoostLog.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::context;
/**
 * @brief: loads configuration items from the config.ini
 * @param _configPath: config.ini path
 * @return void
 */
void ContextConfig::initConfig(std::string const& _configPath)
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(_configPath, pt);
        std::string sslType = pt.get<std::string>("common.ssl_type", "ssl");
        if ("sm_ssl" != sslType)
        {  // SSL
            initCertConfig(pt);
        }
        else
        {  // SM SSL
            initSMCertConfig(pt);
        }

        m_sslType = sslType;
    }
    catch (const std::exception& e)
    {
        boost::filesystem::path currentPath(boost::filesystem::current_path());

        CONTEXT_LOG(WARNING) << LOG_DESC("initConfig failed") << LOG_KV("configPath", _configPath)
                           << LOG_KV("currentPath", currentPath.string())
                           << LOG_KV("error", boost::diagnostic_information(e));


        BOOST_THROW_EXCEPTION(std::runtime_error("initConfig: currentPath:" + currentPath.string() +
                                                 " ,error:" + boost::diagnostic_information(e)));
    }

    CONTEXT_LOG(INFO) << LOG_DESC("initConfig") << LOG_KV("sslType", m_sslType)
                      << LOG_KV("configPath", _configPath);
}

/// loads ca configuration items from the configuration file
void ContextConfig::initCertConfig(const boost::property_tree::ptree& _pt)
{
    std::string caPath = _pt.get<std::string>("cert.ca_path", "./");
    std::string caCertFile = caPath + "/" + _pt.get<std::string>("cert.ca_cert", "ca.crt");
    std::string nodeCertFile = caPath + "/" + _pt.get<std::string>("cert.node_cert", "node.crt");
    std::string nodeKeyFile = caPath + "/" + _pt.get<std::string>("cert.node_key", "node.key");

    CONTEXT_LOG(INFO) << LOG_DESC("initCertConfig") << LOG_KV("ca_path", caPath)
                      << LOG_KV("ca_cert", caCertFile) << LOG_KV("node_cert", nodeCertFile)
                      << LOG_KV("node_key", nodeKeyFile);

    checkFileExist(caCertFile);
    checkFileExist(nodeCertFile);
    checkFileExist(nodeKeyFile);

    CertConfig certConfig;
    certConfig.caCert = caCertFile;
    certConfig.nodeCert = nodeCertFile;
    certConfig.nodeKey = nodeKeyFile;

    m_certConfig = certConfig;

    CONTEXT_LOG(INFO) << LOG_DESC("initCertConfig") << LOG_KV("ca", certConfig.caCert)
                      << LOG_KV("node_cert", certConfig.nodeCert)
                      << LOG_KV("node_key", certConfig.nodeKey);
}

// loads sm ca configuration items from the configuration file
void ContextConfig::initSMCertConfig(const boost::property_tree::ptree& _pt)
{
    std::string caPath = _pt.get<std::string>("cert.ca_path", "./");
    std::string smCaCertFile = caPath + "/" + _pt.get<std::string>("cert.sm_ca_cert", "sm_ca.crt");
    std::string smNodeCertFile =
        caPath + "/" + _pt.get<std::string>("cert.sm_node_cert", "sm_node.crt");
    std::string smNodeKeyFile =
        caPath + "/" + _pt.get<std::string>("cert.sm_node_key", "sm_node.key");
    std::string smEnNodeCertFile =
        caPath + "/" + _pt.get<std::string>("cert.sm_ennode_cert", "sm_ennode.crt");
    std::string smEnNodeKeyFile =
        caPath + "/" + _pt.get<std::string>("cert.sm_ennode_key", "sm_ennode.key");

    checkFileExist(smCaCertFile);
    checkFileExist(smNodeCertFile);
    checkFileExist(smNodeKeyFile);
    checkFileExist(smEnNodeCertFile);
    checkFileExist(smEnNodeKeyFile);

    SMCertConfig smCertConfig;
    smCertConfig.caCert = smCaCertFile;
    smCertConfig.nodeCert = smNodeCertFile;
    smCertConfig.nodeKey = smNodeKeyFile;
    smCertConfig.enNodeCert = smEnNodeCertFile;
    smCertConfig.enNodeKey = smEnNodeKeyFile;

    m_smCertConfig = smCertConfig;

    CONTEXT_LOG(INFO) << LOG_DESC("initSMCertConfig") << LOG_KV("ca_path", caPath)
                      << LOG_KV("sm_ca_cert", smCertConfig.caCert)
                      << LOG_KV("sm_node_cert", smCertConfig.nodeCert)
                      << LOG_KV("sm_node_key", smCertConfig.nodeKey)
                      << LOG_KV("sm_ennode_cert", smCertConfig.enNodeCert)
                      << LOG_KV("sm_ennode_key", smCertConfig.enNodeKey);
}

void ContextConfig::checkFileExist(const std::string& _path)
{
    auto isExist = boost::filesystem::exists(boost::filesystem::path(_path));
    if (!isExist)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("file not exist: " + _path));
    }
}
