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
/** @file MemoryTableFactoryFactory.h
 *  @author ancelmo
 *  @date 20190328
 */

#pragma once

#include "MemoryTableFactory.h"
#include "Storage.h"
#include "Table.h"
#include <libutilities/FixedHash.h>

namespace bcos
{
namespace storage
{
class MemoryTableFactoryFactory : public TableFactoryFactory
{
public:
    TableFactory::Ptr newTableFactory(const bcos::h256& hash, int64_t number) override
    {
        MemoryTableFactory::Ptr tableFactory = std::make_shared<MemoryTableFactory>();
        tableFactory->setStateStorage(m_stroage);
        tableFactory->setBlockHash(hash);
        tableFactory->setBlockNum(number);
        // TODO: check if need handle exception
        tableFactory->init();

        return tableFactory;
    }

    void setStorage(bcos::storage::Storage::Ptr storage) { m_stroage = storage; }

private:
    bcos::storage::Storage::Ptr m_stroage;
};

}  // namespace storage

}  // namespace bcos
