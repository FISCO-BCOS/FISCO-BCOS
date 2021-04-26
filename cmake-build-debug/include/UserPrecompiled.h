/*
    This file is part of FISCO-BCOS
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
 * @file: UserPrecompiled.h
 * @author: bxq
 * @date: 2019
 */

#include "libblockverifier/ExecutiveContextFactory.h"
#include <libprecompiled/extension/DagTransferPrecompiled.h>
#include <libprecompiled/extension/GroupSigPrecompiled.h>
#include <libprecompiled/extension/HelloWorldPrecompiled.h>
#include <libprecompiled/extension/PaillierPrecompiled.h>
#include <libprecompiled/extension/RingSigPrecompiled.h>


    void
    dev::blockverifier::ExecutiveContextFactory::registerUserPrecompiled(
        dev::blockverifier::ExecutiveContext::Ptr context)
{
    // Address should in [0x5001,0xffff]
    // context->setAddress2Precompiled(Address(0x5001),
    // std::make_shared<dev::precompiled::HelloWorldPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x5002), std::make_shared<dev::precompiled::DagTransferPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x5003), std::make_shared<dev::precompiled::PaillierPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x5004), std::make_shared<dev::precompiled::GroupSigPrecompiled>());
    context->setAddress2Precompiled(
        Address(0x5005), std::make_shared<dev::precompiled::RingSigPrecompiled>());
}
