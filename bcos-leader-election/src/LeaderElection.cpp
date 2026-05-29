/**
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief leader-election
 * @file LeaderElection.cpp
 * @author: yujiechen
 * @date 2022-04-26
 */
#include "LeaderElection.h"
#include "bcos-utilities/BoostLog.h"
#include <etcd/KeepAlive.hpp>
#include <etcd/v3/Transaction.hpp>

using namespace bcos;
using namespace bcos::election;

void LeaderElection::start()
{
    auto self = std::weak_ptr<LeaderElection>(shared_from_this());
    m_config->registerTriggerCampaignHandler([self]() -> bool {
        auto leaderElection = self.lock();
        if (!leaderElection)
        {
            return false;
        }
        return leaderElection->campaignLeader();
    });
    m_campaignTimer->registerTimeoutHandler([self]() {
        auto leaderElection = self.lock();
        if (!leaderElection)
        {
            return;
        }
        leaderElection->campaignLeader();
    });
    if (m_config)
    {
        m_config->start();
    }
    auto leader = m_config->fetchLeader();
    if (!leader)
    {
        return;
    }
    campaignLeader();
}

void LeaderElection::stop()
{
    if (m_campaignTimer)
    {
        m_campaignTimer->stop();
    }
    if (m_config)
    {
        m_config->stop();
    }
}

std::pair<bool, int64_t> LeaderElection::grantLease()
{
    auto response = m_etcdClient->leasegrant(m_config->leaseTTL()).get();
    if (!response.is_ok())
    {
        ELECTION_LOG(ERROR) << LOG_DESC("grantLease error")
                            << LOG_KV("msg", response.error_message())
                            << LOG_KV("code", response.error_code());
        return std::make_pair(false, 0);
    }
    auto leaseID = response.value().lease();
    ELECTION_LOG(INFO) << LOG_DESC("grantLease success") << LOG_KV("id", leaseID)
                       << LOG_KV("ttl", response.value().ttl())
                       << LOG_KV("purpose", m_config->purpose());
    return std::make_pair(true, leaseID);
}

