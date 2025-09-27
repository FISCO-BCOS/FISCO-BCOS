#include <bcos-security/BcosKmsDataEncryption.h>
#include <bcos-security/HsmDataEncryption.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>


using namespace bcos::security;


namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(DataEntryptionTest)

BOOST_AUTO_TEST_CASE(testDataEncryption_normal)
{
    BcosKmsDataEncryption dataEncryption("bcos_data_key", false);
    dataEncryption.setCompatibilityVersion(
        static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

    std::string originData = "hello world";
    std::string encryptData = dataEncryption.encrypt(originData);
    std::string decryptData = dataEncryption.decrypt(encryptData);

    BOOST_CHECK_EQUAL(originData, decryptData);
}

BOOST_AUTO_TEST_CASE(testDataEncryption_sm)
{
    BcosKmsDataEncryption dataEncryption("bcos_data_key", true);
    dataEncryption.setCompatibilityVersion(
        static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

    std::string originData = "hello world";
    std::string encryptData = dataEncryption.encrypt(originData);
    std::string decryptData = dataEncryption.decrypt(encryptData);

    BOOST_CHECK_EQUAL(originData, decryptData);
}

// need a HSM environment
BOOST_AUTO_TEST_CASE(testDataEncryption_hsmSM4)
{
#if 0
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    std::string configFilePath =
    "/root/lucasli/FISCO-BCOS/tools/BcosAirBuilder/2nodes-hsm/127.0.0.1/node0/config.ini";
    nodeConfig->loadConfig(configFilePath);
    auto hsmdataEncryption = std::make_shared<HsmDataEncryption>(nodeConfig);

    std::string originData = "hello world!";
    std::string encryptData = hsmdataEncryption->encrypt(originData);
    std::string decryptData = hsmdataEncryption->decrypt(encryptData);

    BOOST_CHECK_EQUAL(originData, decryptData);
#endif
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
