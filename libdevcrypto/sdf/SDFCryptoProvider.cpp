#include "SDFCryptoProvider.h"
#include "swsds.h"
#include <iostream>
#include <cstring>

using namespace std;

namespace dev
{
    namespace crypto
    {

        SDFCryptoProvider::SDFCryptoProvider()
        {
            cout << "open device" << endl;
            SGD_RV deviceStatus = SDF_OpenDevice(&m_deviceHandle);
            if (deviceStatus != SDR_OK)
            {
                cout << "failed open device" << GetErrorMessage(deviceStatus) << endl;
                throw deviceStatus;
            }
            cout << "open session" << endl;
            SGD_RV sessionStatus = SDF_OpenSession(m_deviceHandle, &m_sessionHandle);
            if (sessionStatus != SDR_OK)
            {
                cout << "failed open session" << GetErrorMessage(sessionStatus) << endl;
                throw sessionStatus;
            }
            cout << "finish hsm initialization." << endl;
        }

        SDFCryptoProvider::~SDFCryptoProvider()
        {
            if (m_sessionHandle != NULL)
            {
                SDF_CloseSession(m_sessionHandle);
            }
            if (m_deviceHandle != NULL)
            {
                SDF_CloseDevice(m_deviceHandle);
            }
        }

        SDFCryptoProvider &SDFCryptoProvider::GetInstance()
        {
            static SDFCryptoProvider instance;
            return instance;
        }

        unsigned int SDFCryptoProvider::Sign(Key const &key, AlgorithmType algorithm, unsigned char const *digest, unsigned int const digestLen, unsigned char *signature, unsigned int *signatureLen)
        {

            switch (algorithm)
            {
            case SM2:
            {
                ECCrefPrivateKey eccKey = {bits : 32 * 8};
                strncpy((char *)eccKey.D, (const char *)key.PrivateKey(), 32);
                unsigned char tmpData[512];
                memset(tmpData, 0, sizeof(tmpData));
                SGD_RV signCode = SDF_ExternalSign_ECC(m_sessionHandle, SGD_SM2_1, &eccKey, (SGD_UCHAR *)digest, digestLen, (ECCSignature *)tmpData);
                if (signCode != SDR_OK)
                {
                    return signCode;
                }
                PrintData("Signature r ", ((ECCSignature *)tmpData)->r, 32, 16);
                PrintData("Signature s ", ((ECCSignature *)tmpData)->s, 32, 16);
                strncpy((char *)signature, (const char *)tmpData, 64);
                *signatureLen = 512;
                return SDR_OK;
            }
            default:
                return SDR_ALGNOTSUPPORT;
            }
        }

        unsigned int SDFCryptoProvider::KeyGen(AlgorithmType algorithm, Key *key)
        {
            switch (algorithm)
            {
            case SM2:
            {
                // key
                ECCrefPublicKey pk;
                ECCrefPrivateKey sk;
                SGD_UINT32 keyLen = 256;

                // call generate key
                cout << "Generate key pair " << endl;

                SGD_RV result = SDF_GenerateKeyPair_ECC(m_sessionHandle, SGD_SM2_3, keyLen, &pk, &sk);
                cout << "generate key pair result:" << GetErrorMessage(result) << endl;
                std::basic_string<unsigned char> pk_x = pk.x;
                std::basic_string<unsigned char> pk_y = pk.y;
                std::basic_string<unsigned char> pk_xy = pk_x + pk_y;
                key->setPrivateKey(sk.D, sk.bits / 8);
                key->setPublicKey((unsigned char *)pk_xy.c_str(), pk.bits / 4);

                int pukLen, prkLen;
                pukLen = sizeof(ECCrefPublicKey);
                prkLen = sizeof(ECCrefPrivateKey);
                cout << "Public key .bits: " << pk.bits << ";  private key .bit:" << sk.bits << endl;
                PrintData("PUBLICKEY", (unsigned char *)&pk, pukLen, 16);
                PrintData("PRIVATEKEY", (unsigned char *)&sk, prkLen, 16);
                printf("\n");
                PrintData("key.PublicKey()", key->PublicKey(), pk.bits / 4, 16);
                PrintData("key.PrivateKey()", key->PrivateKey(), sk.bits / 8, 16);
                return SDR_OK;
            }
            default:
                return SDR_ALGNOTSUPPORT;
            }
        }

