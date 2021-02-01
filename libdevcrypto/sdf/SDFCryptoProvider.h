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

#include <string>

namespace dev
{
namespace crypto
{
/**
 *  SDFCryptoProvider suply SDF function calls
 *  Singleton
 */
class SDFCryptoProvider
{
private:
    static SDFCryptoProvider _instance;

    /**
     * Get session from session pool
     */
    static void getSession(void* handle, unsigned int* code);

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

    class Key;
    /**
     * Return the instance
     */
    static SDFCryptoProvider& GetInstance();

    /**
     * Generate key
     * Return error code
     */
    unsigned int KeyGen(std::string algorithm, Key* key);

    /**
     * Sign
     */
    unsigned int Sign(Key const* privateKey, std::string algorithm, unsigned char const* digest,
        int digestLen, unsigned char* signature, int* signatureLen);

    /**
     * Verify signature
     */
    unsigned int Verify(Key const* publicKey, std::string algorithm, unsigned char const* digest,
        int digestLen, unsigned char const* signature, int* signatureLen, bool result);

    /**
     * Make hash
     */
    unsigned int Hash(Key const* key, char const* message, std::string algorithm,
        int const messageLen, char* digest, int* digestLen);

    /**
     * Encrypt
     */
    unsigned int Encrypt(Key const* publicKey, std::string algorithm, unsigned char const* plantext,
        int const plantextLen, unsigned char* cyphertext, int* cyphertextLen);

    /**
     * Decrypt
     */
    unsigned int Decrypt(Key const* privateKey, std::string algorithm,
        unsigned char const* cyphertext, int const cyphertextLen, unsigned char* plantext,
        int* plantextLen);
};
}  // namespace crypto
}  // namespace dev