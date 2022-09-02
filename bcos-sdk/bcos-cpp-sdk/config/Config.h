/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file Config.h
 * @author: octopus
 * @date 2021-12-14
 */
#pragma once

#include <bcos-boostssl/websocket/WsConfig.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <memory>
#include <string>

namespace bcos
{
namespace cppsdk
{
namespace config
{
class Config
{
public:
    using Ptr = std::shared_ptr<Config>;
    using ConstPtr = std::shared_ptr<const Config>;

public:
    std::shared_ptr<bcos::boostssl::ws::WsConfig> loadConfig(const std::string& _configPath);
    std::shared_ptr<bcos::boostssl::ws::WsConfig> loadConfig(
        boost::property_tree::ptree const& _pt);

    void loadCommon(boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config);
    void loadPeers(boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config);
    void loadCert(boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config);
    void loadSslCert(boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config);
    void loadSMSslCert(
        boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config);
};

}  // namespace config
}  // namespace cppsdk
}  // namespace bcos