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
 * @brief interface of ExecutorStatus
 * @file ExecutorStatus.h
 * @author: jimmyshi
 * @date: 2021-07-03
 */

#pragma once
#include "../protocol/LogEntry.h"
#include "../protocol/ProtocolTypeDef.h"
#include <boost/iterator/iterator_categories.hpp>
#include <boost/range/any_range.hpp>
#include <memory>
#include <sstream>
#include <string_view>

namespace bcos
{
namespace protocol
{
class ExecutorStatus
{
public:
    using UniquePtr = std::unique_ptr<ExecutorStatus>;
    using UniqueConstPtr = std::unique_ptr<const ExecutorStatus>;

    virtual ~ExecutorStatus() = default;

    int64_t seq() const { return m_seq; }
    void setSeq(int64_t seq) { m_seq = seq; }

private:
    int64_t m_seq;
};


}  // namespace protocol
}  // namespace bcos
