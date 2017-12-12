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
 * @file: RateLimiter.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef DEV_RATE_LIMITER_H_
#define DEV_RATE_LIMITER_H_
#include <json/json.h>
#include <chrono>
#include <string>
#include <unordered_map>
#include <thread>
#include <memory>
#include <mutex>

#define RL_GLOBAL_LIMIT "globalLimit"
#define RL_INTERFACE_LIMIT "interfaceLimit"
#define RL_ENABLE "enable"
#define RL_DEFAULT_LIMIT "defaultLimit"
#define RL_CUSTOM "custom"
#define RL_NEW_BUCKET -1

namespace dev
{
using rl_time_point = std::chrono::steady_clock::time_point;

struct TokenBucket
{
	int capacity;
	double tokens;
	rl_time_point lastCall;
	TokenBucket() : tokens(RL_NEW_BUCKET), lastCall(std::chrono::steady_clock::now()) {}
};

class RateLimiter
{

  public:
	explicit RateLimiter(const std::string &configJson = std::string());
	~RateLimiter()
	{
		shutdownThread = true;
		asioThread->join();
	};
	bool isPermitted(const std::string &ip, const std::string &interface);
	bool isEnable() { return enable; }

  private:
	bool enable;
	bool globalLimitEnable;
	// total calls per second
	TokenBucket globalBucket;
	bool interfaceLimitEnable;
	// default calls limit of (per ip+interface per second)
	int interfacelLimit;
	Json::Value config;
	std::mutex mt;
	std::unordered_map<std::string, TokenBucket> m_tokenBuckets;
	// custom interface frequency
	std::unordered_map<std::string, int> m_interfaceLimit;
	int callCost;

	std::shared_ptr<std::thread> asioThread;
	// seconds to call cleanInvalidKey()
	int cleanInterval;
	bool shutdownThread;
	//clean invalid key in m_tokenBuckets
	void cleanInvalidKey();
	// check config file
	bool validateConfigJson(const Json::Value &config);
	//config from json
	bool phraseConfig(const std::string &configJson);
	//update token bucket
	void updateBucket(TokenBucket &bucket);
};
}
#endif //DEV_RATE_LIMITER_H_