bool LeaderElection::campaignLeader()
{
    // FIB-169: do NOT hold m_mutex across blocking etcd RPCs or external
    // handler callbacks. The lock is now used only to serialize the small
    // bookkeeping window where we swap m_keepAlive and update the config's
    // leader pointer; everything else (etcd grantLease/txn, m_onCampaignHandler,
    // tryToSwitchToBackup) runs without the lock so a stalled etcd request or
    // slow user-supplied handler cannot block sibling campaign / updateSelfConfig
    // threads.
    try
    {
        // Step 1 (no lock): if there is already a leader, just defer to backup
        // promotion logic. tryToSwitchToBackup() invokes m_onCampaignHandler and
        // therefore must not run under m_mutex.
        if (m_config->getLeader())
        {
            tryToSwitchToBackup();
            return false;
        }

        // Step 2 (no lock): grantLease() performs a blocking etcd RPC.
        auto ret = grantLease();
        if (!ret.first)
        {
            tryToSwitchToBackup();
            m_campaignTimer->restart();
            return false;
        }
        auto leaseID = ret.second;

        // Snapshot config-derived state without holding m_mutex; CampaignConfig
        // already serializes its own internal state with x_self / x_leader.
        auto const leaderKey = m_config->leaderKey();
        auto const leaderValue = m_config->leaderValue();

        auto tx = std::make_shared<etcdv3::Transaction>(leaderKey);
        tx->init_compare(0, etcdv3::CompareResult::EQUAL, etcdv3::CompareTarget::MOD);
        tx->setup_basic_failure_operation(leaderKey);
        tx->setup_basic_create_sequence(leaderKey, leaderValue, leaseID);

        // Step 3 (no lock): blocking etcd txn RPC.
        auto response = m_etcdClient->txn(*tx).get();
        if (!response.is_ok())
        {
            // failed for non-compare-failed error, restart campaign
            if (response.error_code() != etcdv3::ERROR_COMPARE_FAILED)
            {
                m_campaignTimer->restart();
            }
            else
            {
                // failed for compare-failed error, stop campaign
                m_campaignTimer->stop();
            }
            ELECTION_LOG(INFO) << LOG_DESC("campaignLeader failed")
                               << LOG_KV("msg", response.error_message())
                               << LOG_KV("code", response.error_code())
                               << LOG_KV("purpose", m_config->purpose())
                               << LOG_KV("lease", leaseID);
            // tryToSwitchToBackup invokes m_onCampaignHandler — outside lock.
            tryToSwitchToBackup();
            return false;
        }
        m_campaignTimer->stop();
        ELECTION_LOG(INFO) << LOG_DESC("campaignLeader success") << LOG_KV("leaderKey", leaderKey)
                           << LOG_KV("purpose", m_config->purpose()) << LOG_KV("lease", leaseID)
                           << LOG_KV("version", response.value().version())
                           << LOG_KV("msg", response.error_message())
                           << LOG_KV("value", response.value().as_string())
                           << LOG_KV("key", response.value().key());

        // Step 4 (brief critical section): swap m_keepAlive and commit
        // setLeaderToSelf. This is the ONLY work that needs m_mutex and it
        // does no I/O — etcd::KeepAlive construction is non-blocking; the
        // background heartbeat runs on its own thread.
        auto keepAliveTTL = m_config->leaseTTL() - 1;
        auto weakSelf = std::weak_ptr<LeaderElection>(shared_from_this());
        std::shared_ptr<etcd::KeepAlive> oldKeepAlive;
        {
            RecursiveGuard l(m_mutex);
            oldKeepAlive = std::move(m_keepAlive);
            m_keepAlive = std::make_shared<etcd::KeepAlive>(
                *(m_config->etcdClient()),
                [weakSelf](std::exception_ptr e) {
                    auto self = weakSelf.lock();
                    if (!self)
                    {
                        return;
                    }
                    self->onKeepAliveException(e);
                },
                keepAliveTTL, leaseID);
            m_config->setLeaderToSelf(leaseID, response.value().modified_index());
        }
        // Cancel the old keepAlive outside the lock — Cancel() may join an
        // internal thread and we must not pay that latency under m_mutex.
        if (oldKeepAlive)
        {
            ELECTION_LOG(INFO) << LOG_DESC("campaignLeader: cancel keepAlive thread")
                               << LOG_KV("lease", oldKeepAlive->Lease())
                               << LOG_KV("leaderKey", leaderKey);
            oldKeepAlive->Cancel();
        }

        // Step 5 (no lock): user-supplied campaign handler. Must not run under
        // m_mutex because the handler does upper-layer work (e.g. switching the
        // node into master-mode) which can be slow or itself acquire other
        // locks.
        auto leader = m_config->getLeader();
        if (m_onCampaignHandler)
        {
            m_onCampaignHandler(true, leader);
        }
        ELECTION_LOG(INFO)
            << LOG_DESC("campaignLeader: establish new keepAlive thread and switch to master-node")
            << LOG_KV("ttl", keepAliveTTL) << LOG_KV("lease", leaseID)
            << LOG_KV("leaderKey", leaderKey);
        return true;
    }
    catch (std::exception const& e)
    {
        ELECTION_LOG(WARNING) << LOG_DESC("campaignLeader exception")
                              << LOG_KV("message", boost::diagnostic_information(e));
        // FIB-169: read m_keepAlive under the lock to avoid a torn pointer
        // read racing with the success path. Cancel() runs outside the lock.
        std::shared_ptr<etcd::KeepAlive> keepAlive;
        {
            RecursiveGuard l(m_mutex);
            keepAlive = m_keepAlive;
        }
        if (keepAlive)
        {
            ELECTION_LOG(INFO) << LOG_DESC("campaignLeader: cancel keepAlive thread for exception")
                               << LOG_KV("lease", keepAlive->Lease())
                               << LOG_KV("leaderKey", m_config->leaderKey());
            keepAlive->Cancel();
        }
    }
    return false;
}

