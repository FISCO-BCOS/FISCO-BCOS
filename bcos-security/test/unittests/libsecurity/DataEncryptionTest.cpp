#include <boost/test/unit_test.hpp>
#include <bcos-security/DataEncryption.h>
#include <bcos-security/HsmDataEncryption.h>
#include <bcos-tool/NodeConfig.h>


using namespace bcos::security;

namespace bcos
{
namespace test
{
    BOOST_AUTO_TEST_CASE(testDataEncryption_normal)
    {
        DataEncryption dataEncryption("bcos_data_key", false);
        dataEncryption.setCompatibilityVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

        std::string originData = "hello world";
        std::string encryptData = dataEncryption.encrypt(originData);
        std::string decryptData = dataEncryption.decrypt(encryptData);

        BOOST_CHECK_EQUAL(originData, decryptData);
    }

    BOOST_AUTO_TEST_CASE(testDataEncryption_sm)
    {
        DataEncryption dataEncryption("bcos_data_key", false);
        dataEncryption.setCompatibilityVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

        std::string originData = "hello world";
        std::string encryptData = dataEncryption.encrypt(originData);
        std::string decryptData = dataEncryption.decrypt(encryptData);

        BOOST_CHECK_EQUAL(originData, decryptData);
    }

    // need a HSM environment
    // BOOST_AUTO_TEST_CASE(testDataEncryption_hsmSM4)
    // {
    //     auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    //     std::string configFilePath = "/root/lucasli/FISCO-BCOS/tools/BcosAirBuilder/2nodes-hsm/127.0.0.1/node0/config.ini";
    //     nodeConfig->loadConfig(configFilePath);
    //     auto hsmdataEncryption = std::make_shared<HsmDataEncryption>(nodeConfig);

    //     std::string originData = "hello world!";
    //     std::string encryptData = hsmdataEncryption->encrypt(originData);
    //     std::string decryptData = hsmdataEncryption->decrypt(encryptData);

    //     BOOST_CHECK_EQUAL(originData, decryptData);
    // }
}
}