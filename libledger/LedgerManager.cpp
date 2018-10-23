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
 * @brief : implementation of Ledger manager
 * @file: LedgerManager.cpp
 * @author: yujiechen
 * @date: 2018-10-23
 */
#include "LedgerManager.h"

namespace dev
{
namespace ledger
{
/// TODO:
bool LedgerManager::initAllLedgers(
    std::vector<std::shared_ptr<dev::initializer::LedgerParamInterface>> allLedgerParams)
{
    return true;
}
/// TODO:
bool LedgerManager::initSignleLedger(
    std::shared_ptr<dev::initializer::LedgerParamInterface> ledgerParam)
{
    return true;
}

}  // namespace ledger
}  // namespace dev