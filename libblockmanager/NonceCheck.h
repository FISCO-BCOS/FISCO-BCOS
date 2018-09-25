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
/**
 * @file: NonceCheck.h
 * @author: toxotguo
 *
 * @date: 2017
 */

#pragma once

#include "BlockManagerInterface.h"
#include <libblockmanager/BlockManagerInterface.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <boost/timer.hpp>
#include <thread>

using namespace std;
using namespace dev::eth;
using namespace dev::blockmanager;
namespace dev
{
namespace eth
{
class NonceCheck
{
public:
    static u256 maxblocksize;
    NonceCheck(std::shared_ptr<BlockManagerInterface> const& _blockManager)
      : m_blockManager(_blockManager)
    {}
    ~NonceCheck();
    void init();
    bool ok(Transaction const& _transaction, bool _needinsert = false);
    void updateCache(bool _rebuild = false);
    void delCache(Transactions const& _transcations);

private:
    std::weak_ptr<BlockManagerInterface> m_blockManager;
    std::unordered_map<std::string, bool> m_cache;
    unsigned m_startblk;
    unsigned m_endblk;
    mutable SharedMutex m_lock;

    std::string generateKey(Transaction const& _t);
};
}  // namespace eth
}  // namespace dev
