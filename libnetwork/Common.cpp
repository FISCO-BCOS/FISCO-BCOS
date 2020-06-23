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
 * (c) 2016-2020 fisco-dev contributors.
 */
/** @file Common.cpp
 * @author yujiechen
 * @date: 2020-06-16
 */
#include "Common.h"

bool dev::network::getPublicKeyFromCert(std::shared_ptr<std::string> _nodeIDOut, X509* cert)
{
    EVP_PKEY* evpPublicKey = X509_get_pubkey(cert);
    if (!evpPublicKey)
    {
        HOST_LOG(ERROR) << LOG_DESC("Get evpPublicKey failed");
        return false;
    }

    ec_key_st* ecPublicKey = EVP_PKEY_get1_EC_KEY(evpPublicKey);
    if (!ecPublicKey)
    {
        HOST_LOG(ERROR) << LOG_DESC("Get ecPublicKey failed");
        return false;
    }
    /// get public key of the certificate
    const EC_POINT* ecPoint = EC_KEY_get0_public_key(ecPublicKey);
    if (!ecPoint)
    {
        HOST_LOG(ERROR) << LOG_DESC("Get ecPoint failed");
        return false;
    }

    std::shared_ptr<char> hex =
        std::shared_ptr<char>(EC_POINT_point2hex(EC_KEY_get0_group(ecPublicKey), ecPoint,
                                  EC_KEY_get_conv_form(ecPublicKey), NULL),
            [](char* p) { OPENSSL_free(p); });

    if (hex)
    {
        _nodeIDOut->assign(hex.get());
        if (_nodeIDOut->find("04") == 0)
        {
            /// remove 04
            _nodeIDOut->erase(0, 2);
        }
    }
    return true;
}

std::ostream& dev::network::operator<<(std::ostream& _out, NodeIPEndpoint const& _endpoint)
{
    _out << _endpoint.address() << ":" << _endpoint.port();
    return _out;
}
