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
 * @file BoostLogThreadNameAttribute.h
 * @author xingqiangbai
 * @date 2024-01-16
 */
#pragma once
#include "Common.h"
#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/attribute_cast.hpp>
#include <boost/log/attributes/attribute_value.hpp>
#include <boost/log/attributes/attribute_value_impl.hpp>
#include <boost/log/core.hpp>
#include <string>

namespace bcos::log
{
class thread_name_impl : public boost::log::attribute::impl
{
public:
    boost::log::attribute_value get_value() override
    {
        auto name = bcos::pthread_getThreadName();
        return boost::log::attributes::make_attribute_value(name.empty() ? "Unnamed" : name);
    }
};

class thread_name : public boost::log::attribute
{
public:
    thread_name() : boost::log::attribute(new thread_name_impl()) {}
    explicit thread_name(boost::log::attributes::cast_source const& source)
      : boost::log::attribute(source.as<thread_name_impl>())
    {}
};
}  // namespace bcos::log