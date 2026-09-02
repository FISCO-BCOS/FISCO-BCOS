#include <bcos-crypto/encrypt/HsmSM4Crypto.h>
#include <bcos-security/BcosKmsDataEncryption.h>
#include <bcos-security/HsmDataEncryption.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <memory>


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

// The HSM decrypt length guards fire BEFORE the SDF provider is touched, so
// they are testable without an HSM: a stored blob shorter than the IV must
// throw DecryptFailed instead of wrapping size - SM4_IV_DATA_SIZE into a wild
// IV pointer / ~2^64 allocation, and a zero-length cipher must be rejected
// instead of reading back() off an empty buffer.
BOOST_AUTO_TEST_CASE(testHsmDecryptLengthGuards)
{
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    HsmDataEncryption hsmDataEncryption(nodeConfig);
    std::array<uint8_t, 8> shortBlob{};
    BOOST_CHECK_THROW(
        hsmDataEncryption.decrypt(shortBlob.data(), shortBlob.size()), DecryptFailed);

    bcos::crypto::HsmSM4Crypto sm4("/nonexistent/libsdf.so");
    std::array<unsigned char, 16> iv{};
    std::array<unsigned char, 16> cipher{};
    BOOST_CHECK_THROW(sm4.symmetricDecryptWithInternalKey(
                          cipher.data(), 0, 0, iv.data(), iv.size()),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
