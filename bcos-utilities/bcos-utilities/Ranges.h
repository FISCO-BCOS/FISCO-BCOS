#pragma once

#ifdef USE_STD_RANGES
#include <ranges>
namespace RANGES = ::std::ranges;
#else
#include <range/v3/all.hpp>
namespace RANGES = ::ranges::cpp20;
#endif