void LeaderElection::onKeepAliveException(std::exception_ptr _exception)
{
    try
    {
        if (_exception)
        {
            std::rethrow_exception(_exception);
        }
    }
    catch (const std::exception& e)
    {
        ELECTION_LOG(WARNING) << LOG_DESC("onKeepAliveException, restart campaign")
                              << LOG_KV("message", boost::diagnostic_information(e));
    }
    if (m_campaignTimer)
    {
        m_campaignTimer->restart();
    }
    if (m_onKeepAliveException)
    {
        m_onKeepAliveException(_exception);
    }
}

void LeaderElection::tryToSwitchToBackup()
{
    if (!m_onCampaignHandler)
    {
        return;
    }
    auto leader = m_config->getLeader();
    if (leader && m_config->self()->memberID() == leader->memberID())
    {
        ELECTION_LOG(INFO) << LOG_DESC("tryToSwitchToBackup failed for the node-self is leader")
                           << LOG_KV("id", leader->memberID());
        return;
    }
    ELECTION_LOG(INFO) << LOG_DESC("tryToSwitchToBackup")
                       << LOG_KV("memberID", m_config->self()->memberID())
                       << LOG_KV("leader", leader ? leader->memberID() : "no-leader");
    m_onCampaignHandler(false, leader);
}

void LeaderElection::updateSelfConfig(bcos::protocol::MemberInterface::Ptr _self)
{
    // FIB-169: update local config under m_mutex but release the mutex BEFORE
    // dispatching the etcd commit RPC. CampaignConfig::updateSelf already
    // serializes via its own x_self lock; m_mutex here only protects ordering
    // against campaignLeader's brief critical section.
    int64_t leaseID = 0;
    std::string leaderKey;
    std::string leaderValue;
    bool isSelfLeader = false;
    {
        RecursiveGuard l(m_mutex);
        m_config->updateSelf(_self);
        ELECTION_LOG(INFO) << LOG_DESC("updateSelfConfig") << LOG_KV("ID", _self->memberID());
        auto leader = m_config->getLeader();
        if (!leader || leader->memberID() != _self->memberID())
        {
            ELECTION_LOG(INFO) << LOG_DESC("updateSelfConfig return for the node is not leader")
                               << LOG_KV("leaderID", leader ? leader->memberID() : "None");
            return;
        }
        leaseID = leader->leaseID();
        leaderKey = m_config->leaderKey();
        leaderValue = m_config->leaderValue();
        isSelfLeader = true;
    }
    if (!isSelfLeader)
    {
        return;
    }
    ELECTION_LOG(INFO)
        << LOG_DESC("updateSelfConfig, the node-self is leader, sync the modified memberConfig")
        << LOG_KV("lease", leaseID);
    auto tx = std::make_shared<etcdv3::Transaction>(leaderKey);
    tx->init_lease_compare(leaseID, etcdv3::CompareResult::EQUAL, etcdv3::CompareTarget::LEASE);
    tx->setup_basic_failure_operation(leaderKey);
    tx->setup_compare_and_swap_sequence(leaderValue, leaseID);
    // Blocking etcd RPC executed WITHOUT m_mutex held (FIB-169).
    auto response = m_etcdClient->txn(*tx).get();
    if (!response.is_ok())
    {
        ELECTION_LOG(WARNING) << LOG_DESC("sync the modified memberConfig to storage error")
                              << LOG_KV("code", response.error_code())
                              << LOG_KV("msg", response.error_message()) << LOG_KV("lease", leaseID)
                              << LOG_KV("leaderKey", leaderKey);
        return;
    }
    ELECTION_LOG(INFO) << LOG_DESC("updateSelfConfig success");
}
