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
/** @file BlockVerifier.h
 *  @author mingzhenliu
 *  @date 20180921
 */

#pragma once


#include <libutilities/Common.h>
#include <libutilities/DataConvertUtility.h>
#include <libutilities/FixedHash.h>
#include <memory>

#define BLOCKVERIFIER_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("BLOCKVERIFIER")
#define EXECUTIVECONTEXT_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("EXECUTIVECONTEXT")
#define PARA_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("PARA") << LOG_BADGE(utcTime())

namespace bcos
{
namespace blockverifier
{
struct BlockInfo
{
    bcos::h256 hash;
    int64_t number;
    bcos::h256 stateRoot;
};
}  // namespace blockverifier
}  // namespace bcos
