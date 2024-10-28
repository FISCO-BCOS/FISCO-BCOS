#include "AwsKmsWrapper.h"
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/Array.h>
#include <aws/kms/model/DecryptRequest.h>
#include <aws/kms/model/EncryptRequest.h>

AWSKMSWrapper::AWSKMSWrapper(
    const std::string& region, const std::string& accessKey, const std::string& secretKey)
{
    // create credentials
    Aws::Auth::AWSCredentials credentials(accessKey.c_str(), secretKey.c_str());

    // configure client
    Aws::Client::ClientConfiguration config;
    config.region = region;

    // use credentials and config to create client
    m_kmsClient = std::make_shared<Aws::KMS::KMSClient>(credentials, config);
}

std::pair<bool, std::vector<unsigned char>> AWSKMSWrapper::encrypt(
    const std::string& keyId, const std::string& inputFilePath)
{
    std::string plaintext = readFile(inputFilePath);
    Aws::KMS::Model::EncryptRequest request;
    request.SetKeyId(keyId);

    Aws::Utils::ByteBuffer plaintextBlob(
        reinterpret_cast<const unsigned char*>(plaintext.c_str()), plaintext.size());
    request.SetPlaintext(plaintextBlob);

    auto outcome = m_kmsClient->Encrypt(request);
    if (!outcome.IsSuccess())
    {
        std::cerr << "Encryption error: " << outcome.GetError().GetMessage() << std::endl;
        return {false, std::vector<unsigned char>()};
    }

    const auto& resultBlob = outcome.GetResult().GetCiphertextBlob();
    std::vector<unsigned char> ciphertext(
        resultBlob.GetUnderlyingData(), resultBlob.GetUnderlyingData() + resultBlob.GetLength());
    std::cout << "Encrypted data : " << toHex(ciphertext) << " bytes" << std::endl;
    return {true, ciphertext};
}

std::pair<bool, std::string> AWSKMSWrapper::decrypt(const std::vector<unsigned char>& ciphertext)
{
    Aws::KMS::Model::DecryptRequest request;

    Aws::Utils::ByteBuffer ciphertextBlob(ciphertext.data(), ciphertext.size());
    request.SetCiphertextBlob(ciphertextBlob);

    auto outcome = m_kmsClient->Decrypt(request);
    if (!outcome.IsSuccess())
    {
        std::cerr << "Decryption error: " << outcome.GetError().GetMessage() << std::endl;
        return {false, ""};
    }

    const auto& resultBlob = outcome.GetResult().GetPlaintext();
    std::string plaintext(
        reinterpret_cast<const char*>(resultBlob.GetUnderlyingData()), resultBlob.GetLength());

    return {true, plaintext};
}