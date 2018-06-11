/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: RateLimiter.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "RateLimiter.h"
#include "libdevcore/easylog.h"

using namespace std;
using namespace dev;

RateLimiter::RateLimiter(const std::string &configJson) : enable(true), callCost(1), asioThread(NULL), cleanInterval(8), shutdownThread(false)
{
    if (configJson.empty() || !phraseConfig(configJson))
    {
        enable = false;
        LOG(INFO) << "rate limiter is off.";
    }

    if (enable)
    {
        LOG(INFO) << "rate limiter started.";
        LOG(INFO) << "RateLimiter Config" << configJson << endl;
        asioThread = make_shared<thread>(thread([&]() {
            while (!shutdownThread)
            {
                this_thread::sleep_for(std::chrono::seconds(cleanInterval));
                cleanInvalidKey();
            }
        }));
    }
}

// check necessary params
bool RateLimiter::validateConfigJson(const Json::Value &config)
{
    if (!config.isObject())
        return false;
    if (!(config.isMember(RL_GLOBAL_LIMIT) && config[RL_GLOBAL_LIMIT].isObject()))
    {
        LOG(ERROR) << "Rate Limit config file error. Missing " << RL_GLOBAL_LIMIT << " or type isn't json Object";
        return false;
    }

    if (!(config[RL_GLOBAL_LIMIT].isMember(RL_ENABLE) && config[RL_GLOBAL_LIMIT][RL_ENABLE].isBool()))
    {
        LOG(ERROR) << "Rate Limit config file error. " << RL_GLOBAL_LIMIT << " missing " << RL_ENABLE << " or type isn't bool";
        return false;
    }
    if (!(config[RL_GLOBAL_LIMIT].isMember(RL_DEFAULT_LIMIT) && config[RL_GLOBAL_LIMIT][RL_DEFAULT_LIMIT].isUInt()))
    {
        LOG(ERROR) << "Rate Limit config file error. " << RL_GLOBAL_LIMIT << " missing " << RL_DEFAULT_LIMIT << " or type isn't uint";
        return false;
    }
    if (!(config.isMember(RL_INTERFACE_LIMIT) && config[RL_INTERFACE_LIMIT].isObject()))
    {
        LOG(ERROR) << "Rate Limit config file error. Missing " << RL_INTERFACE_LIMIT << " or type isn't json Object";
        return false;
    }
    if (!(config[RL_INTERFACE_LIMIT].isMember(RL_ENABLE) && config[RL_INTERFACE_LIMIT][RL_ENABLE].isBool()))
    {
        LOG(ERROR) << "Rate Limit config file error. " << RL_INTERFACE_LIMIT << " missing " << RL_ENABLE << " or type isn't bool";
        return false;
    }
    if (!(config[RL_INTERFACE_LIMIT].isMember(RL_DEFAULT_LIMIT) && config[RL_INTERFACE_LIMIT][RL_DEFAULT_LIMIT].isUInt()))
    {
        LOG(ERROR) << "Rate Limit config file error. " << RL_INTERFACE_LIMIT << " missing " << RL_DEFAULT_LIMIT << " or type isn't uint";
        return false;
    }
    if (!(config[RL_INTERFACE_LIMIT].isMember(RL_CUSTOM) && config[RL_INTERFACE_LIMIT][RL_CUSTOM].isObject()))
    {
        LOG(ERROR) << "Rate Limit config file error. " << RL_INTERFACE_LIMIT << " missing " << RL_CUSTOM << " or type isn't json Object";
        return false;
    }
    Json::Value::Members mem = config[RL_INTERFACE_LIMIT][RL_CUSTOM].getMemberNames();
    for (auto name : mem)
    {
        if (!config[RL_INTERFACE_LIMIT][RL_CUSTOM][name].isUInt())
        {
            LOG(ERROR) << "Rate Limit config file error. " << RL_INTERFACE_LIMIT << " " << RL_CUSTOM << " " << name << "'s value isn't json Object";
            return false;
        }
    }
    return true;
}

