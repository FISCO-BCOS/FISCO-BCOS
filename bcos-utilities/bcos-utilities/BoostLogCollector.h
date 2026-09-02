/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief: log file collector factory, split out of BoostLog.h so that the heavy
 *         boost.log text_file_backend sink header is only paid by its few users
 *         (BoostLog.cpp, BoostLogInitializer.cpp, TestBoostLog.cpp)
 *
 * @file: BoostLogCollector.h
 */
#pragma once

#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/shared_ptr.hpp>
#include <cstdint>

namespace bcos::log
{
boost::shared_ptr<boost::log::sinks::file::collector> make_collector(
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files, bool convert_tar_gz);
}  // namespace bcos::log
