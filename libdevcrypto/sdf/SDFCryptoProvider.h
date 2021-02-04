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
/** @file SDFCryptoProvider.h
 * @author maggiewu
 * @date 2021-02-01
 */

#include <libdevcrypto/Common.h>
#include <string>
namespace dev
{
namespace crypto
{
enum AlgorithmType : uint32_t
{
    SM2 = 0x00020100,  // SGD_SM2_1
    SM3 = 0x00000001,  // SGD_SM3
    SM4 = 0x00002002,  // SGD_SM4_CBC
};

class SDFKey : KeyPair
{
public:
    Key(unsigned int keyIndex, std::string& password)
    {
        m_keyIndex = keyIndex;
        m_keyPassword = password;
    };
    unsigned int Identifier() { return m_keyIndex; };
    std::string Password() { return m_keyPassword; };

private:
    unsigned int m_keyIndex;
    std::string m_keyPassword;
};

/**
 *  SDFCryptoProvider suply SDF function calls
 *  Singleton
 */
class SDFCryptoProvider
{
private:
    /**
     * Get session from session pool
     */
    void getSession(void** handle, unsigned int* code);

public:
    /**
     * Detect all crypto devices
     * Open each device, and start a session
     */
    SDFCryptoProvider(/* args */);
    /**
     * Close sessions and close devices.
     */
    ~SDFCryptoProvider();
    /**
     * Return the instance
     */
    static SDFCryptoProvider& GetInstance();

    /**
     * Generate key
     * Return error code
     */
    unsigned int KeyGen(AlgorithmType algorithm, Key* key);

    /**
     * Sign
     */
    unsigned int Sign(Key const& key, AlgorithmType algorithm, unsigned char const* digest,
        unsigned int const digestLen, unsigned char* signature, unsigned int* signatureLen);

    /**
     * Verify signature
     */
    unsigned int Verify(Key const& key, AlgorithmType algorithm, unsigned char const* digest,
        unsigned int const digestLen, unsigned char const* signature, unsigned int* signatureLen,
        bool result);

    /**
     * Make hash
     */
    unsigned int Hash(char const* message, AlgorithmType algorithm, unsigned int const messageLen,
        char* digest, unsigned int* digestLen);

    /**
     * Encrypt
     */
    unsigned int Encrypt(Key const& key, AlgorithmType algorithm, unsigned char const* plantext,
        unsigned int const plantextLen, unsigned char* cyphertext, unsigned int* cyphertextLen);

    /**
     * Decrypt
     */
    unsigned int Decrypt(Key const& key, AlgorithmType algorithm, unsigned char const* cyphertext,
        unsigned int const cyphertextLen, unsigned char* plantext, unsigned int* plantextLen);
};
}  // namespace crypto
}  // namespace dev