bool RateLimiter::phraseConfig(const std::string &configJson)
{
    Json::Reader reader;
    if (!reader.parse(configJson, config, false))
    {
        LOG(ERROR) << "Rate Limit config file syntax error.";
        return false;
    }

    if (!validateConfigJson(config))
        return false;

    Json::Value value;
    globalLimitEnable = config[RL_GLOBAL_LIMIT][RL_ENABLE].asBool();
    globalBucket.capacity = config[RL_GLOBAL_LIMIT][RL_DEFAULT_LIMIT].asInt();
    if (globalLimitEnable && globalBucket.capacity <= 0)
    { //shouldn't happen
        globalLimitEnable = false;
        globalBucket.capacity = 0;
    }

    globalBucket.lastCall = chrono::steady_clock::now();

    interfaceLimitEnable = config[RL_INTERFACE_LIMIT][RL_ENABLE].asBool();
    if (!interfaceLimitEnable)
        return true;
    interfacelLimit = config[RL_INTERFACE_LIMIT][RL_DEFAULT_LIMIT].asInt();
    if (interfacelLimit < 0)
        interfacelLimit = 0;
    if (interfacelLimit == 0 && config[RL_INTERFACE_LIMIT][RL_CUSTOM].isNull())
    {
        interfaceLimitEnable = false;
        return true;
    }
    if (!config[RL_INTERFACE_LIMIT][RL_CUSTOM].isNull())
    {
        // phrase interface limit config
        Json::Value::Members mem = config[RL_INTERFACE_LIMIT][RL_CUSTOM].getMemberNames();
        for (auto name : mem)
        {
            m_interfaceLimit[name] = config[RL_INTERFACE_LIMIT][RL_CUSTOM][name].asInt();
        }
    }
    return true;
}

void RateLimiter::updateBucket(TokenBucket &bucket)
{
    if (bucket.tokens == RL_NEW_BUCKET)
        bucket.tokens = bucket.capacity * 0.75;
    rl_time_point currTime = chrono::steady_clock::now();
    chrono::milliseconds timeLong = chrono::duration_cast<chrono::milliseconds>(currTime - bucket.lastCall);
    if (timeLong.count() > 0)
    {
        double newToken = bucket.capacity / 1000.0 * timeLong.count() + bucket.tokens;
        lock_guard<std::mutex> l(mt);
        bucket.tokens = min((double)bucket.capacity, newToken);
        bucket.lastCall = currTime;
    }
}

bool RateLimiter::isPermitted(const std::string &ip, const std::string &interface)
{
    // limiter is off, always return true
    if (!isEnable())
        return true;
    // check global limit
    if (globalLimitEnable)
    {
        updateBucket(globalBucket);
        if (globalBucket.tokens > callCost)
        {
            lock_guard<std::mutex> l(mt);
            globalBucket.tokens -= callCost;
        }
        else
            return false;
    }

    //check interface limit
    string key = ip + interface;
    if (interfaceLimitEnable)
    {
        if (interfacelLimit == 0)
        { //check if has own config
            auto it = m_interfaceLimit.find(interface);
            if (it != m_interfaceLimit.end())
            {
                m_tokenBuckets[key].capacity = it->second;
                updateBucket(m_tokenBuckets[key]);
                if (m_tokenBuckets[key].tokens > callCost)
                {
                    lock_guard<std::mutex> l(mt);
                    m_tokenBuckets[key].tokens -= callCost;
                }
                else
                    return false;
            }
        }
        else
        {
            //if no own config then config by default
            auto it = m_interfaceLimit.find(interface);
            if (it != m_interfaceLimit.end())
            {
                m_tokenBuckets[key].capacity = it->second;
            }
            else
            {
                m_tokenBuckets[key].capacity = interfacelLimit;
            }

            updateBucket(m_tokenBuckets[key]);
            if (m_tokenBuckets[key].tokens > callCost)
            {
                lock_guard<std::mutex> l(mt);
                m_tokenBuckets[key].tokens -= callCost;
            }
            else
                return false;
        }
    }

    return true;
}

void RateLimiter::cleanInvalidKey()
{
    rl_time_point currTime = chrono::steady_clock::now();
    for (auto it = m_tokenBuckets.begin(); it != m_tokenBuckets.end();)
    {
        chrono::seconds timeLong = chrono::duration_cast<chrono::seconds>(currTime - it->second.lastCall);
        if (timeLong.count() > cleanInterval * 2)
        {
            lock_guard<std::mutex> l(mt);
            it = m_tokenBuckets.erase(it);
        }
        else
            ++it;
    }
}
