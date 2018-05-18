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
 * @file: VerifyZkg.cpp
 * @author: jimmyshi
 * @date: 4th May 2018
 */


#include "VerifyZkg.h"
#include <circuit/zkg.hpp>

namespace dev
{
namespace eth
{
//ZkgPool
void ZkgPool::setup()
{
    if (has_setup)
        return;
    has_setup = true;

    if (cm_tree.get() == NULL)
        cm_tree = make_shared<MerkleTree>("cm_tree");

    CMPool_Singleton &cm_pool = CMPool_Singleton::Instance();

    cm_root_pool = make_shared<ExistencePool>("cmroot");
    sn_pool = make_shared<ExistencePool>("sn");

    size_t cm_pool_size = cm_pool.size(0); //Get block 0's pool size
    if (0 == cm_pool_size) //first created pool, init it
    {
        //ZERO CM is the first CM in pool, add it in block 0
        addCm(0, Tx1To1API::ZERO_CM());
    }
}

bool ZkgPool::checkSn(poolBlockNumber_t cbn, const std::string &sn)
{
    return !sn_pool->is_exist(cbn, sn) || sn == Tx1To1API::ZERO_SN(); //sn inexistence or zero_sn is valid
}

bool ZkgPool::checkCmRoot(poolBlockNumber_t cbn, const std::string &cm_root)
{
    return cm_root_pool->is_exist(cbn, cm_root); //cm_root existence is valid
}

bool ZkgPool::checkNewCm(poolBlockNumber_t cbn, const std::string &cm)
{
    return !CMPool_Singleton::Instance().is_exist(cbn, cm); //cm inexistence is valid
}

void ZkgPool::addSn(poolBlockNumber_t cbn, const string &sn)
{
    if (sn != Tx1To1API::ZERO_SN()) //we can use zero_sn forever
        sn_pool->put_if_not_in_this_block(cbn, sn);
}

void ZkgPool::addCm(poolBlockNumber_t cbn, const string &cm)
{
    CMPool_Singleton &cm_pool = CMPool_Singleton::Instance();
    cm_pool.push_back_if_not_in_this_block(cbn, cm);

    string cm_root = currentRoot(cbn);
    LOG(DEBUG) << "current root: " << cm_root << endl;
    cm_root_pool->put_if_not_in_this_block(cbn, cm_root);
}

void ZkgPool::addGovData(poolBlockNumber_t cbn, const string &G_data)
{
    GovDataPool_Singleton &gov_data_pool = GovDataPool_Singleton::Instance();
    gov_data_pool.push_back_if_not_in_this_block(cbn, G_data);
}

std::string ZkgPool::currentRoot(poolBlockNumber_t cbn)
{
    CMPool_Singleton &cm_pool = CMPool_Singleton::Instance();

    //add cm[from, to) -> cm_tree
    size_t to = cm_pool.size(cbn);
    size_t from = max((int)0, (int)to - (int)treeCapacity());
    cm_tree->clear();
    for (size_t i = from; i < to; i++)
        cm_tree->append(cm_pool.get(cbn, i));

    string root = cm_tree->root();
    cm_tree->clear();
    return root;
}

static boost::mutex zkg_pool_lock;
static boost::mutex zkg_verify_lock;
//VerifyZkg ethcall
u256 VerifyZkg::ethcallEnv(
    ExtVMFace *env,
    std::string params,
    uint64_t vpub_old_64,
    uint64_t vpub_new_64,
    std::string G_data_str,
    std::string proof)
{
    initZkg();
    //解析param
    if (params.size() != 8 * 64) //8 parameters, 64 bytes each
    {
        LOG(ERROR) << "Params length is invalid" << std::endl;
        return ZKGErrorCode::INVALID_PARAMETERS;
    }

    size_t offset = 0;
    string input_rt0_str = params.substr(0, 64),
           input_rt1_str = params.substr(64, 64),
           input_sn0_str = params.substr(64 * 2, 64),
           input_sn1_str = params.substr(64 * 3, 64),
           output_cm0_str = params.substr(64 * 4, 64),
           output_cm1_str = params.substr(64 * 5, 64),
           g_str = params.substr(64 * 6, 64),
           Gpk_str = params.substr(64 * 7, 64);

    Tx1To1Param verify_param;
    verify_param.s_rts[0] = params.substr(0, 64);
    verify_param.s_rts[1] = params.substr(64, 64);
    verify_param.s_sns[0] = params.substr(64 * 2, 64);
    verify_param.s_sns[1] = params.substr(64 * 3, 64);
    verify_param.r_cms[0] = params.substr(64 * 4, 64);
    verify_param.r_cms[1] = params.substr(64 * 5, 64);
    verify_param.vpub_old = vpub_old_64;
    verify_param.vpub_new = vpub_new_64;
    verify_param.g = params.substr(64 * 6, 64);
    verify_param.Gpk = params.substr(64 * 7, 64);
    verify_param.G_data = G_data_str;
    verify_param.proof = proof;

    //LOG(DEBUG) <<
    LOG(TRACE) << "------------Verify Zkg--------- " << std::endl;
    LOG(TRACE) << "input_rt0:\t" << verify_param.s_rts[0] << std::endl;
    LOG(TRACE) << "input_rt1:\t" << verify_param.s_rts[1] << std::endl;
    LOG(TRACE) << "input_sn0:\t" << verify_param.s_sns[0] << std::endl;
    LOG(TRACE) << "input_sn1:\t" << verify_param.s_sns[1] << std::endl;
    LOG(TRACE) << "output_cm0:\t" << verify_param.r_cms[0] << std::endl;
    LOG(TRACE) << "output_cm1:\t" << verify_param.r_cms[1] << std::endl;
    LOG(TRACE) << "vpub_old_64:\t" << verify_param.vpub_old << std::endl;
    LOG(TRACE) << "vpub_new_64:\t" << verify_param.vpub_new << std::endl;
    LOG(TRACE) << "g:\t" << verify_param.g << std::endl;
    LOG(TRACE) << "Gpk:\t" << verify_param.Gpk << std::endl;
    LOG(TRACE) << "G_data:\t" << verify_param.G_data << std::endl;
    LOG(TRACE) << "proof:\t" << verify_param.proof << std::endl;

    poolBlockNumber_t cbn = static_cast<poolBlockNumber_t>(env->envInfo().number());
    LOG(TRACE) << "Block number: " << cbn << endl;

    if (!zkg_pool.checkSn(cbn - 1, verify_param.s_sns[0]))
    {
        LOG(DEBUG) << "Verify failed! SN existence(money has been spent):\t" << verify_param.s_sns[0] << endl;
        return ZKGErrorCode::SN_EXISTENCE;
    }

    if (!zkg_pool.checkSn(cbn - 1, verify_param.s_sns[1]))
    {
        LOG(DEBUG) << "Verify failed! SN existence(money has been spent):\t" << verify_param.s_sns[1] << endl;
        return ZKGErrorCode::SN_EXISTENCE;
    }

    //check SN[0] and SN[1] are not the same(ignore zero CM's sn)
    if (ZkgTool::is_same_uint256_str(verify_param.s_sns[0], verify_param.s_sns[1])
     && !ZkgTool::is_same_uint256_str(verify_param.s_sns[0], Tx1To1API::ZERO_SN()))
    {
        LOG(DEBUG) << "Verify failed! SN repeated(spend the same money):\t" << verify_param.s_sns[0] << endl;
        return ZKGErrorCode::SN_REPEATED;        
    }

    //*
    if (!zkg_pool.checkCmRoot(cbn - 1, verify_param.s_rts[0]))
    {
        LOG(DEBUG) << "Verify failed! Old CM inexistence(illegal money):\t" << verify_param.s_rts[0] << endl;
        return ZKGErrorCode::CM_OLD_INEXISTENCE;
    }

    if (!zkg_pool.checkCmRoot(cbn - 1, verify_param.s_rts[1]))
    {
        LOG(DEBUG) << "Verify failed! Old CM inexistence(illegal money):\t" << verify_param.s_rts[1] << endl;
        return ZKGErrorCode::CM_OLD_INEXISTENCE;
    }

    if (!zkg_pool.checkNewCm(cbn - 1, verify_param.r_cms[0]))
    {
        LOG(DEBUG) << "Verify failed! New CM existence(create an existing money):\t" << verify_param.r_cms[0] << endl;
        return ZKGErrorCode::CM_NEW_EXISTENCE;
    }

    if (!zkg_pool.checkNewCm(cbn - 1, verify_param.r_cms[1]))
    {
        LOG(DEBUG) << "Verify failed! New CM existence(create an existing money):\t" << verify_param.r_cms[1] << endl;
        return ZKGErrorCode::CM_NEW_EXISTENCE;
    }
    //if (!zkg_pool.checkBlocker())
    //*/
    zkg_verify_lock.lock();
    try
    {
        if (!tx1to1->verify(verify_param))
        {
            LOG(DEBUG) << "Verify failed! Incorrect proof or params." << std::endl;
            zkg_verify_lock.unlock();
            return ZKGErrorCode::VERIFY_FAILD;
        }
        LOG(DEBUG) << "Verify success! " << std::endl;
    }
    catch (Exception &e)
    {
        LOG(DEBUG) << "Verify failed for: " << e.what() << endl;
        zkg_verify_lock.unlock();
        return ZKGErrorCode::VERIFY_FAILD;
    }
    catch (...)
    {
        LOG(DEBUG) << "Verify failed" << endl;
        zkg_verify_lock.unlock();
        return ZKGErrorCode::VERIFY_FAILD;
    }
    zkg_verify_lock.unlock();

    zkg_pool_lock.lock();
    //update sn
    zkg_pool.addSn(cbn, verify_param.s_sns[0]);
    zkg_pool.addSn(cbn, verify_param.s_sns[1]);

    //update cm and cm_root at the same time
    zkg_pool.addCm(cbn, verify_param.r_cms[0]);
    zkg_pool.addCm(cbn, verify_param.r_cms[1]);

    //update blocker

    //update gov_data
    zkg_pool.addGovData(cbn, verify_param.G_data);
    zkg_pool_lock.unlock();

    return ZKGErrorCode::SUCCESS;
}

uint64_t VerifyZkg::gasCost(
    std::string params,
    uint64_t vpub_old_64,
    uint64_t vpub_new_64,
    std::string G_data_str,
    std::string proof)
{
    uint64_t cost =
        params.length() +
        sizeof(vpub_old_64) +
        sizeof(vpub_new_64) +
        G_data_str.length() +
        proof.length();

    LOG(TRACE) << "Zkg Verify gas cost: " << cost << std::endl;
    return cost;
}

static boost::mutex init_flag_lock;
void VerifyZkg::initZkg()
{
    init_flag_lock.lock();
    if (has_init)
    {
        init_flag_lock.unlock();
        return;
    }
    has_init = true;

    //init tx1to1 api
    if (tx1to1.get() == NULL)
    {
        zkg_set_log_verbosity(ZKG_LOG_NONE);
        tx1to1 = make_shared<Tx1To1API>(
            false, false,
            getDataDir() + string("/pk.data"), //no need at all
            getDataDir() + string("/vk.data"));
    }

    //setup pool
    zkg_pool.setup(); //setup = init/load pool
    init_flag_lock.unlock();
}
}
}