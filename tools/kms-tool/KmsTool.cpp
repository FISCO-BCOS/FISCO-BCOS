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
              << " <mode: encrypt|decrypt> <kms_type> <access_key> <secret_key> <region> <key_id> <inputFilePath> <outputFilePath>\n"
              << "Example: " << programName << " encrypt/decrypt AWS <access_key> <secret_key> <region> <key_id> input.txt output.enc\n";
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
    std::string mode;
    std::string kmsType;
    std::string accessKey;
    std::string secretKey;
    std::string region;
    std::string keyId;
    std::string inputFilePath;
    std::string outputFilePath;

    // 新增模式选择
    std::cout << "Enter mode (encrypt/decrypt): ";
    std::cin >> mode;

    std::cout << "Enter KMS type: ";
    std::cin >> kmsType;

    std::cout << "Enter access key: ";
    std::cin >> accessKey;

    std::cout << "Enter secret key: ";
    std::cin >> secretKey;

    std::cout << "Enter region: ";
    std::cin >> region;

    std::cout << "Enter key ID: ";
    std::cin >> keyId;

    std::cout << "Enter input file path: ";
    std::cin >> inputFilePath;

    std::cout << "Enter output file path: ";
    std::cin >> outputFilePath;

    auto prividerOption =
        magic_enum::enum_cast<CloudKmsType>(kmsType, magic_enum::case_insensitive);
    if (!prividerOption.has_value())
    {
        std::cerr << "Invalid KMS provider: " << kmsType << "\n";
        return 1;
    }
    auto provider = prividerOption.value();

    if (provider == CloudKmsType::AWS)
    {
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

            AwsKmsWrapper kmsWrapper(region, accessKey, secretKey, keyId);

            if (mode == "encrypt")
            {
                auto plaintext = readContents(inputFilePath);
                auto encryptResult = kmsWrapper.encryptContents(plaintext);
                if (encryptResult == nullptr)
                {
                    std::cerr << "Encryption failed!" << "\n";
                    return 1;
                }
                std::cout << "Encrypted data : " << encryptResult->size() << " bytes" << "\n";
                saveFile(outputFilePath, encryptResult);
            }
            else if (mode == "decrypt")
            {
                auto ciphertext = readContents(inputFilePath);
                auto decryptResult = kmsWrapper.decryptContents(ciphertext);
                if (decryptResult == nullptr)
                {
                    std::cerr << "Decryption failed!" << "\n";
                    return 1;
                }
                std::cout << "Decrypted data : " << decryptResult->size() << " bytes" << "\n";
                saveFile(outputFilePath, decryptResult);
                // 可选：输出明文内容
                std::string decryptResultStr(decryptResult->begin(), decryptResult->end());
                std::cout << "Decrypted text: " << decryptResultStr << "\n";
            }
            else
            {
                std::cerr << "Invalid mode: " << mode << ". Use 'encrypt' or 'decrypt'.\n";
                return 1;
            }
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