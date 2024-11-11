#include "bcos-security/bcos-security/cloudkms/AwsKmsWrapper.h"
#include "bcos-security/bcos-security/cloudkms/CloudKmsProvider.h"
#include <aws/core/Aws.h>
#include <iostream>
#include <string>
#include <fstream>

using namespace bcos;
using namespace bcos::security;
void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName
              << " <kms_type> <access_key> <secret_key> <region> <key_id> <inputFilePath> <outputFilePath>" << std::endl;
}

int saveFile(const std::string& filePath, const std::shared_ptr<bytes>& content)
{
    std::ofstream outFile(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!outFile.is_open())
    {
        std::cerr << "Cannot open file for writing: " << filePath << std::endl;
        return 1;
    }

    try
    {
        outFile.write(reinterpret_cast<const char*>(content->data()), content->size());
        if (outFile.fail())
        {
            std::cerr << "Failed to write content to file" << std::endl;
            return 1;
        }
        outFile.close();
    }
    catch (const std::exception& e)
    {
        outFile.close();
        std::cerr << "Error writing file: " << filePath << "\n" << e.what() << std::endl;
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

    auto provider = CloudKmsProviderHelper::FromString(kmsType);

    if (provider == CloudKmsProvider::UNKNOWN)
    {
        std::cerr << "Invalid KMS provider: " << kmsType << std::endl;
        return 1;
    }

    if (provider == CloudKmsProvider::AWS)
    {
        // initialize the AWS SDK
        Aws::SDKOptions options;
        Aws::InitAPI(options);
        {
            try
            {
                // create an AWS KMS wrapper
                AWSKMSWrapper kmsWrapper(region, accessKey, secretKey);

                // encrypt data
                auto encryptResult = kmsWrapper.encrypt(keyId, inputFilePath);
                if (encryptResult == nullptr)
                {
                    std::cerr << "Encryption failed!" << std::endl;
                    return 1;
                }

                std::cout << "Encrypted data : " << encryptResult->size()
                          << " bytes" << std::endl;
                saveFile(outputFilePath, encryptResult);

                // decrypt data
                auto decryptResult = kmsWrapper.decrypt(encryptResult);
                if (decryptResult == nullptr)
                {
                    std::cerr << "Decryption failed!" << std::endl;
                    return 1;
                }

                std::cout << "Decrypted text: " << decryptResult->size() << std::endl;

                // text bytes to string
                std::string decryptResultStr(decryptResult->begin(), decryptResult->end());
                std::cout << "Decrypted text: " << decryptResultStr << std::endl;

                // // read encrypted data from file
                // std::string cipherFile = readFile(outputFilePath);
                // std::vector<unsigned char> cipherData = kmsWrapper.fromHex(cipherFile);
                // std::cout << "test hex " << kmsWrapper.toHex(cipherData) << std::endl;
                // std::cout << "cipherData size " << cipherData.size() << std::endl;

                // auto decryptFileResult = kmsWrapper.decrypt(cipherData);
                // if (!decryptFileResult.first)
                // {
                //     std::cerr << "Decryption failed!" << std::endl;
                //     return 1;
                // }

                // std::cout << "Decrypted text from file: " << decryptFileResult.second << std::endl;

                // auto decryptRawResult = kmsWrapper.decryptRaw(std::make_shared<bytes>(cipherData));
                // if (decryptRawResult == nullptr)
                // {
                //     std::cerr << "Decryption failed!" << std::endl;
                //     return 1;
                // }
                // std::string decryptRawResultStr(decryptRawResult->begin(), decryptRawResult->end());
                // std::cout << "Decrypted text from raw data: " << decryptRawResultStr << std::endl;

            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
        }
        // clean up the AWS SDK
        Aws::ShutdownAPI(options);
        return 0;
    }
    else
    {
        std::cerr << "Unsupported KMS provider: " << kmsType << std::endl;
        return 1;
    }
}