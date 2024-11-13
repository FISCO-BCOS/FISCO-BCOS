/**
 * @brief : Mock AWS KMS service for unit testing using Boost
 * @file AwsKmsWrapperTest.cpp
 */

#include <aws/core/VersionConfig.h>
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/utils/base64/Base64.h>
#include <aws/kms/KMSClient.h>
#include <aws/kms/model/DecryptRequest.h>
#include <aws/kms/model/EncryptRequest.h>
#include <bcos-security/cloudkms/AwsKmsWrapper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::security;

namespace bcos::test
{

// Mock KMS client
class MockKMSClient : public Aws::KMS::KMSClient
{
public:
    mutable bool encryptCalled = false;
    mutable bool decryptCalled = false;
    mutable std::string lastEncryptKeyId;
    mutable Aws::Utils::ByteBuffer lastPlaintext;
    mutable Aws::Utils::ByteBuffer lastCiphertext;

    Aws::KMS::Model::EncryptOutcome Encrypt(
        const Aws::KMS::Model::EncryptRequest& request) const override
    {
        encryptCalled = true;
        lastEncryptKeyId = request.GetKeyId();
        lastPlaintext = request.GetPlaintext();

        if (shouldFailEncryption)
        {
            return Aws::KMS::Model::EncryptOutcome(Aws::Client::AWSError<Aws::KMS::KMSErrors>(
                Aws::KMS::KMSErrors::INVALID_KEY_USAGE, false));
        }

        Aws::KMS::Model::EncryptResult result;
        // Simulate encryption by base64 encoding
        Aws::Utils::ByteBuffer plainBuffer(
            (unsigned char*)request.GetPlaintext().GetUnderlyingData(),
            request.GetPlaintext().GetLength());
        Aws::Utils::Base64::Base64 base64;
        auto encoded = base64.Encode(plainBuffer);
        result.SetCiphertextBlob(
            Aws::Utils::ByteBuffer((unsigned char*)encoded.data(), encoded.length()));
        lastCiphertext = result.GetCiphertextBlob();
        return Aws::KMS::Model::EncryptOutcome(result);
    }

    Aws::KMS::Model::DecryptOutcome Decrypt(
        const Aws::KMS::Model::DecryptRequest& request) const override
    {
        decryptCalled = true;

        if (shouldFailDecryption)
        {
            return Aws::KMS::Model::DecryptOutcome(Aws::Client::AWSError<Aws::KMS::KMSErrors>(
                Aws::KMS::KMSErrors::INVALID_CIPHERTEXT, false));
        }

        Aws::KMS::Model::DecryptResult result;
        const auto& resultCipher = request.GetCiphertextBlob();
        auto resultCipherStr =
            std::string((char*)resultCipher.GetUnderlyingData(), resultCipher.GetLength());
        // Simulate decryption by base64 decoding
        Aws::Utils::Base64::Base64 base64;
        auto decoded = base64.Decode(resultCipherStr);
        result.SetPlaintext(Aws::Utils::ByteBuffer(
            (unsigned char*)decoded.GetUnderlyingData(), decoded.GetLength()));
        return Aws::KMS::Model::DecryptOutcome(result);
    }

    void setShouldFailEncryption(bool should) { shouldFailEncryption = should; }
    void setShouldFailDecryption(bool should) { shouldFailDecryption = should; }

private:
    bool shouldFailEncryption = false;
    bool shouldFailDecryption = false;
};

struct AwsKmsWrapperFixture
{
    AwsKmsWrapperFixture()
    {
        // Initialize AWS SDK
        Aws::SDKOptions options;
        Aws::InitAPI(options);

        // Setup test credentials
        region = "us-west-2";
        accessKey = "test-access-key";
        secretKey = "test-secret-key";
        keyId = "test-key-id";

        // Create mock client
        mockKmsClient = std::make_shared<MockKMSClient>();

        // Create wrapper
        wrapper = std::make_shared<AwsKmsWrapper>(region, accessKey, secretKey, keyId);
        // Inject mock client
        wrapper->setKmsClient(mockKmsClient);
    }

    ~AwsKmsWrapperFixture()
    {
        // Cleanup AWS SDK
        Aws::ShutdownAPI(options);
    }

    std::string region;
    std::string accessKey;
    std::string secretKey;
    std::string keyId;
    Aws::SDKOptions options;
    std::shared_ptr<MockKMSClient> mockKmsClient;
    std::shared_ptr<AwsKmsWrapper> wrapper;
};

static bcos::bytes stringToBytes(const std::string& str)
{
    return bcos::bytes(str.begin(), str.end());
}

// static std::string bytesToString(const bcos::bytes& bytes) {
//     return std::string(bytes.begin(), bytes.end());
// }

BOOST_AUTO_TEST_SUITE(AwsKmsWrapperTest)

BOOST_FIXTURE_TEST_CASE(testSuccessfulEncryption, AwsKmsWrapperFixture)
{
    std::string testData = "Hello, KMS encryption test!";
    auto contents = std::make_shared<bcos::bytes>(stringToBytes(testData));

    auto encryptedContents = wrapper->encryptContents(contents);

    BOOST_CHECK(mockKmsClient->encryptCalled);
    BOOST_CHECK_EQUAL(mockKmsClient->lastEncryptKeyId, keyId);
    BOOST_CHECK(encryptedContents != nullptr);
}

BOOST_FIXTURE_TEST_CASE(testFailedEncryption, AwsKmsWrapperFixture)
{
    mockKmsClient->setShouldFailEncryption(true);

    std::string testData = "Test encryption failure";
    auto contents = std::make_shared<bcos::bytes>(stringToBytes(testData));

    BOOST_CHECK(mockKmsClient->encryptCalled == false);
}

BOOST_FIXTURE_TEST_CASE(testEncryptionDecryptionRoundTrip, AwsKmsWrapperFixture)
{
    std::string testData = "Test round trip";
    auto contents = std::make_shared<bcos::bytes>(stringToBytes(testData));

    auto encryptedContents = wrapper->encryptContents(contents);
    BOOST_CHECK(mockKmsClient->encryptCalled);
    BOOST_CHECK(encryptedContents != nullptr);

    auto decryptedContents = wrapper->decryptContents(encryptedContents);
    BOOST_CHECK(mockKmsClient->decryptCalled);
    BOOST_CHECK(decryptedContents != nullptr);
    std::string resultStr = std::string(decryptedContents->begin(), decryptedContents->end());

    BOOST_CHECK_EQUAL(resultStr, testData);
}

BOOST_FIXTURE_TEST_CASE(testEmptyContent, AwsKmsWrapperFixture)
{
    std::string emptyContents = "";
    auto contents = std::make_shared<bcos::bytes>(stringToBytes(emptyContents));

    auto encryptedContents = wrapper->encryptContents(contents);
    BOOST_CHECK(mockKmsClient->encryptCalled);
    BOOST_CHECK(encryptedContents != nullptr);

    auto decryptedContents = wrapper->decryptContents(encryptedContents);
    BOOST_CHECK(mockKmsClient->decryptCalled);
    BOOST_CHECK(decryptedContents != nullptr);
    BOOST_CHECK(decryptedContents->empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
