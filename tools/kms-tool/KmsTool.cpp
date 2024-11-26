#include "bcos-framework/security/CloudKmsType.h"
#include "bcos-security/bcos-security/cloudkms/AwsKmsWrapper.h"
#include "bcos-utilities/FileUtility.h"
#include <aws/core/Aws.h>
#include <fstream>
#include <iostream>
#include <string>

using namespace bcos;
using namespace bcos::security;
void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName
              << " <kms_type> <access_key> <secret_key> <region> <key_id> <inputFilePath> "
                 "<outputFilePath>"
              << "\n";
}

int saveFile(const std::string& filePath, const std::shared_ptr<bytes>& content)
{
    std::ofstream outFile(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!outFile.is_open())
    {
        std::cerr << "Cannot open file for writing: " << filePath << "\n";
        return 1;
    }

    try
    {
        outFile.write(reinterpret_cast<const char*>(content->data()), content->size());
        if (outFile.fail())
        {
            std::cerr << "Failed to write content to file"
                      << "\n";
            return 1;
        }
        outFile.close();
    }
    catch (const std::exception& e)
    {
        outFile.close();
        std::cerr << "Error writing file: " << filePath << "\n" << e.what() << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc != 8)
    {
        printUsage(argv[0]);
        return 1;
    }

    const std::string kmsType = argv[1];
    const std::string accessKey = argv[2];
    const std::string secretKey = argv[3];
    const std::string region = argv[4];
    const std::string keyId = argv[5];
    const std::string inputFilePath = argv[6];
    const std::string outputFilePath = argv[7];

    auto provider = cloudKmsTypeFromString(kmsType);

    if (provider == CloudKmsType::UNKNOWN)
    {
        std::cerr << "Invalid KMS provider: " << kmsType << "\n";
        return 1;
    }

    if (provider == CloudKmsType::AWS)
    {
        // initialize the AWS SDK
        try
        {
            struct AwsSdkLifecycleManager
            {
                Aws::SDKOptions options;
                AwsSdkLifecycleManager() { Aws::InitAPI(options); }
                ~AwsSdkLifecycleManager() { Aws::ShutdownAPI(options); }
                AwsSdkLifecycleManager(const AwsSdkLifecycleManager&) = delete;
                AwsSdkLifecycleManager& operator=(const AwsSdkLifecycleManager&) = delete;
                AwsSdkLifecycleManager(AwsSdkLifecycleManager&&) = delete;
                AwsSdkLifecycleManager& operator=(AwsSdkLifecycleManager&&) = delete;
            } sdkLifecycleManager;
            // create an AWS KMS wrapper
            AwsKmsWrapper kmsWrapper(region, accessKey, secretKey, keyId);

            // encrypt data
            auto plaintext = readContents(inputFilePath);
            auto encryptResult = kmsWrapper.encryptContents(plaintext);
            if (encryptResult == nullptr)
            {
                std::cerr << "Encryption failed!"
                          << "\n";
                return 1;
            }

            std::cout << "Encrypted data : " << encryptResult->size() << " bytes"
                      << "\n";
            saveFile(outputFilePath, encryptResult);

            // decrypt data
            auto decryptResult = kmsWrapper.decryptContents(encryptResult);
            if (decryptResult == nullptr)
            {
                std::cerr << "Decryption failed!"
                          << "\n";
                return 1;
            }

            std::cout << "Decrypted text: " << decryptResult->size() << "\n";

            // text bytes to string
            std::string decryptResultStr(decryptResult->begin(), decryptResult->end());
            std::string expectedStr(plaintext->begin(), plaintext->end());
            std::cout << "Decrypted text: " << decryptResultStr << "\n";
            std::cout << "expectedStr text: " << expectedStr << "\n";
            if (decryptResultStr != expectedStr)
            {
                std::cerr << "Error:Decrypted data is not the same as the original data!"
                          << "\n";
                return 1;
            }
            std::cout << "Decrypted data is the same as the original data"
                      << "\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "Unsupported KMS provider: " << kmsType << "\n";
    return 1;
}