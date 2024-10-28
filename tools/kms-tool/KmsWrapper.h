// kms_wrapper.h
#pragma once
#include "Utils.h"
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class KMSWrapper
{
public:
    // explicit KMSWrapper(
    //     const std::string& region, const std::string& accessKey, const std::string& secretKey);
    // ~KMSWrapper() = default;

    // encrypt data，return pair<isSuccess, encrypted data>
    virtual std::pair<bool, std::vector<unsigned char>> encrypt(
        const std::string& keyId, const std::string& plaintext) = 0;

    // decrypt data，return pair<isSuccess, decrypted text>
    virtual std::pair<bool, std::string> decrypt(const std::vector<unsigned char>& ciphertext) = 0;
    inline std::string toHex(std::vector<unsigned char>& data)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (const auto& byte : data)
        {
            ss << std::setw(2) << static_cast<int>(byte);
        }

        return ss.str();
    }

    inline std::vector<unsigned char> fromHex(std::string& hex)
    {
        if (hex.length() % 2 != 0)
        {
            throw std::invalid_argument("Hex string length must be even");
        }

        // preallocate memory
        std::vector<unsigned char> bytes;
        bytes.reserve(hex.length() / 2);

        // check if the string is a valid hex string
        for (size_t i = 0; i < hex.length(); i += 2)
        {
            // check if the character is a valid hex character
            if (!std::isxdigit(hex[i]) || !std::isxdigit(hex[i + 1]))
            {
                throw std::invalid_argument(
                    "Invalid hex character at position " + std::to_string(i));
            }

            // use sscanf to convert the hex characters to a byte
            unsigned int byte;
            std::sscanf(hex.c_str() + i, "%2x", &byte);
            bytes.push_back(static_cast<unsigned char>(byte));
        }

        return bytes;
    }
};