#pragma once
#include <algorithm>
#include <array>
#include <string>

enum class CloudKMSProvider
{
    AWS,      // Amazon Web Services KMS
    AZURE,    // Microsoft Azure Key Vault
    HUAWEI,   // Huawei Cloud KMS
    ALIYUN,   // Alibaba Cloud KMS
    TENCENT,  // Tencent Cloud KMS
    GCP,      // Google Cloud KMS
    BAIDU     // Baidu Cloud KMS
};

class CloudKMSProviderHelper
{
public:
    // check if the provider name is valid
    static bool isValidProvider(const std::string& provider)
    {
        // convert the provider name to uppercase
        std::string upperProvider = toUpper(provider);

        // define a list of valid provider names
        static const std::array<std::string, 7> validProviders = {
            "AWS", "AZURE", "HUAWEI", "ALIYUN", "TENCENT", "GCP", "BAIDU"};

        return std::any_of(validProviders.begin(), validProviders.end(),
            [&upperProvider](const std::string& valid) { return valid == upperProvider; });
    }

    // convert the provider name to the corresponding enum value
    static std::pair<bool, CloudKMSProvider> stringToProvider(const std::string& provider)
    {
        std::string upperProvider = toUpper(provider);

        if (upperProvider == "AWS")
            return {true, CloudKMSProvider::AWS};
        if (upperProvider == "AZURE")
            return {true, CloudKMSProvider::AZURE};
        if (upperProvider == "HUAWEI")
            return {true, CloudKMSProvider::HUAWEI};
        if (upperProvider == "ALIYUN")
            return {true, CloudKMSProvider::ALIYUN};
        if (upperProvider == "TENCENT")
            return {true, CloudKMSProvider::TENCENT};
        if (upperProvider == "GCP")
            return {true, CloudKMSProvider::GCP};
        if (upperProvider == "BAIDU")
            return {true, CloudKMSProvider::BAIDU};

        return {false, CloudKMSProvider::AWS};  // 默认返回值，第一个bool表示是否转换成功
    }

    // convert the provider enum value to the corresponding string
    static std::string providerToString(CloudKMSProvider provider)
    {
        switch (provider)
        {
        case CloudKMSProvider::AWS:
            return "AWS";
        case CloudKMSProvider::AZURE:
            return "Azure";
        case CloudKMSProvider::HUAWEI:
            return "Huawei";
        case CloudKMSProvider::ALIYUN:
            return "Aliyun";
        case CloudKMSProvider::TENCENT:
            return "Tencent";
        case CloudKMSProvider::GCP:
            return "GCP";
        case CloudKMSProvider::BAIDU:
            return "Baidu";
        default:
            return "Unknown";
        }
    }

private:
    // convert a string to uppercase
    static std::string toUpper(std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }
};