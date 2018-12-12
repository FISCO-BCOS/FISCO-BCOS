/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file GmAES.cpp
 * @author Asherli
 * @date 2018
 */

#include "libdevcrypto/AES.h"
#include "sm4/sm4.h"
#include <libdevcore/easylog.h>
#include <openssl/sm4.h>
#include <string.h>

using namespace dev;
using namespace dev::crypto;
using namespace std;


char* ascii2hex(const char* chs, int len)
{
    char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    char* ascii = (char*)calloc(len * 3 + 1, sizeof(char));  // calloc ascii

    int i = 0;
    while (i < len)
    {
        int b = chs[i] & 0x000000ff;
        ascii[i * 2] = hex[b / 16];
        ascii[i * 2 + 1] = hex[b % 16];
        ++i;
    }
    return ascii;
}


bytes dev::aesCBCEncrypt(
    bytesConstRef plainData, string const& keyData, int keyLen, bytesConstRef ivData)
{
    // LOG(DEBUG)<<"GUOMI SM4 EN TYPE......................";
    int padding = plainData.size() % 16;
    int nSize = 16 - padding;
    int inDataVLen = plainData.size() + nSize;
    bytes inDataV(inDataVLen);
    memcpy(inDataV.data(), (unsigned char*)plainData.data(), plainData.size());
    memset(inDataV.data() + plainData.size(), nSize, nSize);

    bytes enData(inDataVLen);
    SM4::getInstance().setKey((unsigned char*)keyData.data(), keyData.size());
    SM4::getInstance().cbcEncrypt(
        inDataV.data(), enData.data(), inDataVLen, (unsigned char*)ivData.data(), 1);
    // LOG(DEBUG)<<"ivData:"<<ascii2hex((const char*)ivData.data(),ivData.size());
    return enData;
}

bytes dev::aesCBCDecrypt(
    bytesConstRef cipherData, string const& keyData, int keyLen, bytesConstRef ivData)
{
    // LOG(DEBUG)<<"GM SM4 DE TYPE....................";
    bytes deData(cipherData.size());
    SM4::getInstance().setKey((unsigned char*)keyData.data(), keyData.size());
    SM4::getInstance().cbcEncrypt((unsigned char*)cipherData.data(), deData.data(),
        cipherData.size(), (unsigned char*)ivData.data(), 0);
    int padding = deData.data()[cipherData.size() - 1];
    int deLen = cipherData.size() - padding;
    deData.resize(deLen);
    return deData;
}