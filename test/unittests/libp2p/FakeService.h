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
 * @brief: fake service of libp2p
 * @file: FakeTxPool.h
 * @author: yujiechen
 * @date: 2018-09-25
 */
#pragma once
#include "FakeHost.h"
#include <libp2p/Service.h>
#include <libp2p/Session.h>
using namespace dev;
using namespace dev::p2p;
namespace dev
{
namespace test
{
static std::shared_ptr<Service> createService(
    std::string const& client_version, std::string const& listenIp, uint16_t const& listenPort)
{
    FakeHost* hostPtr = createFakeHost(client_version, listenIp, listenPort);
    std::shared_ptr<P2PMsgHandler> m_p2pHandler = std::make_shared<P2PMsgHandler>();
    std::shared_ptr<Host> m_host = std::shared_ptr<Host>(hostPtr);
    std::shared_ptr<Service> m_service = std::make_shared<Service>(m_host, m_p2pHandler);
    return m_service;
}
}  // namespace test
}  // namespace dev
