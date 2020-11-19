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

/**
 * @brief : MPTStateFactory
 * @author: mingzhenliu
 * @date: 2018-09-21
 */


#pragma once

#include "MPTState.h"
#include <libexecutive/StateFactoryInterface.h>
#include <libmptstate/OverlayDB.h>
#include <libstorage/Storage.h>
#include <boost/filesystem.hpp>

namespace bcos
{
namespace mptstate
{
class MPTStateFactory : public bcos::executive::StateFactoryInterface
{
public:
    MPTStateFactory(u256 const& _accountStartNonce, boost::filesystem::path const& _basePath,
        h256 const& _genesisHash, WithExisting _we)
      : m_accountStartNonce(_accountStartNonce), m_basePath(_basePath), m_genesisHash(_genesisHash)
    {
        m_db = MPTState::openDB(m_basePath, m_genesisHash, _we);
    };
    virtual ~MPTStateFactory(){};
    virtual std::shared_ptr<bcos::executive::StateFace> getState(
        h256 const& _root, std::shared_ptr<bcos::storage::TableFactory> _factory) override;

private:
    bcos::OverlayDB m_db;
    u256 m_accountStartNonce;
    boost::filesystem::path m_basePath;
    h256 m_genesisHash;
};

}  // namespace mptstate

}  // namespace bcos
