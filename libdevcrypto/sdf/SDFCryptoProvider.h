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
    unsigned int Sign(Key const* key, std::string algorithm, unsigned char const* digest,
        unsigned int const digestLen, unsigned char* signature, unsigned int* signatureLen);

    /**
     * Verify signature
     */
    unsigned int Verify(Key const* key, std::string algorithm, unsigned char const* digest,
        unsigned int const digestLen, unsigned char const* signature, unsigned int* signatureLen,
        bool result);

    /**
     * Make hash
     */
    unsigned int Hash(char const* message, std::string algorithm, unsigned int const messageLen,
        char* digest, unsigned int* digestLen);

    /**
     * Encrypt
     */
    unsigned int Encrypt(Key const* key, std::string algorithm, unsigned char const* plantext,
        unsigned int const plantextLen, unsigned char* cyphertext, unsigned int* cyphertextLen);

    /**
     * Decrypt
     */
    unsigned int Decrypt(Key const* key, std::string algorithm, unsigned char const* cyphertext,
        unsigned int const cyphertextLen, unsigned char* plantext, unsigned int* plantextLen);
};
}  // namespace crypto
}  // namespace dev