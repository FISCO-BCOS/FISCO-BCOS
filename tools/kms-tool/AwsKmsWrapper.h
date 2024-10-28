// kms_wrapper.h
#pragma once
#include "KmsWrapper.h"
#include <aws/kms/KMSClient.h>
#include <string>
#include <utility>
#include <vector>

class AWSKMSWrapper : public KMSWrapper
{
public:
    explicit AWSKMSWrapper(
        const std::string& region, const std::string& accessKey, const std::string& secretKey);
    ~AWSKMSWrapper() = default;

    // encrypt data, return pair<success, encrypted data>
    std::pair<bool, std::vector<unsigned char>> encrypt(
        const std::string& keyId, const std::string& plaintext) override;

    // decrypt data, return pair<success, decrypted text>
    std::pair<bool, std::string> decrypt(const std::vector<unsigned char>& ciphertext) override;

private:
    std::shared_ptr<Aws::KMS::KMSClient> m_kmsClient;
};