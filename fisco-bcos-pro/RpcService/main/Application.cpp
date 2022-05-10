#include "Common/TarsUtils.h"
#include "RpcService/RpcInitializer.h"
#include "RpcService/RpcServiceServer.h"
#include "libinitializer/CommandHelper.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <tarscpp/servant/Application.h>

using namespace bcostars;
class RpcServiceApp : public Application
{
public:
    RpcServiceApp() {}

    ~RpcServiceApp() override {}

    void initialize() override
    {
        // Note: since tars Application catch the exception and output the error message with
        // e.what() which unable to explicitly display specific error messages, we actively catch
        // and output exception information here
        try
        {
            auto configDir = ServerConfig::BasePath;
            m_iniConfigPath = configDir + "/config.ini";
            addConfig("config.ini");

            BCOS_LOG(INFO) << LOG_BADGE("[Rpc][Application]") << LOG_DESC("add config.ini")
                           << LOG_KV("configDir", configDir);

            initService(configDir);
            RpcServiceParam param;
            param.rpcInitializer = m_rpcInitializer;
            addServantWithParams<RpcServiceServer, RpcServiceParam>(
                getProxyDesc(bcos::protocol::RPC_SERVANT_NAME), param);
            // init rpc
            auto ret = getEndPointDescByAdapter(this, bcos::protocol::RPC_SERVANT_NAME);
            if (!ret.first)
            {
                throw std::runtime_error("load endpoint information failed");
            }
            std::string clientID = ret.second;
            BCOS_LOG(INFO) << LOG_DESC("begin init rpc") << LOG_KV("rpcID", ret.second);
            param.rpcInitializer->setClientID(ret.second);
            m_rpcInitializer->start();
        }
        catch (std::exception const& e)
        {
            std::cout << "init RpcService failed, error: " << boost::diagnostic_information(e)
                      << std::endl;
            throw e;
        }
    }

    void destroyApp() override
    {
        // terminate the network threads
        Application::terminate();
        // stop the nodeService
        m_rpcInitializer->stop();
    }

protected:
    virtual void initService(std::string const& _configDir)
    {
        // !!! Notice:
        auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(
            std::make_shared<bcos::crypto::KeyFactoryImpl>());
        nodeConfig->loadConfig(m_iniConfigPath, false);
        if (nodeConfig->rpcSmSsl())
        {
            addConfig("sm_ca.crt");
            addConfig("sm_ssl.crt");
            addConfig("sm_enssl.crt");
            addConfig("sm_ssl.key");
            addConfig("sm_enssl.key");
        }
        else
        {
            addConfig("ca.crt");
            addConfig("ssl.key");
            addConfig("ssl.crt");
        }

        // init the log
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(m_iniConfigPath, pt);
        m_logInitializer = std::make_shared<bcos::BoostLogInitializer>();
        m_logInitializer->setLogPath(getLogPath());
        m_logInitializer->initLog(pt);
        // for stat the nodeVersion
        bcos::initializer::showNodeVersionMetric();
        nodeConfig->loadServiceConfig(pt);

        m_rpcInitializer = std::make_shared<RpcInitializer>(_configDir, nodeConfig);
    }

private:
    std::string m_iniConfigPath;
    bcos::BoostLogInitializer::Ptr m_logInitializer;
    RpcInitializer::Ptr m_rpcInitializer;
};

int main(int argc, char* argv[])
{
    try
    {
        bcos::initializer::initCommandLine(argc, argv);
        RpcServiceApp app;
        app.main(argc, argv);
        app.waitForShutdown();

        return 0;
    }
    catch (std::exception& e)
    {
        cerr << "std::exception:" << e.what() << std::endl;
    }
    return -1;
}