/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file SDFSM4Crypto.cpp
 * @author maggie
 * @date 2021-04-02
 */
#include "HSMCrypto.h"
#include "CryptoProvider.h"
#include "libdevcore/Common.h"
#include "sdf/SDFCryptoProvider.h"
#include <libdevcore/CommonData.h>

using namespace std;
using namespace dev;
using namespace crypto;

#if FISCO_SDF
using namespace hsm;
using namespace hsm::sdf;
#endif

std::string dev::crypto::SDFSM4Encrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t, const unsigned char* _ivData)
{
    // Add padding
    int padding = _plainDataSize % 16;
    int nSize = 16 - padding;
    int inDataVLen = _plainDataSize + nSize;
    bytes inDataV(inDataVLen);
    memcpyWithCheck(inDataV.data(), inDataV.size(), _plainData, _plainDataSize);
    memset(inDataV.data() + _plainDataSize, nSize, nSize);

    // Encrypt
    Key key = Key();
    std::shared_ptr<const std::vector<byte>> pbKeyValue =
        std::make_shared<const std::vector<byte>>(_key, _key + 16);
    key.setSymmetricKey(pbKeyValue);
    CryptoProvider& provider = SDFCryptoProvider::GetInstance();
    unsigned int size;
    // bytes enData(inDataVLen/16);
    string enData;
    enData.resize(inDataVLen);
    bytes iv(16);
    memcpyWithCheck(iv.data(), iv.size(), _ivData, 16);
    provider.Encrypt(key, SM4_CBC, (unsigned char*)iv.data(), (unsigned char*)inDataV.data(),
        inDataVLen, (unsigned char*)enData.data(), &size);
    return enData;
}
std::string dev::crypto::SDFSM4Decrypt(const unsigned char* _cypherData, size_t _cypherDataSize,
    const unsigned char* _key, size_t, const unsigned char* _ivData)
{
    string deData;
    deData.resize(_cypherDataSize);
    Key key = Key();
    std::shared_ptr<const std::vector<byte>> pbKeyValue =
        std::make_shared<const std::vector<byte>>(_key, _key + 16);
    key.setSymmetricKey(pbKeyValue);
    CryptoProvider& provider = SDFCryptoProvider::GetInstance();

    unsigned int size;
    bytes iv(16);
    memcpyWithCheck(iv.data(), iv.size(), _ivData, 16);
    provider.Decrypt(key, SM4_CBC, (unsigned char*)iv.data(), _cypherData, _cypherDataSize,
        (unsigned char*)deData.data(), &size);
    int padding = deData.at(_cypherDataSize - 1);
    int deLen = _cypherDataSize - padding;
    return deData.substr(0, deLen);
}
