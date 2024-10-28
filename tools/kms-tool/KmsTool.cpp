#include "AwsKmsWrapper.h"
#include "KmsProvider.h"
#include <aws/core/Aws.h>
#include <iostream>
#include <string>

void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName
              << " <kms_type> <access_key> <secret_key> <region> <key_id> <inputFilePath> <outputFilePath>" << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc != 8)
    {
        printUsage(argv[0]);
        return 1;
    }
    // AWS凭证信息

    const std::string kmsType = argv[1];
    const std::string accessKey = argv[2];
    const std::string secretKey = argv[3];
    const std::string region = argv[4];
    const std::string keyId = argv[5];
    const std::string inputFilePath = argv[6];
    const std::string outputFilePath = argv[7];

    auto provider = CloudKMSProviderHelper::stringToProvider(kmsType);

    if (!provider.first)
    {
        std::cerr << "Invalid KMS provider: " << kmsType << std::endl;
        return 1;
    }

    if (provider.second == CloudKMSProvider::AWS)
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
                if (!encryptResult.first)
                {
                    std::cerr << "Encryption failed!" << std::endl;
                    return 1;
                }

                std::cout << "Encrypted data : " << kmsWrapper.toHex(encryptResult.second)
                          << " bytes" << std::endl;
                saveFile(outputFilePath, kmsWrapper.toHex(encryptResult.second));

                std::cout << "Encrypted data size: " << encryptResult.second.size() << " bytes"
                          << std::endl;

                // decrypt data
                auto decryptResult = kmsWrapper.decrypt(encryptResult.second);
                if (!decryptResult.first)
                {
                    std::cerr << "Decryption failed!" << std::endl;
                    return 1;
                }

                std::cout << "Decrypted text: " << decryptResult.second << std::endl;

                // read encrypted data from file
                std::string cipherFile = readFile(outputFilePath);
                std::vector<unsigned char> cipherData = kmsWrapper.fromHex(cipherFile);
                std::cout << "test hex " << kmsWrapper.toHex(cipherData) << std::endl;
                std::cout << "cipherData size " << cipherData.size() << std::endl;

                auto decryptFileResult = kmsWrapper.decrypt(cipherData);
                if (!decryptFileResult.first)
                {
                    std::cerr << "Decryption failed!" << std::endl;
                    return 1;
                }

                std::cout << "Decrypted text from file: " << decryptFileResult.second << std::endl;

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


    // std::string accessKey;
    // std::string secretKey;
    // std::string region;
    // std::string keyId;

    // // 从用户输入获取凭证信息
    // std::cout << "Enter AWS Access Key: ";
    // std::getline(std::cin, accessKey);

    // std::cout << "Enter AWS Secret Key: ";
    // std::getline(std::cin, secretKey);

    // std::cout << "Enter AWS Region (e.g., us-west-2): ";
    // std::getline(std::cin, region);

    // std::cout << "Enter KMS Key ID: ";
    // std::getline(std::cin, keyId);
}