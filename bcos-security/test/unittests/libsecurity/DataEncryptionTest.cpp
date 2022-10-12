#include <boost/test/unit_test.hpp>
#include <bcos-security/DataEncryption.h>

using namespace bcos::security;

namespace bcos
{
namespace test
{
    BOOST_AUTO_TEST_CASE(testDataEncryption_normal)
    {
        DataEncryption dataEncryption{ nullptr };
        dataEncryption.init("bcos_data_key", false);

        std::string originData = "hello world";
        std::string encryptData = dataEncryption.encrypt(originData);
        std::string decryptData = dataEncryption.decrypt(encryptData);

        BOOST_CHECK_EQUAL(originData, decryptData);
    }

    BOOST_AUTO_TEST_CASE(testDataEncryption_sm)
    {
        DataEncryption dataEncryption{ nullptr };
        dataEncryption.init("bcos_data_key", true);

        std::string originData = "hello world";
        std::string encryptData = dataEncryption.encrypt(originData);
        std::string decryptData = dataEncryption.decrypt(encryptData);

        BOOST_CHECK_EQUAL(originData, decryptData);
    }
}
}