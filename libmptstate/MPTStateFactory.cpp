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


#include "MPTStateFactory.h"
using namespace dev;
using namespace dev::mptstate;
using namespace dev::eth;
using namespace dev::executive;

std::shared_ptr<StateFace> MPTStateFactory::getState(
    h256 const& _root, std::shared_ptr<dev::storage::TableFactory>)
{
    if (_root == dev::h256())
    {
        auto mptState = std::make_shared<MPTState>(m_accountStartNonce, m_db, BaseState::Empty);

        return mptState;
    }
    else
    {
        auto mptState =
            std::make_shared<MPTState>(m_accountStartNonce, m_db, BaseState::PreExisting);
        mptState->setRoot(_root);

        return mptState;
    }
}
