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

#include "StorageStateFactory.h"
#include "StorageState.h"

using namespace std;
using namespace dev;
using namespace dev::storagestate;
using namespace dev::executive;
std::shared_ptr<StateFace> StorageStateFactory::getState(
    h256 const&, std::shared_ptr<dev::storage::TableFactory> _factory)
{
    auto storageState = make_shared<StorageState>(m_accountStartNonce);
    storageState->setMemoryTableFactory(_factory);
    return storageState;
}
