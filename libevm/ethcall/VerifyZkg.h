/*
 * This file is part of FISCO BCOS.
 * 
 * FISCO BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * FISCO BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 * @file: VerifyZkg.h
 * @author: jimmyshi
 * @date: 4th May 2018
 */


#pragma once

#include "EthCallEntry.h"
#include <libdevcore/easylog.h>
#include <libdevcore/Common.h>
#include <libethereum/Client.h>
#include <libethereum/Pool.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <string>
#include <vector>
#include <circuit/zkg.hpp>
#include <unistd.h>

//using namespace std;
//using namespace dev;
//using namespace dev::eth;

namespace dev
{
namespace eth
{

enum ZKGErrorCode : uint32_t
{
    SUCCESS = 0,
    VERIFY_FAILD = 1,
    SN_EXISTENCE = 2,
    CM_OLD_INEXISTENCE = 3,
    CM_NEW_EXISTENCE = 4,
    BLOCKER_EXISTENCE = 5,
    SN_REPEATED = 6,
    INVALID_PARAMETERS = 7
};

class ZkgPool
{
  public:
    std::shared_ptr<ExistencePool> cm_root_pool, sn_pool;
    std::shared_ptr<MerkleTree> cm_tree;
    bool has_setup = false;

  public:
    ZkgPool() {}
    ~ZkgPool() {}

    void setup();

    bool checkSn(poolBlockNumber_t cbn, const std::string &sn);
    bool checkCmRoot(poolBlockNumber_t cbn, const std::string &cm_root);
    bool checkNewCm(poolBlockNumber_t cbn, const std::string &cm);

    void addSn(poolBlockNumber_t cbn, const std::string &sn);
    void addCm(poolBlockNumber_t cbn, const std::string &cm);
    void addGovData(poolBlockNumber_t cbn, const std::string &G_data);

  private:
    std::string currentRoot(poolBlockNumber_t cbn);
    static size_t treeCapacity()
    {
        return size_t(1 << Tx1To1API::TREE_DEPTH());
    }
};

class VerifyZkg : EthCallExecutor<
                      std::string, //input_rt[0], input_rt[1]
                                   //input_sn[0], input_sn[1]
                                   //output_cm[0], output_cm[1]
                                   //g_256， Gpk_256
                      uint64_t,    //vpub_old_64
                      uint64_t,    //vpub_new_64
                      std::string, //G_data_v 监管数据
                      std::string  //proof
                      >
{
  private:
    std::shared_ptr<Tx1To1API> tx1to1;
    ZkgPool zkg_pool;

    bool has_init = false;

  public:
    VerifyZkg()
    {
        this->registerEthcall(EthCallIdList::VERIFY_ZKG);
    }

    u256 ethcallEnv(
        ExtVMFace *env,
        std::string params,
        uint64_t vpub_old_64,
        uint64_t vpub_new_64,
        std::string G_data_str,
        std::string proof);

    uint64_t gasCost(
        std::string params,
        uint64_t vpub_old_64,
        uint64_t vpub_new_64,
        std::string G_data_str,
        std::string proof);

    void initZkg();
};
}
}