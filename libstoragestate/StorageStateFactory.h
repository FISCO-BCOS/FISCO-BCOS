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
 * @brief : StorageStateFactory
 * @author: xingqiangbai
 * @date: 2018-10-26
 */


#pragma once

#include <libexecutive/StateFactoryInterface.h>

namespace dev
{
namespace storagestate
{
class StorageStateFactory : public dev::executive::StateFactoryInterface
{
public:
    StorageStateFactory(u256 const& _accountStartNonce) : m_accountStartNonce(_accountStartNonce) {}
    virtual ~StorageStateFactory() {}
    std::shared_ptr<dev::executive::StateFace> getState(
        h256 const& _root, std::shared_ptr<dev::storage::TableFactory> _factory) override;
    void enableBinaryEncode(bool _enableBinaryEncode) { m_enableBinary = _enableBinaryEncode; }

private:
    u256 m_accountStartNonce;
    bool m_enableBinary = false;
};
}  // namespace storagestate
}  // namespace dev
