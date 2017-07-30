#pragma once

#include<iostream>

using namespace std;

namespace dev {
    namespace crypto {
        std::string RSAKeyVerify(const std::string& pubStr, const std::string& strData);
        std::string RSAKeySign(const std::string& strPemFileName, const std::string& strData);
    }
}
