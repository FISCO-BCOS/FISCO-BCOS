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
/** @file ECDHE.cpp
 * @author Alex Leverington <nessence@gmail.com> Asherli
 * @date 2018
 */

#include "ECDHE.h"
#include "CryptoPP.h"
#include "libdevcrypto/Hash.h"

using namespace std;
using namespace dev;
using namespace dev::crypto;
bool ECDHE::agree(Public const& _remote, Secret& o_sharedSecret) const
{
    if (m_remoteEphemeral)
        // agreement can only occur once
        BOOST_THROW_EXCEPTION(InvalidState());

    m_remoteEphemeral = _remote;
    return Secp256k1PP::get()->agree(m_ephemeral.secret(), m_remoteEphemeral, o_sharedSecret);
}