        unsigned int SDFCryptoProvider::Hash(char const *message, AlgorithmType algorithm, unsigned int const messageLen,
                                             unsigned char *digest, unsigned int *digestLen)
        {
            switch (algorithm)
            {
            case SM3:
            {
                SGD_RV rv;
                rv = SDF_HashInit(m_sessionHandle, SGD_SHA256, NULL, NULL, 0);
                if (rv != SDR_OK)
                {
                    cout << "Hash init error: " << rv;
                    return rv;
                }

                rv = SDF_HashUpdate(m_sessionHandle, (SGD_UCHAR *)message, messageLen);
                if (rv != SDR_OK)
                {
                    cout << "Hash update error: " << rv;
                    return rv;
                }

                rv = SDF_HashFinal(m_sessionHandle, (SGD_UCHAR *)digest, digestLen);
                if (rv != SDR_OK)
                {
                    cout << "Hash init error: " << rv;
                    return rv;
                }
                PrintData("Hash", digest, (unsigned int)*digestLen, 16);
                break;
            }
            default:
                return SDR_ALGNOTSUPPORT;
            }
        }

        unsigned int SDFCryptoProvider::PrintDeviceInfo()
        {
            DEVICEINFO stDeviceInfo;
            SGD_RV rv = SDF_GetDeviceInfo(m_sessionHandle, &stDeviceInfo);
            cout << "get device info result:" << GetErrorMessage(rv) << " device name:" << stDeviceInfo.DeviceName << endl;
            return 0;
        }
        unsigned int SDFCryptoProvider::Verify(Key const &key, AlgorithmType algorithm, unsigned char const *digest,
                                unsigned int const digestLen, unsigned char const *signature, unsigned int *signatureLen,
                                bool *result){
            switch (algorithm)
            {
            case SM2:{
                ECCrefPublicKey eccKey = {bits : 32 * 8};
                strncpy((char *)eccKey.x, (const char *)key.PublicKey(), 32);
                strncpy((char *)eccKey.y, (const char *)key.PublicKey()+32, 32);
                ECCSignature eccSignature;
                strncpy((char *)eccSignature.r, (const char *)signature, 32);
                strncpy((char *)eccSignature.s, (const char *)signature+32, 32);
                SGD_RV code = SDF_ExternalVerify_ECC(m_sessionHandle,SGD_SM2_1,&eccKey, (SGD_UCHAR *)digest, digestLen, &eccSignature);
                if (code == SDR_OK){
                    *result = true;
                }else{
                    *result = false;
                }
                return code;
            }
            default:
                break;
            }
        };

        std::string SDFCryptoProvider::GetErrorMessage(SGD_RV code)
        {
            switch (code)
            {
            case SDR_OK:
                return "success";
            case SDR_UNKNOWERR:
                return "unknown error";
            case SDR_NOTSUPPORT:
                return "not support";
            case SDR_COMMFAIL:
                return "communication failed";
            case SDR_OPENDEVICE:
                return "failed open device";
            case SDR_OPENSESSION:
                return "failed open session";
            case SDR_PARDENY:
                return "permission deny";
            case SDR_KEYNOTEXIST:
                return "key not exit";
            case SDR_ALGNOTSUPPORT:
                return "algorithm not support";
            case SDR_ALGMODNOTSUPPORT:
                return "algorithm not support mode";
            case SDR_PKOPERR:
                return "public key calculate error";
            case SDR_SKOPERR:
                return "private key calculate error";
            case SDR_SIGNERR:
                return "signature error";
            case SDR_VERIFYERR:
                return "verify signature error";
            case SDR_SYMOPERR:
                return "symmetric crypto calculate error";
            case SDR_STEPERR:
                return "step error";
            case SDR_FILESIZEERR:
                return "file size error";
            case SDR_FILENOEXIST:
                return "file not exist";
            case SDR_FILEOFSERR:
                return "file offset error";
            case SDR_KEYTYPEERR:
                return "key type not right";
            case SDR_KEYERR:
                return "key error";
            default:
                return "unkown";
            }
        }
        inline int PrintData(char *itemName, unsigned char *sourceData, unsigned int dataLength, unsigned int rowCount)
        {
            int i, j;

            if ((sourceData == NULL) || (rowCount == 0) || (dataLength == 0))
                return -1;

            if (itemName != NULL)
                printf("%s[%d]:\n", itemName, dataLength);

            for (i = 0; i < (int)(dataLength / rowCount); i++)
            {
                printf("%08x  ", i * rowCount);

                for (j = 0; j < (int)rowCount; j++)
                {
                    printf("%02x ", *(sourceData + i * rowCount + j));
                }

                printf("\n");
            }

            if (!(dataLength % rowCount))
                return 0;

            printf("%08x  ", (dataLength / rowCount) * rowCount);

            for (j = 0; j < (int)(dataLength % rowCount); j++)
            {
                printf("%02x ", *(sourceData + (dataLength / rowCount) * rowCount + j));
            }

            printf("\n");

            return 0;
        }

    } // namespace crypto
} // namespace dev
